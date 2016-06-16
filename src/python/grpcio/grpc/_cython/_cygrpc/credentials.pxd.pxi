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

cimport cpython


cdef class ChannelCredentials:

  cdef grpc_channel_credentials *c_credentials
  cdef grpc_ssl_pem_key_cert_pair c_ssl_pem_key_cert_pair
  cdef list references


cdef class CallCredentials:

  cdef grpc_call_credentials *c_credentials
  cdef list references


cdef class ServerCredentials:

  cdef grpc_server_credentials *c_credentials
  cdef grpc_ssl_pem_key_cert_pair *c_ssl_pem_key_cert_pairs
  cdef size_t c_ssl_pem_key_cert_pairs_count
  cdef list references


cdef class CredentialsMetadataPlugin:

  cdef object plugin_callback
  cdef bytes plugin_name

  cdef grpc_metadata_credentials_plugin make_c_plugin(self)


cdef class AuthMetadataContext:

  cdef grpc_auth_metadata_context context


cdef void plugin_get_metadata(
    void *state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void *user_data) with gil

cdef void plugin_destroy_c_plugin_state(void *state) with gil
