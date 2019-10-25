# Copyright 2019 gRPC authors.
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


from cpython cimport Py_INCREF, Py_DECREF
from libc cimport string

import socket as native_socket
try:
    import ipaddress  # CPython 3.3 and above
except ImportError:
    pass

cdef grpc_socket_vtable asyncio_socket_vtable
cdef grpc_custom_resolver_vtable asyncio_resolver_vtable
cdef grpc_custom_timer_vtable asyncio_timer_vtable
cdef grpc_custom_poller_vtable asyncio_pollset_vtable


cdef grpc_error* asyncio_socket_init(
        grpc_custom_socket* grpc_socket,
        int domain) with gil:
    socket = _AsyncioSocket.create(grpc_socket, None, None)
    Py_INCREF(socket)
    grpc_socket.impl = <void*>socket
    return <grpc_error*>0


cdef void asyncio_socket_destroy(grpc_custom_socket* grpc_socket) with gil:
    Py_DECREF(<_AsyncioSocket>grpc_socket.impl)


cdef void asyncio_socket_connect(
        grpc_custom_socket* grpc_socket,
        const grpc_sockaddr* addr,
        size_t addr_len,
        grpc_custom_connect_callback connect_cb) with gil:

    host, port = sockaddr_to_tuple(addr, addr_len)
    socket = <_AsyncioSocket>grpc_socket.impl
    socket.connect(host, port, connect_cb)


cdef void asyncio_socket_close(
        grpc_custom_socket* grpc_socket,
        grpc_custom_close_callback close_cb) with gil:
    socket = (<_AsyncioSocket>grpc_socket.impl)
    socket.close()
    close_cb(grpc_socket)


cdef void asyncio_socket_shutdown(grpc_custom_socket* grpc_socket) with gil:
    socket = (<_AsyncioSocket>grpc_socket.impl)
    socket.close()


cdef void asyncio_socket_write(
        grpc_custom_socket* grpc_socket,
        grpc_slice_buffer* slice_buffer,
        grpc_custom_write_callback write_cb) with gil:
    socket = (<_AsyncioSocket>grpc_socket.impl)
    socket.write(slice_buffer, write_cb)


cdef void asyncio_socket_read(
        grpc_custom_socket* grpc_socket,
        char* buffer_,
        size_t length,
        grpc_custom_read_callback read_cb) with gil:
    socket = (<_AsyncioSocket>grpc_socket.impl)
    socket.read(buffer_, length, read_cb)


cdef grpc_error* asyncio_socket_getpeername(
        grpc_custom_socket* grpc_socket,
        const grpc_sockaddr* addr,
        int* length) with gil:
    peer = (<_AsyncioSocket>grpc_socket.impl).peername()

    cdef grpc_resolved_address c_addr
    hostname = str_to_bytes(peer[0])
    grpc_string_to_sockaddr(&c_addr, hostname, peer[1])
    # TODO(https://github.com/grpc/grpc/issues/20684) Remove the memcpy
    string.memcpy(<void*>addr, <void*>c_addr.addr, c_addr.len)
    length[0] = c_addr.len
    return grpc_error_none()


cdef grpc_error* asyncio_socket_getsockname(
        grpc_custom_socket* grpc_socket,
        const grpc_sockaddr* addr,
        int* length) with gil:
    """Supplies sock_addr in add_socket_to_server."""
    cdef grpc_resolved_address c_addr
    socket = (<_AsyncioSocket>grpc_socket.impl)
    if socket is None:
        peer = ('0.0.0.0', 0)
    else:
        peer = socket.sockname()
    hostname = str_to_bytes(peer[0])
    grpc_string_to_sockaddr(&c_addr, hostname, peer[1])
    # TODO(https://github.com/grpc/grpc/issues/20684) Remove the memcpy
    string.memcpy(<void*>addr, <void*>c_addr.addr, c_addr.len)
    length[0] = c_addr.len
    return grpc_error_none()


cdef grpc_error* asyncio_socket_listen(grpc_custom_socket* grpc_socket) with gil:
    (<_AsyncioSocket>grpc_socket.impl).listen()
    return grpc_error_none()


def _asyncio_apply_socket_options(object s, so_reuse_port=False):
    # TODO(https://github.com/grpc/grpc/issues/20667)
    # Connects the so_reuse_port option to channel arguments
    s.setsockopt(native_socket.SOL_SOCKET, native_socket.SO_REUSEADDR, 1)
    s.setsockopt(native_socket.IPPROTO_TCP, native_socket.TCP_NODELAY, True)


cdef grpc_error* asyncio_socket_bind(
        grpc_custom_socket* grpc_socket,
        const grpc_sockaddr* addr,
        size_t len, int flags) with gil:
    host, port = sockaddr_to_tuple(addr, len)
    try:
        ip = ipaddress.ip_address(host)
        if isinstance(ip, ipaddress.IPv6Address):
            family = native_socket.AF_INET6
        else:
            family = native_socket.AF_INET

        socket = native_socket.socket(family=family)
        _asyncio_apply_socket_options(socket)
        socket.bind((host, port))
    except IOError as io_error:
        return socket_error("bind", str(io_error))
    else:
        aio_socket = _AsyncioSocket.create_with_py_socket(grpc_socket, socket)
        cpython.Py_INCREF(aio_socket)  # Py_DECREF in asyncio_socket_destroy
        grpc_socket.impl = <void*>aio_socket
        return grpc_error_none()


cdef void asyncio_socket_accept(
        grpc_custom_socket* grpc_socket,
        grpc_custom_socket* grpc_socket_client,
        grpc_custom_accept_callback accept_cb) with gil:
    (<_AsyncioSocket>grpc_socket.impl).accept(grpc_socket_client, accept_cb)


cdef grpc_error* asyncio_resolve(
        char* host,
        char* port,
        grpc_resolved_addresses** res) with gil:
    result = native_socket.getaddrinfo(host, port)
    res[0] = tuples_to_resolvaddr(result)


cdef void asyncio_resolve_async(
        grpc_custom_resolver* grpc_resolver,
        char* host,
        char* port) with gil:
    resolver = _AsyncioResolver.create(grpc_resolver)
    resolver.resolve(host, port)


cdef void asyncio_timer_start(grpc_custom_timer* grpc_timer) with gil:
    timer = _AsyncioTimer.create(grpc_timer, grpc_timer.timeout_ms / 1000.0)
    Py_INCREF(timer)
    grpc_timer.timer = <void*>timer


cdef void asyncio_timer_stop(grpc_custom_timer* grpc_timer) with gil:
    timer = <_AsyncioTimer>grpc_timer.timer
    timer.stop()
    Py_DECREF(timer)


cdef void asyncio_init_loop() with gil:
    pass


cdef void asyncio_destroy_loop() with gil:
    pass


cdef void asyncio_kick_loop() with gil:
    pass


cdef void asyncio_run_loop(size_t timeout_ms) with gil:
    pass


def install_asyncio_iomgr():
    asyncio_resolver_vtable.resolve = asyncio_resolve
    asyncio_resolver_vtable.resolve_async = asyncio_resolve_async

    asyncio_socket_vtable.init = asyncio_socket_init
    asyncio_socket_vtable.connect = asyncio_socket_connect
    asyncio_socket_vtable.destroy = asyncio_socket_destroy
    asyncio_socket_vtable.shutdown = asyncio_socket_shutdown
    asyncio_socket_vtable.close = asyncio_socket_close
    asyncio_socket_vtable.write = asyncio_socket_write
    asyncio_socket_vtable.read = asyncio_socket_read
    asyncio_socket_vtable.getpeername = asyncio_socket_getpeername
    asyncio_socket_vtable.getsockname = asyncio_socket_getsockname
    asyncio_socket_vtable.bind = asyncio_socket_bind
    asyncio_socket_vtable.listen = asyncio_socket_listen
    asyncio_socket_vtable.accept = asyncio_socket_accept

    asyncio_timer_vtable.start = asyncio_timer_start
    asyncio_timer_vtable.stop = asyncio_timer_stop

    asyncio_pollset_vtable.init = asyncio_init_loop
    asyncio_pollset_vtable.poll = asyncio_run_loop
    asyncio_pollset_vtable.kick = asyncio_kick_loop
    asyncio_pollset_vtable.shutdown = asyncio_destroy_loop

    grpc_custom_iomgr_init(
        &asyncio_socket_vtable,
        &asyncio_resolver_vtable,
        &asyncio_timer_vtable,
        &asyncio_pollset_vtable
    )
