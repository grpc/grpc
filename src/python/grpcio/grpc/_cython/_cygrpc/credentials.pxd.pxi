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


cdef class CallCredentials:

  cdef grpc_call_credentials *c(self) except *

  # TODO(https://github.com/grpc/grpc/issues/12531): remove.
  cdef grpc_call_credentials *c_credentials


cdef int _get_metadata(
    void *state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void *user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t *num_creds_md, grpc_status_code *status,
    const char **error_details) except -1 with gil

cdef void _destroy(void *state) noexcept nogil


cdef class MetadataPluginCallCredentials(CallCredentials):

  cdef readonly object _metadata_plugin
  cdef readonly bytes _name

  cdef grpc_call_credentials *c(self) except *


cdef grpc_call_credentials *_composition(call_credentialses)


cdef class CompositeCallCredentials(CallCredentials):

  cdef readonly tuple _call_credentialses

  cdef grpc_call_credentials *c(self) except *


cdef class ChannelCredentials:

  cdef grpc_channel_credentials *c(self) except *


cdef class SSLSessionCacheLRU:

  cdef grpc_ssl_session_cache *_cache


cdef class SSLChannelCredentials(ChannelCredentials):

  cdef readonly object _pem_root_certificates
  cdef readonly object _private_key
  cdef readonly object _certificate_chain

  cdef grpc_channel_credentials *c(self) except *


cdef class CompositeChannelCredentials(ChannelCredentials):

  cdef readonly tuple _call_credentialses
  cdef readonly ChannelCredentials _channel_credentials

  cdef grpc_channel_credentials *c(self) except *


cdef class XDSChannelCredentials(ChannelCredentials):

  cdef readonly ChannelCredentials _fallback_credentials

  cdef grpc_channel_credentials *c(self) except *


cdef class ServerCertificateConfig:

  cdef grpc_ssl_server_certificate_config *c_cert_config
  cdef const char *c_pem_root_certs
  cdef grpc_ssl_pem_key_cert_pair *c_ssl_pem_key_cert_pairs
  cdef size_t c_ssl_pem_key_cert_pairs_count
  cdef list references


cdef class ServerCredentials:

  cdef grpc_server_credentials *c_credentials
  cdef grpc_ssl_pem_key_cert_pair *c_ssl_pem_key_cert_pairs
  cdef size_t c_ssl_pem_key_cert_pairs_count
  cdef list references
  # the cert config related state is used only if these credentials are
  # created with cert config/fetcher
  cdef object initial_cert_config
  cdef object cert_config_fetcher
  # whether C-core has asked for the initial_cert_config
  cdef bint initial_cert_config_fetched


cdef class LocalChannelCredentials(ChannelCredentials):

  cdef grpc_local_connect_type _local_connect_type


cdef class ALTSChannelCredentials(ChannelCredentials):
  cdef grpc_alts_credentials_options *c_options

  cdef grpc_channel_credentials *c(self) except *
