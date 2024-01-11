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


cdef class _Tag:

  cdef BaseEvent event(self, grpc_event c_event)


cdef class _ConnectivityTag(_Tag):

  cdef readonly object _user_tag

  cdef ConnectivityEvent event(self, grpc_event c_event)


cdef class _RequestCallTag(_Tag):

  cdef readonly object _user_tag
  cdef Call call
  cdef CallDetails call_details
  cdef grpc_metadata_array c_invocation_metadata

  cdef void prepare(self) except *
  cdef RequestCallEvent event(self, grpc_event c_event)


cdef class _BatchOperationTag(_Tag):

  cdef object _user_tag
  cdef readonly object _operations
  cdef readonly object _retained_call
  cdef grpc_op *c_ops
  cdef size_t c_nops

  cdef void prepare(self) except *
  cdef BatchOperationEvent event(self, grpc_event c_event)


cdef class _ServerShutdownTag(_Tag):

  cdef readonly object _user_tag
  # This allows CompletionQueue to notify the Python Server object that the
  # underlying GRPC core server has shutdown
  cdef readonly Server _shutting_down_server

  cdef ServerShutdownEvent event(self, grpc_event c_event)
