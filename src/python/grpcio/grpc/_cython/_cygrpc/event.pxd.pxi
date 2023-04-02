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

cdef class BaseEvent:
  pass

cdef class ConnectivityEvent(BaseEvent):

  cdef readonly grpc_completion_type completion_type
  cdef readonly bint success
  cdef readonly object tag


cdef class RequestCallEvent(BaseEvent):

  cdef readonly grpc_completion_type completion_type
  cdef readonly bint success
  cdef readonly object tag
  cdef readonly Call call
  cdef readonly CallDetails call_details
  cdef readonly tuple invocation_metadata


cdef class BatchOperationEvent(BaseEvent):

  cdef readonly grpc_completion_type completion_type
  cdef readonly bint success
  cdef readonly object tag
  cdef readonly object batch_operations


cdef class ServerShutdownEvent(BaseEvent):

  cdef readonly grpc_completion_type completion_type
  cdef readonly bint success
  cdef readonly object tag
