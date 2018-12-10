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


cdef class Operation:

  cdef void c(self) except *
  cdef void un_c(self) except *

  # TODO(https://github.com/grpc/grpc/issues/7950): Eliminate this!
  cdef grpc_op c_op


cdef class SendInitialMetadataOperation(Operation):

  cdef readonly object _initial_metadata;
  cdef readonly int _flags
  cdef grpc_metadata *_c_initial_metadata
  cdef size_t _c_initial_metadata_count

  cdef void c(self) except *
  cdef void un_c(self) except *


cdef class SendMessageOperation(Operation):

  cdef readonly bytes _message
  cdef readonly int _flags
  cdef grpc_byte_buffer *_c_message_byte_buffer

  cdef void c(self) except *
  cdef void un_c(self) except *


cdef class SendCloseFromClientOperation(Operation):

  cdef readonly int _flags

  cdef void c(self) except *
  cdef void un_c(self) except *


cdef class SendStatusFromServerOperation(Operation):

  cdef readonly object _trailing_metadata
  cdef readonly object _code
  cdef readonly object _details
  cdef readonly int _flags
  cdef grpc_metadata *_c_trailing_metadata
  cdef size_t _c_trailing_metadata_count
  cdef grpc_slice _c_details

  cdef void c(self) except *
  cdef void un_c(self) except *


cdef class ReceiveInitialMetadataOperation(Operation):

  cdef readonly int _flags
  cdef tuple _initial_metadata
  cdef grpc_metadata_array _c_initial_metadata

  cdef void c(self) except *
  cdef void un_c(self) except *


cdef class ReceiveMessageOperation(Operation):

  cdef readonly int _flags
  cdef grpc_byte_buffer *_c_message_byte_buffer
  cdef bytes _message

  cdef void c(self) except *
  cdef void un_c(self) except *


cdef class ReceiveStatusOnClientOperation(Operation):

  cdef readonly int _flags
  cdef grpc_metadata_array _c_trailing_metadata
  cdef grpc_status_code _c_code
  cdef grpc_slice _c_details
  cdef const char* _c_error_string
  cdef tuple _trailing_metadata
  cdef object _code
  cdef str _details
  cdef str _error_string

  cdef void c(self) except *
  cdef void un_c(self) except *


cdef class ReceiveCloseOnServerOperation(Operation):

  cdef readonly int _flags
  cdef object _cancelled
  cdef int _c_cancelled

  cdef void c(self) except *
  cdef void un_c(self) except *
