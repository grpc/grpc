# Copyright 2018 gRPC authors.
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

cimport cpython
from libc cimport string
from libc.stdlib cimport malloc, free
import errno
gevent_g = None
gevent_socket = None
gevent_hub = None
gevent_event = None
g_event = None
g_pool = None

cdef grpc_error* grpc_error_none():
  return <grpc_error*>0

cdef grpc_error* socket_error(str syscall, str err):
  error_str = "{} failed: {}".format(syscall, err)
  error_bytes = str_to_bytes(error_str)
  return grpc_socket_error(error_bytes)

cdef resolved_addr_to_tuple(grpc_resolved_address* address):
  cdef char* res_str
  port = grpc_sockaddr_get_port(address)
  str_len = grpc_sockaddr_to_string(&res_str, address, 0) 
  byte_str = _decode(<bytes>res_str[:str_len])
  if byte_str.endswith(':' + str(port)):
    byte_str = byte_str[:(0 - len(str(port)) - 1)]
  byte_str = byte_str.lstrip('[')
  byte_str = byte_str.rstrip(']')
  byte_str = '{}'.format(byte_str)
  return byte_str, port

cdef sockaddr_to_tuple(const grpc_sockaddr* address, size_t length):
  cdef grpc_resolved_address c_addr
  string.memcpy(<void*>c_addr.addr, <void*> address, length)
  c_addr.len = length
  return resolved_addr_to_tuple(&c_addr)

cdef sockaddr_is_ipv4(const grpc_sockaddr* address, size_t length):
  cdef grpc_resolved_address c_addr
  string.memcpy(<void*>c_addr.addr, <void*> address, length)
  c_addr.len = length
  return grpc_sockaddr_get_uri_scheme(&c_addr) == b'ipv4'

cdef grpc_resolved_addresses* tuples_to_resolvaddr(tups):
  cdef grpc_resolved_addresses* addresses
  tups_set = set((tup[4][0], tup[4][1]) for tup in tups)
  addresses = <grpc_resolved_addresses*> malloc(sizeof(grpc_resolved_addresses))
  addresses.naddrs = len(tups_set)
  addresses.addrs = <grpc_resolved_address*> malloc(sizeof(grpc_resolved_address) * len(tups_set))
  i = 0
  for tup in set(tups_set):
    hostname = str_to_bytes(tup[0])
    grpc_string_to_sockaddr(&addresses.addrs[i], hostname, tup[1])
    i += 1
  return addresses

def _spawn_greenlet(*args):
  greenlet = g_pool.spawn(*args)

###############################
### socket implementation ###
###############################

cdef class SocketWrapper:
  def __cinit__(self):
    self.sockopts = []
    self.socket = None
    self.c_socket = NULL
    self.c_buffer = NULL
    self.len = 0

cdef grpc_error* socket_init(grpc_custom_socket* socket, int domain) with gil:
  sw = SocketWrapper()
  sw.c_socket = socket
  sw.sockopts = []
  cpython.Py_INCREF(sw)
  # Python doesn't support AF_UNSPEC sockets, so we defer creation until
  # bind/connect when we know what type of socket we need
  sw.socket = None
  sw.closed = False
  sw.accepting_socket = NULL
  socket.impl = <void*>sw
  return grpc_error_none()

cdef socket_connect_async_cython(SocketWrapper socket_wrapper, addr_tuple):
  try:
    socket_wrapper.socket.connect(addr_tuple)
    socket_wrapper.connect_cb(<grpc_custom_socket*>socket_wrapper.c_socket,
                              grpc_error_none())
  except IOError as io_error:
    socket_wrapper.connect_cb(<grpc_custom_socket*>socket_wrapper.c_socket,
                              socket_error("connect", str(io_error)))
  g_event.set()

def socket_connect_async(socket_wrapper, addr_tuple):
  socket_connect_async_cython(socket_wrapper, addr_tuple)

cdef void socket_connect(grpc_custom_socket* socket, const grpc_sockaddr* addr,
                         size_t addr_len,
                         grpc_custom_connect_callback cb) with gil:
  py_socket = None
  socket_wrapper = <SocketWrapper>socket.impl
  socket_wrapper.connect_cb = cb
  addr_tuple = sockaddr_to_tuple(addr, addr_len)
  if sockaddr_is_ipv4(addr, addr_len):
      py_socket = gevent_socket.socket(gevent_socket.AF_INET)
  else:
      py_socket = gevent_socket.socket(gevent_socket.AF_INET6)
  applysockopts(py_socket)
  socket_wrapper.socket = py_socket
  _spawn_greenlet(socket_connect_async, socket_wrapper, addr_tuple)

cdef void socket_destroy(grpc_custom_socket* socket) with gil:
  cpython.Py_DECREF(<SocketWrapper>socket.impl)

cdef void socket_shutdown(grpc_custom_socket* socket) with gil:
  try:
    (<SocketWrapper>socket.impl).socket.shutdown(gevent_socket.SHUT_RDWR)
  except IOError as io_error:
    if io_error.errno != errno.ENOTCONN:
      raise io_error

cdef void socket_close(grpc_custom_socket* socket,
                       grpc_custom_close_callback cb) with gil:
  socket_wrapper = (<SocketWrapper>socket.impl)
  if socket_wrapper.socket is not None:
    socket_wrapper.socket.close()
    socket_wrapper.closed = True
    socket_wrapper.close_cb = cb
    # Delay the close callback until the accept() call has picked it up
    if socket_wrapper.accepting_socket != NULL:
      return
  socket_wrapper.close_cb(socket)

def socket_sendmsg(socket, write_bytes):
  try:
    return socket.sendmsg(write_bytes)
  except AttributeError:
    # sendmsg not available on all Pythons/Platforms
    return socket.send(b''.join(write_bytes))

cdef socket_write_async_cython(SocketWrapper socket_wrapper, write_bytes):
  try:
    while write_bytes:
      sent_byte_count = socket_sendmsg(socket_wrapper.socket, write_bytes)
      while sent_byte_count > 0:
        if sent_byte_count < len(write_bytes[0]):
          write_bytes[0] = write_bytes[0][sent_byte_count:]
          sent_byte_count = 0
        else:
          sent_byte_count -= len(write_bytes[0])
          write_bytes = write_bytes[1:]
    socket_wrapper.write_cb(<grpc_custom_socket*>socket_wrapper.c_socket,
                            grpc_error_none())
  except IOError as io_error:
    socket_wrapper.write_cb(<grpc_custom_socket*>socket_wrapper.c_socket,
                            socket_error("send", str(io_error)))
  g_event.set()

def socket_write_async(socket_wrapper, write_bytes):
  socket_write_async_cython(socket_wrapper, write_bytes)

cdef void socket_write(grpc_custom_socket* socket, grpc_slice_buffer* buffer,
                       grpc_custom_write_callback cb) with gil:
  cdef char* start
  sw = <SocketWrapper>socket.impl
  sw.write_cb = cb
  write_bytes = []
  for i in range(buffer.count):
    start = grpc_slice_buffer_start(buffer, i)
    length = grpc_slice_buffer_length(buffer, i)
    write_bytes.append(<bytes>start[:length])
  _spawn_greenlet(socket_write_async, <SocketWrapper>socket.impl, write_bytes)

cdef socket_read_async_cython(SocketWrapper socket_wrapper):
  cdef char* buff_char_arr
  try:
    buff_str = socket_wrapper.socket.recv(socket_wrapper.len)
    buff_char_arr = buff_str
    string.memcpy(<void*>socket_wrapper.c_buffer, buff_char_arr, len(buff_str))
    socket_wrapper.read_cb(<grpc_custom_socket*>socket_wrapper.c_socket,
                           len(buff_str), grpc_error_none())
  except IOError as io_error:
    socket_wrapper.read_cb(<grpc_custom_socket*>socket_wrapper.c_socket,
                           -1, socket_error("recv", str(io_error)))
  g_event.set()

def socket_read_async(socket_wrapper):
  socket_read_async_cython(socket_wrapper)

cdef void socket_read(grpc_custom_socket* socket, char* buffer,
                      size_t length, grpc_custom_read_callback cb) with gil:
  sw = <SocketWrapper>socket.impl
  sw.read_cb = cb
  sw.c_buffer = buffer
  sw.len = length
  _spawn_greenlet(socket_read_async, sw)

cdef grpc_error* socket_getpeername(grpc_custom_socket* socket,
                                    const grpc_sockaddr* addr,
                                    int* length) with gil:
  cdef char* src_buf
  peer = (<SocketWrapper>socket.impl).socket.getpeername()

  cdef grpc_resolved_address c_addr
  hostname = str_to_bytes(peer[0])
  grpc_string_to_sockaddr(&c_addr, hostname, peer[1])
  string.memcpy(<void*>addr, <void*>c_addr.addr, c_addr.len)
  length[0] = c_addr.len
  return grpc_error_none()  

cdef grpc_error* socket_getsockname(grpc_custom_socket* socket,
                                    const grpc_sockaddr* addr,
                                    int* length) with gil:
  cdef char* src_buf
  cdef grpc_resolved_address c_addr
  if (<SocketWrapper>socket.impl).socket is None:
    peer = ('0.0.0.0', 0)
  else:
    peer = (<SocketWrapper>socket.impl).socket.getsockname()
  hostname = str_to_bytes(peer[0])
  grpc_string_to_sockaddr(&c_addr, hostname, peer[1])
  string.memcpy(<void*>addr, <void*>c_addr.addr, c_addr.len)
  length[0] = c_addr.len
  return grpc_error_none()

def applysockopts(s):
  s.setsockopt(gevent_socket.SOL_SOCKET, gevent_socket.SO_REUSEADDR, 1)
  s.setsockopt(gevent_socket.IPPROTO_TCP, gevent_socket.TCP_NODELAY, True)

cdef grpc_error* socket_bind(grpc_custom_socket* socket,
                             const grpc_sockaddr* addr,
                             size_t len, int flags) with gil:
  addr_tuple = sockaddr_to_tuple(addr, len)
  try:
    try:
      py_socket = gevent_socket.socket(gevent_socket.AF_INET)
      applysockopts(py_socket)
      py_socket.bind(addr_tuple)
    except gevent_socket.gaierror as e:
      py_socket = gevent_socket.socket(gevent_socket.AF_INET6)
      applysockopts(py_socket)
      py_socket.bind(addr_tuple)
    (<SocketWrapper>socket.impl).socket = py_socket
  except IOError as io_error:
    return socket_error("bind", str(io_error))
  else:
    return grpc_error_none()

cdef grpc_error* socket_listen(grpc_custom_socket* socket) with gil:
  (<SocketWrapper>socket.impl).socket.listen(50)
  return grpc_error_none()

cdef void accept_callback_cython(SocketWrapper s):
   try:
     conn, address = s.socket.accept()
     sw = SocketWrapper()
     sw.closed = False
     sw.c_socket = s.accepting_socket
     sw.sockopts = []
     sw.socket = conn
     sw.c_socket.impl = <void*>sw
     sw.accepting_socket = NULL
     cpython.Py_INCREF(sw)
     s.accepting_socket = NULL
     s.accept_cb(<grpc_custom_socket*>s.c_socket, sw.c_socket, grpc_error_none())
   except IOError as io_error:
      #TODO actual error
      s.accepting_socket = NULL
      s.accept_cb(<grpc_custom_socket*>s.c_socket, s.accepting_socket,
                  socket_error("accept", str(io_error)))
      if s.closed:
        s.close_cb(<grpc_custom_socket*>s.c_socket)
   g_event.set()

def socket_accept_async(s):
  accept_callback_cython(s)

cdef void socket_accept(grpc_custom_socket* socket, grpc_custom_socket* client,
                        grpc_custom_accept_callback cb) with gil:
  sw = <SocketWrapper>socket.impl
  sw.accepting_socket = client
  sw.accept_cb = cb
  _spawn_greenlet(socket_accept_async, sw)

#####################################
######Resolver implementation #######
#####################################

cdef class ResolveWrapper:
  def __cinit__(self):
    self.c_resolver = NULL
    self.c_host = NULL
    self.c_port = NULL

cdef socket_resolve_async_cython(ResolveWrapper resolve_wrapper):
  try:
    res = gevent_socket.getaddrinfo(resolve_wrapper.c_host, resolve_wrapper.c_port)
    grpc_custom_resolve_callback(<grpc_custom_resolver*>resolve_wrapper.c_resolver,
                                 tuples_to_resolvaddr(res), grpc_error_none())
  except IOError as io_error:
    grpc_custom_resolve_callback(<grpc_custom_resolver*>resolve_wrapper.c_resolver,
                                 <grpc_resolved_addresses*>0,
                                 socket_error("getaddrinfo", str(io_error)))
  g_event.set()

def socket_resolve_async_python(resolve_wrapper):
  socket_resolve_async_cython(resolve_wrapper)

cdef void socket_resolve_async(grpc_custom_resolver* r, char* host, char* port) with gil:
  rw = ResolveWrapper()
  rw.c_resolver = r
  rw.c_host = host
  rw.c_port = port
  _spawn_greenlet(socket_resolve_async_python, rw)

cdef grpc_error* socket_resolve(char* host, char* port,
                                grpc_resolved_addresses** res) with gil:
    try:
      result = gevent_socket.getaddrinfo(host, port)
      res[0] = tuples_to_resolvaddr(result)
      return grpc_error_none()
    except IOError as io_error:
      return socket_error("getaddrinfo", str(io_error))

###############################
### timer implementation ######
###############################

cdef class TimerWrapper:
  def __cinit__(self, deadline):
    self.timer = gevent_hub.get_hub().loop.timer(deadline)
    self.event = None

  def start(self):
    self.event = gevent_event.Event()
    self.timer.start(self.on_finish)

  def on_finish(self):
    grpc_custom_timer_callback(self.c_timer, grpc_error_none())
    self.timer.stop()
    g_event.set()

  def stop(self):
    self.event.set()
    self.timer.stop()

cdef void timer_start(grpc_custom_timer* t) with gil:
  timer = TimerWrapper(t.timeout_ms / 1000.0)
  timer.c_timer = t
  t.timer = <void*>timer
  timer.start()

cdef void timer_stop(grpc_custom_timer* t) with gil:
  time_wrapper = <object>t.timer
  time_wrapper.stop()

###############################
### pollset implementation ###
###############################

cdef void init_loop() with gil:
  pass

cdef void destroy_loop() with gil:
  g_pool.join()

cdef void kick_loop() with gil:
  g_event.set()

cdef void run_loop(size_t timeout_ms) with gil:
    timeout = timeout_ms / 1000.0
    if timeout_ms > 0:
      g_event.wait(timeout)
      g_event.clear()

###############################
### Initializer ###############
###############################

cdef grpc_socket_vtable gevent_socket_vtable
cdef grpc_custom_resolver_vtable gevent_resolver_vtable
cdef grpc_custom_timer_vtable gevent_timer_vtable
cdef grpc_custom_poller_vtable gevent_pollset_vtable

def init_grpc_gevent():
  # Lazily import gevent
  global gevent_socket
  global gevent_g
  global gevent_hub
  global gevent_event
  global g_event
  global g_pool
  import gevent
  gevent_g = gevent
  import gevent.socket
  gevent_socket = gevent.socket
  import gevent.hub
  gevent_hub = gevent.hub
  import gevent.event
  gevent_event = gevent.event
  import gevent.pool

  g_event = gevent.event.Event()
  g_pool = gevent.pool.Group()
  gevent_resolver_vtable.resolve = socket_resolve
  gevent_resolver_vtable.resolve_async = socket_resolve_async

  gevent_socket_vtable.init = socket_init
  gevent_socket_vtable.connect = socket_connect
  gevent_socket_vtable.destroy = socket_destroy
  gevent_socket_vtable.shutdown = socket_shutdown
  gevent_socket_vtable.close = socket_close
  gevent_socket_vtable.write = socket_write
  gevent_socket_vtable.read = socket_read
  gevent_socket_vtable.getpeername = socket_getpeername
  gevent_socket_vtable.getsockname = socket_getsockname
  gevent_socket_vtable.bind = socket_bind
  gevent_socket_vtable.listen = socket_listen
  gevent_socket_vtable.accept = socket_accept

  gevent_timer_vtable.start = timer_start
  gevent_timer_vtable.stop = timer_stop

  gevent_pollset_vtable.init = init_loop
  gevent_pollset_vtable.poll = run_loop
  gevent_pollset_vtable.kick = kick_loop
  gevent_pollset_vtable.shutdown = destroy_loop

  grpc_custom_iomgr_init(&gevent_socket_vtable,
                         &gevent_resolver_vtable,
                         &gevent_timer_vtable,
                         &gevent_pollset_vtable)
