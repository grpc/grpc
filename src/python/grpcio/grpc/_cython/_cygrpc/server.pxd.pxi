# Copyright 2015 gRPC authors.
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

cdef class Server:

  cdef grpc_server *c_server

  cdef bint is_started  # start has been called
  cdef bint is_shutting_down  # shutdown has been called
  cdef bint is_shutdown  # notification of complete shutdown received
  # used at dealloc when user forgets to shutdown
  cdef CompletionQueue backup_shutdown_queue
  # TODO(https://github.com/grpc/grpc/issues/15662): Elide this.
  cdef list references
  cdef list registered_completion_queues
  cdef dict registered_methods # Mapping[bytes, RegisteredMethod]

  cdef _c_shutdown(self, CompletionQueue queue, tag)
  cdef _c_request_unregistered_call(self,
       _RequestCallTag request_call_tag,
       CompletionQueue call_queue,
       CompletionQueue server_queue)
  cdef _c_request_registered_call(self,
       _RequestCallTag request_call_tag,
       CompletionQueue call_queue,
       CompletionQueue server_queue,
       bytes method)
  cdef notify_shutdown_complete(self)

cdef class RegisteredMethod:

  cdef void *c_registered_method
  cdef bytes method
