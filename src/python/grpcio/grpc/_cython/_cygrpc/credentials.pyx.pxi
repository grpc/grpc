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

import grpc
import threading


cdef class CallCredentials:

  cdef grpc_call_credentials *c(self):
    raise NotImplementedError()


cdef int _get_metadata(
    void *state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void *user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t *num_creds_md, grpc_status_code *status,
    const char **error_details) with gil:
  cdef size_t metadata_count
  cdef grpc_metadata *c_metadata
  def callback(metadata, grpc_status_code status, bytes error_details):
    if status is StatusCode.ok:
      _store_c_metadata(metadata, &c_metadata, &metadata_count)
      cb(user_data, c_metadata, metadata_count, status, NULL)
      _release_c_metadata(c_metadata, metadata_count)
    else:
      cb(user_data, NULL, 0, status, error_details)
  args = context.service_url, context.method_name, callback,
  threading.Thread(target=<object>state, args=args).start()
  return 0  # Asynchronous return


cdef void _destroy(void *state) with gil:
  cpython.Py_DECREF(<object>state)


cdef class MetadataPluginCallCredentials(CallCredentials):

  def __cinit__(self, metadata_plugin, name):
    self._metadata_plugin = metadata_plugin
    self._name = name

  cdef grpc_call_credentials *c(self):
    cdef grpc_metadata_credentials_plugin c_metadata_plugin
    c_metadata_plugin.get_metadata = _get_metadata
    c_metadata_plugin.destroy = _destroy
    c_metadata_plugin.state = <void *>self._metadata_plugin
    c_metadata_plugin.type = self._name
    cpython.Py_INCREF(self._metadata_plugin)
    return grpc_metadata_credentials_create_from_plugin(c_metadata_plugin, NULL)


cdef grpc_call_credentials *_composition(call_credentialses):
  call_credentials_iterator = iter(call_credentialses)
  cdef CallCredentials composition = next(call_credentials_iterator)
  cdef grpc_call_credentials *c_composition = composition.c()
  cdef CallCredentials additional_call_credentials
  cdef grpc_call_credentials *c_additional_call_credentials
  cdef grpc_call_credentials *c_next_composition
  for additional_call_credentials in call_credentials_iterator:
    c_additional_call_credentials = additional_call_credentials.c()
    c_next_composition = grpc_composite_call_credentials_create(
        c_composition, c_additional_call_credentials, NULL)
    grpc_call_credentials_release(c_composition)
    grpc_call_credentials_release(c_additional_call_credentials)
    c_composition = c_next_composition
  return c_composition


cdef class CompositeCallCredentials(CallCredentials):

  def __cinit__(self, call_credentialses):
    self._call_credentialses = call_credentialses

  cdef grpc_call_credentials *c(self):
    return _composition(self._call_credentialses)


cdef class ChannelCredentials:

  cdef grpc_channel_credentials *c(self):
    raise NotImplementedError()


cdef class SSLChannelCredentials(ChannelCredentials):

  def __cinit__(self, pem_root_certificates, private_key, certificate_chain):
    self._pem_root_certificates = pem_root_certificates
    self._private_key = private_key
    self._certificate_chain = certificate_chain

  cdef grpc_channel_credentials *c(self):
    cdef const char *c_pem_root_certificates
    cdef grpc_ssl_pem_key_cert_pair c_pem_key_certificate_pair
    if self._pem_root_certificates is None:
      c_pem_root_certificates = NULL
    else:
      c_pem_root_certificates = self._pem_root_certificates
    if self._private_key is None and self._certificate_chain is None:
      return grpc_ssl_credentials_create(
          c_pem_root_certificates, NULL, NULL)
    else:
      c_pem_key_certificate_pair.private_key = self._private_key
      c_pem_key_certificate_pair.certificate_chain = self._certificate_chain
      return grpc_ssl_credentials_create(
          c_pem_root_certificates, &c_pem_key_certificate_pair, NULL)


cdef class CompositeChannelCredentials(ChannelCredentials):

  def __cinit__(self, call_credentialses, channel_credentials):
    self._call_credentialses = call_credentialses
    self._channel_credentials = channel_credentials

  cdef grpc_channel_credentials *c(self):
    cdef grpc_channel_credentials *c_channel_credentials
    c_channel_credentials = self._channel_credentials.c()
    cdef grpc_call_credentials *c_call_credentials_composition = _composition(
        self._call_credentialses)
    cdef grpc_channel_credentials *composition
    c_composition = grpc_composite_channel_credentials_create(
        c_channel_credentials, c_call_credentials_composition, NULL)
    grpc_channel_credentials_release(c_channel_credentials)
    grpc_call_credentials_release(c_call_credentials_composition)
    return c_composition


cdef class ServerCertificateConfig:

  def __cinit__(self):
    grpc_init()
    self.c_cert_config = NULL
    self.c_pem_root_certs = NULL
    self.c_ssl_pem_key_cert_pairs = NULL
    self.references = []

  def __dealloc__(self):
    grpc_ssl_server_certificate_config_destroy(self.c_cert_config)
    gpr_free(self.c_ssl_pem_key_cert_pairs)
    grpc_shutdown()


cdef class ServerCredentials:

  def __cinit__(self):
    grpc_init()
    self.c_credentials = NULL
    self.references = []
    self.initial_cert_config = None
    self.cert_config_fetcher = None
    self.initial_cert_config_fetched = False

  def __dealloc__(self):
    if self.c_credentials != NULL:
      grpc_server_credentials_release(self.c_credentials)
    grpc_shutdown()

cdef const char* _get_c_pem_root_certs(pem_root_certs):
  if pem_root_certs is None:
    return NULL
  else:
    return pem_root_certs

cdef grpc_ssl_pem_key_cert_pair* _create_c_ssl_pem_key_cert_pairs(pem_key_cert_pairs):
  # return a malloc'ed grpc_ssl_pem_key_cert_pair from a _list_ of SslPemKeyCertPair
  for pair in pem_key_cert_pairs:
    if not isinstance(pair, SslPemKeyCertPair):
      raise TypeError("expected pem_key_cert_pairs to be sequence of "
                      "SslPemKeyCertPair")
  cdef size_t c_ssl_pem_key_cert_pairs_count = len(pem_key_cert_pairs)
  cdef grpc_ssl_pem_key_cert_pair* c_ssl_pem_key_cert_pairs = NULL
  with nogil:
    c_ssl_pem_key_cert_pairs = (
      <grpc_ssl_pem_key_cert_pair *>gpr_malloc(
        sizeof(grpc_ssl_pem_key_cert_pair) * c_ssl_pem_key_cert_pairs_count))
  for i in range(c_ssl_pem_key_cert_pairs_count):
    c_ssl_pem_key_cert_pairs[i] = (
      (<SslPemKeyCertPair>pem_key_cert_pairs[i]).c_pair)
  return c_ssl_pem_key_cert_pairs

def server_credentials_ssl(pem_root_certs, pem_key_cert_pairs,
                           bint force_client_auth):
  pem_root_certs = str_to_bytes(pem_root_certs)
  pem_key_cert_pairs = list(pem_key_cert_pairs)
  cdef ServerCredentials credentials = ServerCredentials()
  credentials.references.append(pem_root_certs)
  credentials.references.append(pem_key_cert_pairs)
  cdef const char * c_pem_root_certs = _get_c_pem_root_certs(pem_root_certs)
  credentials.c_ssl_pem_key_cert_pairs_count = len(pem_key_cert_pairs)
  credentials.c_ssl_pem_key_cert_pairs = _create_c_ssl_pem_key_cert_pairs(pem_key_cert_pairs)
  cdef grpc_ssl_server_certificate_config *c_cert_config = NULL
  c_cert_config = grpc_ssl_server_certificate_config_create(
    c_pem_root_certs, credentials.c_ssl_pem_key_cert_pairs,
    credentials.c_ssl_pem_key_cert_pairs_count)
  cdef grpc_ssl_server_credentials_options* c_options = NULL
  # C-core assumes ownership of c_cert_config
  c_options = grpc_ssl_server_credentials_create_options_using_config(
    GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
    if force_client_auth else
    GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
    c_cert_config)
  # C-core assumes ownership of c_options
  credentials.c_credentials = grpc_ssl_server_credentials_create_with_options(c_options)
  return credentials

def server_certificate_config_ssl(pem_root_certs, pem_key_cert_pairs):
  pem_root_certs = str_to_bytes(pem_root_certs)
  pem_key_cert_pairs = list(pem_key_cert_pairs)
  cdef ServerCertificateConfig cert_config = ServerCertificateConfig()
  cert_config.references.append(pem_root_certs)
  cert_config.references.append(pem_key_cert_pairs)
  cert_config.c_pem_root_certs = _get_c_pem_root_certs(pem_root_certs)
  cert_config.c_ssl_pem_key_cert_pairs_count = len(pem_key_cert_pairs)
  cert_config.c_ssl_pem_key_cert_pairs = _create_c_ssl_pem_key_cert_pairs(pem_key_cert_pairs)
  cert_config.c_cert_config = grpc_ssl_server_certificate_config_create(
    cert_config.c_pem_root_certs, cert_config.c_ssl_pem_key_cert_pairs,
    cert_config.c_ssl_pem_key_cert_pairs_count)
  return cert_config

def server_credentials_ssl_dynamic_cert_config(initial_cert_config,
                                               cert_config_fetcher,
                                               bint force_client_auth):
  if not isinstance(initial_cert_config, grpc.ServerCertificateConfiguration):
    raise TypeError(
        'initial_cert_config must be a grpc.ServerCertificateConfiguration')
  if not callable(cert_config_fetcher):
    raise TypeError('cert_config_fetcher must be callable')
  cdef ServerCredentials credentials = ServerCredentials()
  credentials.initial_cert_config = initial_cert_config
  credentials.cert_config_fetcher = cert_config_fetcher
  cdef grpc_ssl_server_credentials_options* c_options = NULL
  c_options = grpc_ssl_server_credentials_create_options_using_config_fetcher(
    GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
    if force_client_auth else
    GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
    _server_cert_config_fetcher_wrapper,
    <void*>credentials)
  # C-core assumes ownership of c_options
  credentials.c_credentials = grpc_ssl_server_credentials_create_with_options(c_options)
  return credentials
