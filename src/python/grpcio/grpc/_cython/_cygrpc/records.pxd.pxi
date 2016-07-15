# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


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
  cdef readonly object key, value


cdef class ChannelArgs:

  cdef grpc_channel_args c_args
  cdef list args


cdef class Metadatum:

  cdef grpc_metadata c_metadata
  cdef object _key, _value


cdef class Metadata:

  cdef grpc_metadata_array c_metadata_array
  cdef object metadata


cdef class Operation:

  cdef grpc_op c_op
  cdef ByteBuffer _received_message
  cdef Metadata _received_metadata
  cdef grpc_status_code _received_status_code
  cdef char *_received_status_details
  cdef size_t _received_status_details_capacity
  cdef int _received_cancelled
  cdef readonly bint is_valid
  cdef object references


cdef class Operations:

  cdef grpc_op *c_ops
  cdef size_t c_nops
  cdef list operations


cdef class CompressionOptions:

  cdef grpc_compression_options c_options
