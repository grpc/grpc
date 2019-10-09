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

import socket

from libc cimport string

cdef class _AsyncioSocket:
    def __cinit__(self):
        self._grpc_socket = NULL
        self._grpc_connect_cb = NULL
        self._grpc_read_cb = NULL
        self._reader = None
        self._writer = None
        self._task_connect = None
        self._task_read = None
        self._read_buffer = NULL

    @staticmethod
    cdef _AsyncioSocket create(grpc_custom_socket * grpc_socket):
        socket = _AsyncioSocket()
        socket._grpc_socket = grpc_socket
        return socket

    def __repr__(self):
        class_name = self.__class__.__name__ 
        id_ = id(self)
        connected = self.is_connected()
        return f"<{class_name} {id_} connected={connected}>"

    def _connect_cb(self, future):
        error = False
        try:
            self._reader, self._writer = future.result()
        except Exception as e:
            error = True
        finally:
            self._task_connect = None

        if not error:
            # gRPC default posix implementation disables nagle
            # algorithm.
            sock = self._writer.transport.get_extra_info('socket')
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, True)

            self._grpc_connect_cb(
                <grpc_custom_socket*>self._grpc_socket,
                <grpc_error*>0
            )
        else:
            self._grpc_connect_cb(
                <grpc_custom_socket*>self._grpc_socket,
                grpc_socket_error("connect {}".format(str(e)).encode())
            )

    def _read_cb(self, future):
        error = False
        try:
            buffer_ = future.result()
        except Exception as e:
            error = True
            error_msg = str(e)
        finally:
            self._task_read = None

        if not error:
            string.memcpy(
                <void*>self._read_buffer,
                <char*>buffer_,
                len(buffer_)
            )
            self._grpc_read_cb(
                <grpc_custom_socket*>self._grpc_socket,
                len(buffer_),
                <grpc_error*>0
            )
        else:
            self._grpc_read_cb(
                <grpc_custom_socket*>self._grpc_socket,
                -1,
                grpc_socket_error("read {}".format(error_msg).encode())
            )

    cdef void connect(self, object host, object port, grpc_custom_connect_callback grpc_connect_cb):
        assert not self._task_connect

        self._task_connect = asyncio.ensure_future(
            asyncio.open_connection(host, port)
        )
        self._grpc_connect_cb = grpc_connect_cb
        self._task_connect.add_done_callback(self._connect_cb)

    cdef void read(self, char * buffer_, size_t length, grpc_custom_read_callback grpc_read_cb):
        assert not self._task_read

        self._task_read = asyncio.ensure_future(
            self._reader.read(n=length)
        )
        self._grpc_read_cb = grpc_read_cb
        self._task_read.add_done_callback(self._read_cb)
        self._read_buffer = buffer_
 
    cdef void write(self, grpc_slice_buffer * g_slice_buffer, grpc_custom_write_callback grpc_write_cb):
        cdef char* start
        buffer_ = bytearray()
        for i in range(g_slice_buffer.count):
            start = grpc_slice_buffer_start(g_slice_buffer, i)
            length = grpc_slice_buffer_length(g_slice_buffer, i)
            buffer_.extend(<bytes>start[:length])

        self._writer.write(buffer_)

        grpc_write_cb(
            <grpc_custom_socket*>self._grpc_socket,
            <grpc_error*>0
        )

    cdef bint is_connected(self):
        return self._reader and not self._reader._transport.is_closing()

    cdef void close(self):
        if self.is_connected():
            self._writer.close()
