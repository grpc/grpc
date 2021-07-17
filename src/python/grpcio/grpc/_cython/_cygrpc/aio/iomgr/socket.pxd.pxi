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


cdef class _AsyncioSocket:
    cdef:
        # Common attributes
        grpc_custom_socket * _grpc_socket
        grpc_custom_read_callback _grpc_read_cb
        grpc_custom_write_callback _grpc_write_cb
        object _reader
        object _writer
        object _task_read
        object _task_write
        object _task_connect
        object _task_listen
        char * _read_buffer
        # Caches the picked event loop, so we can avoid the 30ns overhead each
        # time we need access to the event loop.
        object _loop
        # TODO(lidiz) Drop after 3.6 deprecation. Python 3.7 introduces methods
        # like `is_closing()` to help graceful shutdown.
        bint _closed

        # Client-side attributes
        grpc_custom_connect_callback _grpc_connect_cb
        
        # Server-side attributes
        grpc_custom_accept_callback _grpc_accept_cb
        grpc_custom_socket * _grpc_client_socket
        object _server
        object _py_socket
        object _peername

    @staticmethod
    cdef _AsyncioSocket create(
            grpc_custom_socket * grpc_socket,
            object reader,
            object writer)
    @staticmethod
    cdef _AsyncioSocket create_with_py_socket(grpc_custom_socket * grpc_socket, object py_socket)

    cdef void connect(self, object host, object port, grpc_custom_connect_callback grpc_connect_cb)
    cdef void write(self, grpc_slice_buffer * g_slice_buffer, grpc_custom_write_callback grpc_write_cb)
    cdef void read(self, char * buffer_, size_t length, grpc_custom_read_callback grpc_read_cb)
    cdef bint is_connected(self)
    cdef void close(self)

    cdef accept(self, grpc_custom_socket* grpc_socket_client, grpc_custom_accept_callback grpc_accept_cb)
    cdef listen(self)
    cdef tuple peername(self)
    cdef tuple sockname(self)
