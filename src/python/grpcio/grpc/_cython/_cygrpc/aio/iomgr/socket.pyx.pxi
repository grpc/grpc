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

import socket as native_socket

from libc cimport string

cdef int _ASYNCIO_STREAM_DEFAULT_SOCKET_BACKLOG = 100


# TODO(https://github.com/grpc/grpc/issues/21348) Better flow control needed.
cdef class _AsyncioSocket:
    def __cinit__(self):
        self._grpc_socket = NULL
        self._grpc_connect_cb = NULL
        self._grpc_read_cb = NULL
        self._grpc_write_cb = NULL
        self._reader = None
        self._writer = None
        self._task_connect = None
        self._task_read = None
        self._task_write = None
        self._task_listen = None
        self._read_buffer = NULL
        self._server = None
        self._py_socket = None
        self._peername = None
        self._closed = False
        self._loop = get_working_loop()

    @staticmethod
    cdef _AsyncioSocket create(grpc_custom_socket * grpc_socket,
                               object reader,
                               object writer):
        socket = _AsyncioSocket()
        socket._grpc_socket = grpc_socket
        socket._reader = reader
        socket._writer = writer
        if writer is not None:
            socket._peername = writer.get_extra_info('peername')
        return socket

    @staticmethod
    cdef _AsyncioSocket create_with_py_socket(grpc_custom_socket * grpc_socket, object py_socket):
        socket = _AsyncioSocket()
        socket._grpc_socket = grpc_socket
        socket._py_socket = py_socket
        return socket

    def __repr__(self):
        class_name = self.__class__.__name__ 
        id_ = id(self)
        connected = self.is_connected()
        return f"<{class_name} {id_} connected={connected}>"

    async def _async_connect(self, object host, object port,):
        self._task_connect = None
        try:
            self._reader, self._writer = await asyncio.open_connection(host, port)
        except Exception as e:
            self._grpc_connect_cb(
                <grpc_custom_socket*>self._grpc_socket,
                grpc_socket_error("Socket connect failed: {}: {}".format(type(e), str(e)).encode())
            )
        else:
            # gRPC default posix implementation disables nagle
            # algorithm.
            sock = self._writer.transport.get_extra_info('socket')
            sock.setsockopt(native_socket.IPPROTO_TCP, native_socket.TCP_NODELAY, True)

            self._grpc_connect_cb(
                <grpc_custom_socket*>self._grpc_socket,
                <grpc_error_handle>0
            )

    cdef void connect(self,
                      object host,
                      object port,
                      grpc_custom_connect_callback grpc_connect_cb):
        assert not self._reader
        assert not self._task_connect

        self._task_connect = self._loop.create_task(
            self._async_connect(host, port)
        )
        self._grpc_connect_cb = grpc_connect_cb

    async def _async_read(self, size_t length):
        self._task_read = None
        try:
            inbound_buffer = await self._reader.read(n=length)
        except ConnectionError as e:
            self._grpc_read_cb(
                <grpc_custom_socket*>self._grpc_socket,
                -1,
                grpc_socket_error("Read failed: {}".format(e).encode())
            )
        else:
            string.memcpy(
                <void*>self._read_buffer,
                <char*>inbound_buffer,
                len(inbound_buffer)
            )
            self._grpc_read_cb(
                <grpc_custom_socket*>self._grpc_socket,
                len(inbound_buffer),
                <grpc_error_handle>0
            )

    cdef void read(self, char * buffer_, size_t length, grpc_custom_read_callback grpc_read_cb):
        assert not self._task_read

        self._grpc_read_cb = grpc_read_cb
        self._read_buffer = buffer_
        self._task_read = self._loop.create_task(self._async_read(length))

    async def _async_write(self, bytearray outbound_buffer):
        self._writer.write(outbound_buffer)
        self._task_write = None
        try:
            await self._writer.drain()
            self._grpc_write_cb(
                <grpc_custom_socket*>self._grpc_socket,
                <grpc_error_handle>0
            )
        except ConnectionError as connection_error:
            self._grpc_write_cb(
                <grpc_custom_socket*>self._grpc_socket,
                grpc_socket_error("Socket write failed: {}".format(connection_error).encode()),
            )

    cdef void write(self, grpc_slice_buffer * g_slice_buffer, grpc_custom_write_callback grpc_write_cb):
        """Performs write to network socket in AsyncIO.
        
        For each socket, Core guarantees there'll be only one ongoing write.
        When the write is finished, we need to call grpc_write_cb to notify
        Core that the work is done.
        """
        assert not self._task_write
        cdef char* start
        cdef bytearray outbound_buffer = bytearray()
        for i in range(g_slice_buffer.count):
            start = grpc_slice_buffer_start(g_slice_buffer, i)
            length = grpc_slice_buffer_length(g_slice_buffer, i)
            outbound_buffer.extend(<bytes>start[:length])

        self._grpc_write_cb = grpc_write_cb
        self._task_write = self._loop.create_task(self._async_write(outbound_buffer))

    cdef bint is_connected(self):
        return self._reader and not self._reader._transport.is_closing()

    cdef void close(self):
        if self._closed:
            return
        else:
            self._closed = True
        if self.is_connected():
            self._writer.close()
        if self._task_listen and not self._task_listen.done():
            self._task_listen.close()
        if self._server:
            self._server.close()
        # NOTE(lidiz) If the asyncio.Server is created from a Python socket,
        # the server.close() won't release the fd until the close() is called
        # for the Python socket.
        if self._py_socket:
            self._py_socket.close()

    def _new_connection_callback(self, object reader, object writer):
        # If the socket is closed, stop.
        if self._closed:
            return

        # Close the connection if server is not started yet.
        if self._grpc_accept_cb == NULL:
            writer.close()
            return

        client_socket = _AsyncioSocket.create(
            self._grpc_client_socket,
            reader,
            writer,
        )

        self._grpc_client_socket.impl = <void*>client_socket
        cpython.Py_INCREF(client_socket)  # Py_DECREF in asyncio_socket_destroy
        # Accept callback expects to be called with:
        # * grpc_custom_socket: A grpc custom socket for server
        # * grpc_custom_socket: A grpc custom socket for client (with new Socket instance)
        # * grpc_error: An error object
        self._grpc_accept_cb(self._grpc_socket, self._grpc_client_socket, grpc_error_none())

    cdef listen(self):
        self._py_socket.listen(_ASYNCIO_STREAM_DEFAULT_SOCKET_BACKLOG)
        async def create_asyncio_server():
            self._server = await asyncio.start_server(
                self._new_connection_callback,
                sock=self._py_socket,
            )

        self._task_listen = self._loop.create_task(create_asyncio_server())

    cdef accept(self,
                grpc_custom_socket* grpc_socket_client,
                grpc_custom_accept_callback grpc_accept_cb):
        self._grpc_client_socket = grpc_socket_client
        self._grpc_accept_cb = grpc_accept_cb

    cdef tuple peername(self):
        return self._peername

    cdef tuple sockname(self):
        return self._py_socket.getsockname()
