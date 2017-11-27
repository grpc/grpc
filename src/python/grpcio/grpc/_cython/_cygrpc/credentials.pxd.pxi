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

cimport cpython


cdef class ChannelCredentials:

  cdef grpc_channel_credentials *c_credentials
  cdef grpc_ssl_pem_key_cert_pair c_ssl_pem_key_cert_pair
  cdef list references


cdef class CallCredentials:

  cdef grpc_call_credentials *c_credentials
  cdef list references


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
  # the cert config related state is used only if this credentials is
  # created with cert config/fetcher
  cdef object initial_cert_config
  cdef object cert_config_fetcher
  # whether C-core has asked for the initial_cert_config
  cdef bint initial_cert_config_fetched


cdef class CredentialsMetadataPlugin:

  cdef object plugin_callback
  cdef bytes plugin_name


cdef grpc_metadata_credentials_plugin _c_plugin(CredentialsMetadataPlugin plugin)


cdef class AuthMetadataContext:

  cdef grpc_auth_metadata_context context


cdef int plugin_get_metadata(
    void *state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void *user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t *num_creds_md, grpc_status_code *status,
    const char **error_details) with gil

cdef void plugin_destroy_c_plugin_state(void *state) with gil
