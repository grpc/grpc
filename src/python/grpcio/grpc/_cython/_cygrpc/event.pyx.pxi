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


cdef class ConnectivityEvent:

  def __cinit__(
      self, grpc_completion_type completion_type, bint success, object tag):
    self.completion_type = completion_type
    self.success = success
    self.tag = tag


cdef class RequestCallEvent:

  def __cinit__(
      self, grpc_completion_type completion_type, bint success, object tag,
      Call call, CallDetails call_details, tuple invocation_metadata):
    self.completion_type = completion_type
    self.success = success
    self.tag = tag
    self.call = call
    self.call_details = call_details
    self.invocation_metadata = invocation_metadata


cdef class BatchOperationEvent:

  def __cinit__(
      self, grpc_completion_type completion_type, bint success, object tag,
      object batch_operations):
    self.completion_type = completion_type
    self.success = success
    self.tag = tag
    self.batch_operations = batch_operations


cdef class ServerShutdownEvent:

  def __cinit__(
      self, grpc_completion_type completion_type, bint success, object tag):
    self.completion_type = completion_type
    self.success = success
    self.tag = tag
