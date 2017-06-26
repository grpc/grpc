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


cdef bytes _slice_bytes(grpc_slice slice)
cdef grpc_slice _copy_slice(grpc_slice slice) nogil
cdef grpc_slice _slice_from_bytes(bytes value) nogil


cdef class Timespec:

  cdef gpr_timespec c_time


cdef class CallDetails:

  cdef grpc_call_details c_details


cdef class OperationTag:

  cdef object user_tag
  cdef list references
  # This allows CompletionQueue to notify the Python Server object that the
  # underlying GRPC core server has shutdown
  cdef Server shutting_down_server
  cdef Call operation_call
  cdef CallDetails request_call_details
  cdef Metadata request_metadata
  cdef Operations batch_operations
  cdef bint is_new_request


cdef class Event:

  cdef readonly grpc_completion_type type
  cdef readonly bint success
  cdef readonly object tag

  # For Server.request_call
  cdef readonly bint is_new_request
  cdef readonly CallDetails request_call_details
  cdef readonly Metadata request_metadata

  # For server calls
  cdef readonly Call operation_call

  # For Call.start_batch
  cdef readonly Operations batch_operations


cdef class ByteBuffer:

  cdef grpc_byte_buffer *c_byte_buffer


cdef class SslPemKeyCertPair:

  cdef grpc_ssl_pem_key_cert_pair c_pair
  cdef readonly object private_key, certificate_chain


cdef class ChannelArg:

  cdef grpc_arg c_arg
  cdef grpc_arg_pointer_vtable ptr_vtable
  cdef readonly object key, value


cdef class ChannelArgs:

  cdef grpc_channel_args c_args
  cdef list args


cdef class Metadatum:

  cdef grpc_metadata c_metadata
  cdef void _copy_metadatum(self, grpc_metadata *destination) nogil


cdef class Metadata:

  cdef grpc_metadata_array c_metadata_array
  cdef void _claim_slice_ownership(self)


cdef class Operation:

  cdef grpc_op c_op
  cdef ByteBuffer _received_message
  cdef Metadata _received_metadata
  cdef grpc_status_code _received_status_code
  cdef grpc_slice _status_details
  cdef int _received_cancelled
  cdef readonly bint is_valid
  cdef object references


cdef class Operations:

  cdef grpc_op *c_ops
  cdef size_t c_nops
  cdef list operations


cdef class CompressionOptions:

  cdef grpc_compression_options c_options
