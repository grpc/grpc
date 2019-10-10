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
        grpc_custom_socket * _grpc_socket
        grpc_custom_connect_callback _grpc_connect_cb
        grpc_custom_read_callback _grpc_read_cb
        object _reader
        object _writer
        object _task_read
        object _task_connect
        char * _read_buffer

    @staticmethod
    cdef _AsyncioSocket create(grpc_custom_socket * grpc_socket)

    cdef void connect(self, object host, object port, grpc_custom_connect_callback grpc_connect_cb)
    cdef void write(self, grpc_slice_buffer * g_slice_buffer, grpc_custom_write_callback grpc_write_cb)
    cdef void read(self, char * buffer_, size_t length, grpc_custom_read_callback grpc_read_cb)
    cdef bint is_connected(self)
    cdef void close(self)
