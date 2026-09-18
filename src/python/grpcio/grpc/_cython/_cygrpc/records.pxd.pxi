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
cdef grpc_slice _copy_slice(grpc_slice slice) noexcept nogil
cdef grpc_slice _slice_from_bytes(bytes value) noexcept nogil


cdef class CallDetails:

  cdef grpc_call_details c_details


cdef class SslPemKeyCertPair:

  cdef grpc_ssl_pem_key_cert_pair c_pair
  cdef readonly object private_key, certificate_chain


cdef class CompressionOptions:

  cdef grpc_compression_options c_options
