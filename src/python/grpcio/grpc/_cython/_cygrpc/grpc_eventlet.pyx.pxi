# Copyright 2020 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# distutils: language=c++
#
#
#
# IMPLEMENTATION DETAILS:
# =======================
#
# Below code implements the eventlet custom IO manager code, while gevent's
# implementation was used as reference, many modifications were necessary,
# since these two libraries use greenthreads differently, and we had to
# workaround some of eventlet's quirks resulting in less readable code.
#
# These comments try to provide context to some of the implementation
# decisions.
#
# Event class
# -----------
#
# Eventlet's Event class is not implemented using any event notification
# mechanism (ie: epoll), but with greenthread switching and cannot be used
# between different native threads.  In our case it means that we cannot use
# them to kick a poll from a different native thread. That's why we have our
# own implementation.
#
# Greenthread pools
# -----------------
#
# Eventlet has a Hub for each native thread with a greenthread main loop that
# automatically handles the polling of readers and writers, timers, firing the
# different greenthreas, etc.  This means that even if we use a shared
# GreenPool between the main native thread (MT) and the ThreadPoolExecutor
# native thread (TPE), in the end the GreenPool will have greenthreads running
# on both hubs.  If we are not careful where we run some of our greenthreads we
# could en up in a deadlock, mostly with the kick and poll.
#
# The deadlock happends when a kick from the TPE schedules the switch to the
# poller on the TPE hub, because then the poller code that was started on the
# MT and is waiting for a kick continues on the TPE (different thread), so we
# end up with the TM having a gRPC lock and waiting for the GIL, and the TPE
# waiting on the gRPC lock and having the GIL.
#
# That is why we schedule some switch calls on the poller hub instead of the
# current hub on the kick.
#
# MAINLOOP
# --------
#
# We use a socket pair for the kick when signalling the poller because the
# eventlet's MAINLOOP code waits using a poll call and setting the timeout to
# the time when the next greenthread that needs to be run.  This means new
# greenthreads that are scheduled in the hub from another native thread will
# not be picked up until the poll call from the hub exits.  By having a
# greenthread waiting on a socket read operation we can wake the MAINLOOP from
# another thread so that the new calls that have been added to the hub are
# taken into account without delay.
#
# Socket close
# ------------
#
# Eventlet does not signal socket readers/writers when the socket is closed
# from another greenthread, (sometimes we seem to get an OSError, but not
# always), so we have to code the signaling ourselves by raising exceptions on
# the different greenthreads.  If we don't, we won't be able to stop the
# server, since the accept call never returns and the accept callback is never
# called.
#
# We also have to make sure that we only call the close callback after all the
# other readers/writers callbacks have been completed.
#
# Class methods
# -------------
#
# Methods e_timer_finished and e_signal_closed could be class methods instead,
# but since they are cdef methods, it seems that these behave as static methods
# when added to the hub, so we have to manually pass the "self" parameter on
# the timer/spawn call or it will complain it is missing the parameter at
# runtime.  Since that is the case, we just avoid having the extra attribute
# access by using the global methods.
#
# Global variables
# ----------------
#
# We don't set defaults for global variables at the beginning of our code, they
# are just created in different methods:
# - In init_grpc_eventlet:
#     * e_eventlet_lib
#     * e_socket_lib
#     * eventlet_spawn
#     * e_connection_backlog
#     * e_greenlet
#     * e_GlobalTimer
#     * SOCKET_CLOSED_EXC
#     * DO_REUSE_PORT
# - In eventlet_init_poll:
#     * e_poller_waiters
#     * e_poller_wsock
#     * e_poller_hub
#
# The exception is e_poller_is_set, which is a non Python global variable.
#
# Notes
# -----
#
# Names of parameters in methods called by eventlet_spawn ARE IMPORTANT.  Since
# we are calling cdef code directly in eventlet_spawn, instead of using an
# intermediary python method, all parameters of the same class IN ALL these
# callback methods must have the same name, otherwise compiler will fail
# because it will generate multiple wrapers for the same class.  This includes
# Python classes such as object, tuple, etc.
#
# Blocking threads in user's code will affect the performance of the gRPC
# library, since we are spawning the async calls on greenthreads on the same
# hub.

cimport cpython
import errno
from libc cimport string


cdef int e_poller_is_set = 0


#############################
### socket implementation ###
#############################

cdef class EventletSocketWrapper:
    def __cinit__(self):
        fork_handlers_and_grpc_init()
        self.accepting_socket = NULL
        self.socket = None
        self.users = []
        self.c_socket = NULL
        self.c_buffer = NULL
        self.len = 0

    def __dealloc__(self):
        grpc_shutdown_blocking()

    cdef call_close_cb(EventletSocketWrapper self):
        # Close callback must be called after all other socket callbacks
        if not self.users:
            # This makes sure there's only 1 caller (in case we are waiting on
            # read and write or if we also get the OSError exception) and also
            # prevents races.
            cb, self.close_cb = self.close_cb, <grpc_custom_close_callback>0
            if cb:
                cb(self.c_socket)


cdef void e_signal_closed(EventletSocketWrapper socket_wrapper,
                          object current):
    # If thread hasn't done its callback yet, then abort it
    if current in socket_wrapper.users:
        try:
            current.throw(SOCKET_CLOSED_EXC)
        # Throw returns raising GreenletExit (it's not an Exception instance)
        except:
            return

    # If the thread finished before we could send it the signal, then try to do
    # the close callback.
    socket_wrapper.call_close_cb()


cdef grpc_error* eventlet_socket_init(grpc_custom_socket* socket,
                                      int domain) with gil:
    # Python doesn't support AF_UNSPEC sockets, so we defer creation until
    # bind/connect when we know the type and can set the socket attribute
    sw = EventletSocketWrapper()
    sw.c_socket = socket
    socket.impl = <void*>sw
    cpython.Py_INCREF(sw)
    return <grpc_error*>0


cdef tuple eventlet_get_socket_and_addr(const grpc_sockaddr *addr,
                                        size_t addr_len, int flags=0):
    if sockaddr_is_ipv4(addr, addr_len):
        py_socket = e_socket_lib.socket(e_socket_lib.AF_INET)
    else:
        py_socket = e_socket_lib.socket(e_socket_lib.AF_INET6)

    py_socket.setsockopt(e_socket_lib.SOL_SOCKET, e_socket_lib.SO_REUSEADDR, 1)
    if DO_REUSE_PORT and (flags & GRPC_CUSTOM_SOCKET_OPT_SO_REUSEPORT):
        py_socket.setsockopt(e_socket_lib.SOL_SOCKET,
                             e_socket_lib.SO_REUSEPORT, 1)
    py_socket.setsockopt(e_socket_lib.IPPROTO_TCP, e_socket_lib.TCP_NODELAY,
                         True)

    return py_socket, sockaddr_to_tuple(addr, addr_len)


cdef eventlet_socket_connect_async(EventletSocketWrapper socket_wrapper,
                                   tuple addr_tuple):
    try:
        current = e_greenlet.getcurrent()
        socket_wrapper.users.append(current)
        socket_wrapper.socket.connect(addr_tuple)
        socket_wrapper.connect_cb(socket_wrapper.c_socket, <grpc_error*>0)
        socket_wrapper.users.remove(current)
    except (IOError, OSError) as exc:
        socket_wrapper.users.remove(current)
        socket_wrapper.connect_cb(socket_wrapper.c_socket,
                                  socket_error('connect', str(exc)))
        socket_wrapper.call_close_cb()


cdef void eventlet_socket_connect(grpc_custom_socket* socket,
                                  const grpc_sockaddr* addr, size_t addr_len,
                                  grpc_custom_connect_callback cb) with gil:
    socket_wrapper = <EventletSocketWrapper>socket.impl
    socket_wrapper.connect_cb = cb
    socket_wrapper.socket, addr_tuple = eventlet_get_socket_and_addr(addr,
                                                                     addr_len)
    eventlet_spawn(eventlet_socket_connect_async, socket_wrapper, addr_tuple)


cdef void eventlet_socket_destroy(grpc_custom_socket* socket):
    cpython.Py_DECREF(<EventletSocketWrapper>socket.impl)


cdef void eventlet_socket_shutdown(grpc_custom_socket* socket) with gil:
    try:
        (<EventletSocketWrapper>socket.impl).socket.shutdown(
            e_socket_lib.SHUT_RDWR)
    except IOError as io_error:
        if io_error.errno != errno.ENOTCONN:
            raise io_error
    except Exception:
        pass


cdef void eventlet_socket_close(grpc_custom_socket* socket,
                                grpc_custom_close_callback cb) with gil:
    socket_wrapper = (<EventletSocketWrapper>socket.impl)
    # Is None after eventlet_socket_init and before bind/connect
    if socket_wrapper.socket is not None:
        socket_wrapper.socket.close()
        # Eventlet does not raise an exception to polling greenthreads, so they
        # may be left waiting forever. Raise it ourselves.
        if socket_wrapper.users:
            socket_wrapper.close_cb = cb
            for greenlet in socket_wrapper.users:
                eventlet_spawn(e_signal_closed, socket_wrapper, greenlet)
            return
    cb(socket)


cdef eventlet_socket_write_async(EventletSocketWrapper socket_wrapper,
                                 write_bytes):
    try:
        current = e_greenlet.getcurrent()
        socket_wrapper.users.append(current)
        socket_wrapper.socket.sendall(write_bytes)
        socket_wrapper.write_cb(socket_wrapper.c_socket, <grpc_error*>0)
        socket_wrapper.users.remove(current)
    except (IOError, OSError) as exc:
        socket_wrapper.users.remove(current)
        socket_wrapper.write_cb(socket_wrapper.c_socket,
                                socket_error('send', str(exc)))
        socket_wrapper.call_close_cb()


cdef void eventlet_socket_write(grpc_custom_socket* socket,
                                grpc_slice_buffer* buffer,
                                grpc_custom_write_callback cb) with gil:
    cdef char* start
    sw = <EventletSocketWrapper>socket.impl
    sw.write_cb = cb
    data = bytearray()
    for i in range(buffer.count):
        start = grpc_slice_buffer_start(buffer, i)
        length = grpc_slice_buffer_length(buffer, i)
        data.extend(<bytes>start[:length])
    eventlet_spawn(eventlet_socket_write_async, sw, data)


cdef eventlet_socket_read_async(EventletSocketWrapper socket_wrapper):
    cdef char* buff_char_arr
    try:
        current = e_greenlet.getcurrent()
        socket_wrapper.users.append(current)
        buff_str = socket_wrapper.socket.recv(socket_wrapper.len)
        buff_char_arr = buff_str
        string.memcpy(<void*>socket_wrapper.c_buffer, buff_char_arr,
                      len(buff_str))
        socket_wrapper.read_cb(socket_wrapper.c_socket, len(buff_str),
                               <grpc_error*>0)
        socket_wrapper.users.remove(current)
    except (IOError, OSError) as exc:
        socket_wrapper.users.remove(current)
        socket_wrapper.read_cb(<grpc_custom_socket*>socket_wrapper.c_socket,
                               -1, socket_error('recv', str(exc)))
        socket_wrapper.call_close_cb()


cdef void eventlet_socket_read(grpc_custom_socket* socket, char* buffer,
                               size_t length,
                               grpc_custom_read_callback cb) with gil:
    sw = <EventletSocketWrapper>socket.impl
    sw.read_cb = cb
    sw.c_buffer = buffer
    sw.len = length
    eventlet_spawn(eventlet_socket_read_async, sw)


cdef grpc_error* eventlet_socket_getpeername(grpc_custom_socket* socket,
                                             const grpc_sockaddr* addr,
                                             int* length) with gil:
    cdef grpc_resolved_address c_addr

    peer = (<EventletSocketWrapper>socket.impl).socket.getpeername()
    hostname = str_to_bytes(peer[0])
    grpc_string_to_sockaddr(&c_addr, hostname, peer[1])
    string.memcpy(<void*>addr, <void*>c_addr.addr, c_addr.len)
    length[0] = c_addr.len
    return <grpc_error*>0


cdef grpc_error* eventlet_socket_getsockname(grpc_custom_socket* socket,
                                             const grpc_sockaddr* addr,
                                             int* length) with gil:
    cdef grpc_resolved_address c_addr
    if (<EventletSocketWrapper>socket.impl).socket is None:
        peer = ('0.0.0.0', 0)
    else:
        peer = (<EventletSocketWrapper>socket.impl).socket.getsockname()
    hostname = str_to_bytes(peer[0])
    grpc_string_to_sockaddr(&c_addr, hostname, peer[1])
    string.memcpy(<void*>addr, <void*>c_addr.addr, c_addr.len)
    length[0] = c_addr.len
    return <grpc_error*>0


cdef grpc_error* eventlet_socket_bind(grpc_custom_socket* socket,
                                      const grpc_sockaddr* addr, size_t len,
                                      int flags) with gil:
    py_socket, addr_tuple = eventlet_get_socket_and_addr(addr, len, flags)
    try:
        py_socket.bind(addr_tuple)
    except Exception as exc:
        return socket_error('bind', str(exc))
    (<EventletSocketWrapper>socket.impl).socket = py_socket
    return <grpc_error*>0


cdef grpc_error* eventlet_socket_listen(grpc_custom_socket* socket) with gil:
    (<EventletSocketWrapper>socket.impl).socket.listen(e_connection_backlog)
    return <grpc_error*>0


cdef void eventlet_socket_accept_async(EventletSocketWrapper socket_wrapper):
    try:
        current = e_greenlet.getcurrent()
        socket_wrapper.users.append(current)
        conn, address = socket_wrapper.socket.accept()
        sw = EventletSocketWrapper()
        sw.c_socket = socket_wrapper.accepting_socket
        sw.socket = conn
        sw.c_socket.impl = <void*>sw
        cpython.Py_INCREF(sw)
        socket_wrapper.accept_cb(socket_wrapper.c_socket, sw.c_socket,
                                 <grpc_error*>0)
        socket_wrapper.users.remove(current)
    except (IOError, OSError) as exc:
        socket_wrapper.users.remove(current)
        socket_wrapper.accept_cb(<grpc_custom_socket*>socket_wrapper.c_socket,
                                 NULL, socket_error('accept', str(exc)))
        socket_wrapper.call_close_cb()


cdef void eventlet_socket_accept(grpc_custom_socket* socket,
                                 grpc_custom_socket* client,
                                 grpc_custom_accept_callback cb) with gil:
    sw = <EventletSocketWrapper>socket.impl
    sw.accepting_socket = client
    sw.accept_cb = cb
    eventlet_spawn(eventlet_socket_accept_async, sw)


###############################
### resolver implementation ###
###############################

cdef class EventletResolveWrapper:
    def __cinit__(self):
        fork_handlers_and_grpc_init()
        self.c_resolver = NULL
        self.c_host = NULL
        self.c_port = NULL

    def __dealloc__(self):
        grpc_shutdown_blocking()


cdef eventlet_socket_resolve_async_callback(
        EventletResolveWrapper resolve_wrapper):
    try:
        # Eventlet doesn't handle bytes, so we may need conversion. Issue
        # https://github.com/eventlet/eventlet/issues/599
        res = e_socket_lib.getaddrinfo(_decode(resolve_wrapper.c_host),
                                       resolve_wrapper.c_port)
        grpc_custom_resolve_callback(resolve_wrapper.c_resolver,
                                     tuples_to_resolvaddr(res),
                                     <grpc_error*>0)
    except Exception as exc:
        grpc_custom_resolve_callback(resolve_wrapper.c_resolver,
                                     <grpc_resolved_addresses*>0,
                                     socket_error('getaddrinfo', str(exc)))


cdef void eventlet_socket_resolve_async(grpc_custom_resolver* r, char* host,
                                        char* port) with gil:
    rw = EventletResolveWrapper()
    rw.c_resolver = r
    rw.c_host, rw.c_port = host, port
    eventlet_spawn(eventlet_socket_resolve_async_callback, rw)


cdef grpc_error* eventlet_socket_resolve(char* host, char* port,
                                         grpc_resolved_addresses** res
                                         ) with gil:
    try:
        # Eventlet doesn't handle bytes, so we may need conversion. Issue
        # https://github.com/eventlet/eventlet/issues/599
        result = e_socket_lib.getaddrinfo(_decode(host), port)
        res[0] = tuples_to_resolvaddr(result)
        return <grpc_error*>0
    except Exception as exc:
        return socket_error('getaddrinfo', str(exc))


############################
### timer implementation ###
############################

cdef e_timer_finished(EventletTimerWrapper time_wrapper):
    # This method cannot be an instance method because the gRPC custom
    # timer callback will call `eventlet_timer_stop`, where we decref the
    # wrapper and free the instance.
    grpc_custom_timer_callback(time_wrapper.c_timer, <grpc_error*>0)


cdef class EventletTimerWrapper:
    def __cinit__(self, timeout_ms):
        fork_handlers_and_grpc_init()
        self.c_timer = NULL
        self.timer = e_GlobalTimer(timeout_ms / 1000.0, e_timer_finished, self)

    def stop(self):
        self.timer.cancel()

    def start(self):
        self.timer.schedule()

    def __dealloc__(self):
        grpc_shutdown_blocking()


cdef void eventlet_timer_start(grpc_custom_timer* t) with gil:
    wrapper = EventletTimerWrapper(t.timeout_ms)
    t.timer = <void *>wrapper
    cpython.Py_INCREF(wrapper)
    wrapper.c_timer = t
    wrapper.start()


cdef void eventlet_timer_stop(grpc_custom_timer* t) with gil:
    wrapper = <object>t.timer
    wrapper.stop()
    cpython.Py_DECREF(wrapper)


##############################
### pollset implementation ###
##############################

def e_sock_waiter(rsock):
    """Wait on a socket so we can wake the MAINLOOP from other threads."""
    do_run = 1
    while do_run:
        do_run = int(rsock.recv(1))
    # We'll only get here after eventlet_destroy_poll has been called
    rsock.close()


cdef void eventlet_init_poll() with gil:
    """Initialize poller's global variables.

    Initialized global variables are e_poller_waiters, e_poller_is_set,
    e_poller_wsock, and e_poller_hub
    """
    global e_poller_waiters, e_poller_is_set, e_poller_wsock, e_poller_hub

    e_poller_waiters = []
    e_poller_is_set = 0

    # Connected socket pair used to wake up poller greenthreads
    socket = e_eventlet_lib.patcher.original('socket')
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(('127.0.0.1', 0))
    sock.listen(1)
    csock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    csock.connect(sock.getsockname())
    csock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, True)
    e_poller_wsock, _addr = sock.accept()
    e_poller_wsock.settimeout(None)
    e_poller_wsock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, True)
    sock.close()
    rsock = e_eventlet_lib.greenio.GreenSocket(csock)
    rsock.settimeout(None)

    e_poller_hub = e_eventlet_lib.hubs.get_hub()

    # Start the greenthread in charge of waking up the MAINLOOP of the poller
    # thread.
    g = e_greenlet(e_sock_waiter, parent=e_poller_hub.greenlet)
    e_poller_hub.schedule_call_global(0, g.switch, rsock)


cdef void eventlet_destroy_poll() with gil:
    global e_poller_waiters, e_poller_wsock, e_poller_hub

    e_poller_wsock.sendall(b'0')
    # Socket will get closed when freed
    e_poller_wsock = e_poller_hub = e_poller_waiters = None


cdef void eventlet_kick_poll() with gil:
    """Signal that a callback has been completed.

    This will release all polling greenthreads, if there are any, or just flag
    the next thread that polls.

    All asynchronous calls must do a kick before finishing, and after the
    callback, to signal the gRPC that they have completed.
    """
    global e_poller_is_set

    if not e_poller_is_set:
        e_poller_is_set = 1
        # Avoid duplicate waiter.switch calls on concurrent kicks using
        # try...except and pop instead of a while loop.
        try:
            while 1:
                waiter = e_poller_waiters.pop()
                # Cancel the timer and add a call to return `True` to each of
                # the pollers that are waiting on `e_poller_hub.switch()` in
                # the eventlet_run_poll method.
                waiter.grpc_poll_timer.cancel()
                e_poller_hub.add_timer(e_GlobalTimer(0, waiter.switch, True))
        except IndexError:
            pass
        # If we are on a different hub/native thread, then the poller's hub may
        # be waiting on a poll call with a timeout (default timeout is 60
        # seconds), and won't notice the new calls that need to be scheduled
        # until the poll timeouts.  To avoid this we send data using the socket
        # and force the poll to exit so the MAINLOOP will notice the new calls
        # it has to handle.
        if e_poller_hub is not e_eventlet_lib.hubs.get_hub():
            e_poller_wsock.sendall(b'1')


def e_poll_timeout(waiter):
    """Handle the timeout of a poller greenthread."""
    try:
        # If the waiter is no longer there, we had an unlikely race with the
        # kick, and we don't need to signal the timeout.
        e_poller_waiters.remove(waiter)
        waiter.grpc_poll_timer.cancel()
        # This will switch execution to the `e_poller_hub.switch` line in the
        # eventlet_run_poll returning False to express we timed out.
        waiter.switch(False)
    except ValueError:
        pass


cdef void eventlet_run_poll(size_t timeout_ms) with gil:
    """Wait for completion of any of the callbacks."""
    global e_poller_is_set

    # NOTE: When timeouts are 0 they are usually coming from the MAINLOOP, so
    # we cannot yield, and when the poller flag is set there's no need to wait.
    # We could check that `e_greenlet.getcurrent()` is not
    # `e_eventlet_lib.hubs.get_hub().greenlet` to know we are not in the
    # MAINLOOP, but that check is slower that checking `timeout_ms` is not 0.
    if timeout_ms and not e_poller_is_set:
        current = e_greenlet.getcurrent()
        timer = e_GlobalTimer(timeout_ms / 1000.0, e_poll_timeout, current)

        # Store the timer so we can cancel it on timeout and signal
        current.grpc_poll_timer = timer

        # Add to list of polling greenthreads that kick uses to wake them up.
        e_poller_waiters.append(current)
        e_poller_hub.add_timer(timer)

        # Now switch to the MAINLOOP.  We'll return to this greenthread once
        # the timeout switches back with False or signal with True.  Skip
        # resetting the poller flag if we didn't receive the signal.
        try:
            if not e_poller_hub.switch():
                return
        except:
            timer.cancel()
            raise

    e_poller_is_set = 0


###################
### Initializer ###
###################

cdef grpc_socket_vtable eventlet_socket_vtable
cdef grpc_custom_resolver_vtable eventlet_resolver_vtable
cdef grpc_custom_timer_vtable eventlet_timer_vtable
cdef grpc_custom_poller_vtable eventlet_pollset_vtable


def eventlet_async_callback_func(cb, args):
    # Method used by the gRPC code to run async callback functions
    eventlet_spawn(cb, *args)


def init_grpc_eventlet(connection_backlog=50):
    """Initialize Eventlet's custom IO manager.

    Here we initialize the following global variables: e_eventlet_lib,
    e_socket_lib, eventlet_spawn, e_connection_backlog, e_greenlet,
    e_GlobalTimer, and SOCKET_CLOSED_EXC, DO_REUSE_PORT
    """
    global e_eventlet_lib, e_socket_lib, eventlet_spawn, e_connection_backlog
    global e_greenlet, e_GlobalTimer, SOCKET_CLOSED_EXC, DO_REUSE_PORT

    # Lazily import libraries
    import eventlet
    from eventlet.hubs import timer
    from eventlet.support import greenlets
    import platform

    e_eventlet_lib = eventlet
    SOCKET_CLOSED_EXC = IOError('Socket closed')
    e_socket_lib = eventlet.green.socket
    e_GlobalTimer = timer.Timer
    e_greenlet  = greenlets.greenlet
    eventlet_spawn = eventlet.spawn_n
    e_connection_backlog = connection_backlog

    DO_REUSE_PORT = False if platform.system() == 'Windows' else True
    set_async_callback_func(eventlet_async_callback_func)

    eventlet_resolver_vtable.resolve = eventlet_socket_resolve
    eventlet_resolver_vtable.resolve_async = eventlet_socket_resolve_async

    eventlet_socket_vtable.init = eventlet_socket_init
    eventlet_socket_vtable.connect = eventlet_socket_connect
    eventlet_socket_vtable.destroy = eventlet_socket_destroy
    eventlet_socket_vtable.shutdown = eventlet_socket_shutdown
    eventlet_socket_vtable.close = eventlet_socket_close
    eventlet_socket_vtable.write = eventlet_socket_write
    eventlet_socket_vtable.read = eventlet_socket_read
    eventlet_socket_vtable.getpeername = eventlet_socket_getpeername
    eventlet_socket_vtable.getsockname = eventlet_socket_getsockname
    eventlet_socket_vtable.bind = eventlet_socket_bind
    eventlet_socket_vtable.listen = eventlet_socket_listen
    eventlet_socket_vtable.accept = eventlet_socket_accept

    eventlet_timer_vtable.start = eventlet_timer_start
    eventlet_timer_vtable.stop = eventlet_timer_stop

    eventlet_pollset_vtable.init = eventlet_init_poll
    eventlet_pollset_vtable.poll = eventlet_run_poll
    eventlet_pollset_vtable.kick = eventlet_kick_poll
    eventlet_pollset_vtable.shutdown = eventlet_destroy_poll

    grpc_custom_iomgr_init(&eventlet_socket_vtable,
                           &eventlet_resolver_vtable,
                           &eventlet_timer_vtable,
                           &eventlet_pollset_vtable)
