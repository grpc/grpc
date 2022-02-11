# Copyright 2017 gRPC authors.
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

cdef class TimerWrapper:

  cdef grpc_custom_timer *c_timer
  cdef object timer
  cdef object event

cdef class SocketWrapper:
  cdef object sockopts
  cdef object socket
  cdef object closed
  cdef grpc_custom_socket *c_socket
  cdef char* c_buffer
  cdef size_t len
  cdef grpc_custom_socket *accepting_socket

  cdef grpc_custom_connect_callback connect_cb
  cdef grpc_custom_write_callback write_cb
  cdef grpc_custom_read_callback read_cb
  cdef grpc_custom_accept_callback accept_cb
  cdef grpc_custom_close_callback close_cb


cdef class ResolveWrapper:
  cdef grpc_custom_resolver *c_resolver
  cdef const char* c_host
  cdef const char* c_port
