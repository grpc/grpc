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
import traceback


cdef class ChannelCredentials:

  def __cinit__(self):
    grpc_init()
    self.c_credentials = NULL
    self.c_ssl_pem_key_cert_pair.private_key = NULL
    self.c_ssl_pem_key_cert_pair.certificate_chain = NULL
    self.references = []

  # The object *can* be invalid in Python if we fail to make the credentials
  # (and the core thus returns NULL credentials). Used primarily for debugging.
  @property
  def is_valid(self):
    return self.c_credentials != NULL

  def __dealloc__(self):
    if self.c_credentials != NULL:
      grpc_channel_credentials_release(self.c_credentials)
    grpc_shutdown()


cdef class CallCredentials:

  def __cinit__(self):
    grpc_init()
    self.c_credentials = NULL
    self.references = []

  # The object *can* be invalid in Python if we fail to make the credentials
  # (and the core thus returns NULL credentials). Used primarily for debugging.
  @property
  def is_valid(self):
    return self.c_credentials != NULL

  def __dealloc__(self):
    if self.c_credentials != NULL:
      grpc_call_credentials_release(self.c_credentials)
    grpc_shutdown()


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


cdef class CredentialsMetadataPlugin:

  def __cinit__(self, object plugin_callback, bytes name):
    """
    Args:
      plugin_callback (callable): Callback accepting a service URL (str/bytes)
        and callback object (accepting a MetadataArray,
        grpc_status_code, and a str/bytes error message). This argument
        when called should be non-blocking and eventually call the callback
        object with the appropriate status code/details and metadata (if
        successful).
      name (bytes): Plugin name.
    """
    grpc_init()
    if not callable(plugin_callback):
      raise ValueError('expected callable plugin_callback')
    self.plugin_callback = plugin_callback
    self.plugin_name = name

  def __dealloc__(self):
    grpc_shutdown()


cdef grpc_metadata_credentials_plugin _c_plugin(CredentialsMetadataPlugin plugin):
  cdef grpc_metadata_credentials_plugin c_plugin
  c_plugin.get_metadata = plugin_get_metadata
  c_plugin.destroy = plugin_destroy_c_plugin_state
  c_plugin.state = <void *>plugin
  c_plugin.type = plugin.plugin_name
  cpython.Py_INCREF(plugin)
  return c_plugin


cdef class AuthMetadataContext:

  def __cinit__(self):
    grpc_init()
    self.context.service_url = NULL
    self.context.method_name = NULL

  @property
  def service_url(self):
    return self.context.service_url

  @property
  def method_name(self):
    return self.context.method_name

  def __dealloc__(self):
    grpc_shutdown()


cdef int plugin_get_metadata(
    void *state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void *user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t *num_creds_md, grpc_status_code *status,
    const char **error_details) with gil:
  called_flag = [False]
  def python_callback(
      Metadata metadata, grpc_status_code status,
      bytes error_details):
    cb(user_data, metadata.c_metadata, metadata.c_count, status, error_details)
    called_flag[0] = True
  cdef CredentialsMetadataPlugin self = <CredentialsMetadataPlugin>state
  cdef AuthMetadataContext cy_context = AuthMetadataContext()
  cy_context.context = context
  def async_callback():
    try:
      self.plugin_callback(cy_context, python_callback)
    except Exception as error:
      if not called_flag[0]:
        cb(user_data, NULL, 0, StatusCode.unknown,
           traceback.format_exc().encode())
  threading.Thread(group=None, target=async_callback).start()
  return 0  # Asynchronous return

cdef void plugin_destroy_c_plugin_state(void *state) with gil:
  cpython.Py_DECREF(<CredentialsMetadataPlugin>state)

def channel_credentials_google_default():
  cdef ChannelCredentials credentials = ChannelCredentials();
  with nogil:
    credentials.c_credentials = grpc_google_default_credentials_create()
  return credentials

def channel_credentials_ssl(pem_root_certificates,
                            SslPemKeyCertPair ssl_pem_key_cert_pair):
  pem_root_certificates = str_to_bytes(pem_root_certificates)
  cdef ChannelCredentials credentials = ChannelCredentials()
  cdef const char *c_pem_root_certificates = NULL
  if pem_root_certificates is not None:
    c_pem_root_certificates = pem_root_certificates
    credentials.references.append(pem_root_certificates)
  if ssl_pem_key_cert_pair is not None:
    with nogil:
      credentials.c_credentials = grpc_ssl_credentials_create(
          c_pem_root_certificates, &ssl_pem_key_cert_pair.c_pair, NULL)
    credentials.references.append(ssl_pem_key_cert_pair)
  else:
    with nogil:
      credentials.c_credentials = grpc_ssl_credentials_create(
        c_pem_root_certificates, NULL, NULL)
  return credentials

def channel_credentials_composite(
    ChannelCredentials credentials_1 not None,
    CallCredentials credentials_2 not None):
  if not credentials_1.is_valid or not credentials_2.is_valid:
    raise ValueError("passed credentials must both be valid")
  cdef ChannelCredentials credentials = ChannelCredentials()
  with nogil:
    credentials.c_credentials = grpc_composite_channel_credentials_create(
        credentials_1.c_credentials, credentials_2.c_credentials, NULL)
  credentials.references.append(credentials_1)
  credentials.references.append(credentials_2)
  return credentials

def call_credentials_composite(
    CallCredentials credentials_1 not None,
    CallCredentials credentials_2 not None):
  if not credentials_1.is_valid or not credentials_2.is_valid:
    raise ValueError("passed credentials must both be valid")
  cdef CallCredentials credentials = CallCredentials()
  with nogil:
    credentials.c_credentials = grpc_composite_call_credentials_create(
        credentials_1.c_credentials, credentials_2.c_credentials, NULL)
  credentials.references.append(credentials_1)
  credentials.references.append(credentials_2)
  return credentials

def call_credentials_google_compute_engine():
  cdef CallCredentials credentials = CallCredentials()
  with nogil:
    credentials.c_credentials = (
        grpc_google_compute_engine_credentials_create(NULL))
  return credentials

def call_credentials_service_account_jwt_access(
    json_key, Timespec token_lifetime not None):
  json_key = str_to_bytes(json_key)
  cdef CallCredentials credentials = CallCredentials()
  cdef char *json_key_c_string = json_key
  with nogil:
    credentials.c_credentials = (
        grpc_service_account_jwt_access_credentials_create(
            json_key_c_string, token_lifetime.c_time, NULL))
  credentials.references.append(json_key)
  return credentials

def call_credentials_google_refresh_token(json_refresh_token):
  json_refresh_token = str_to_bytes(json_refresh_token)
  cdef CallCredentials credentials = CallCredentials()
  cdef char *json_refresh_token_c_string = json_refresh_token
  with nogil:
    credentials.c_credentials = grpc_google_refresh_token_credentials_create(
        json_refresh_token_c_string, NULL)
  credentials.references.append(json_refresh_token)
  return credentials

def call_credentials_google_iam(authorization_token, authority_selector):
  authorization_token = str_to_bytes(authorization_token)
  authority_selector = str_to_bytes(authority_selector)
  cdef CallCredentials credentials = CallCredentials()
  cdef char *authorization_token_c_string = authorization_token
  cdef char *authority_selector_c_string = authority_selector
  with nogil:
    credentials.c_credentials = grpc_google_iam_credentials_create(
        authorization_token_c_string, authority_selector_c_string, NULL)
  credentials.references.append(authorization_token)
  credentials.references.append(authority_selector)
  return credentials

def call_credentials_metadata_plugin(CredentialsMetadataPlugin plugin):
  cdef CallCredentials credentials = CallCredentials()
  cdef grpc_metadata_credentials_plugin c_plugin = _c_plugin(plugin)
  with nogil:
    credentials.c_credentials = (
        grpc_metadata_credentials_create_from_plugin(c_plugin, NULL))
  # TODO(atash): the following held reference is *probably* never necessary
  credentials.references.append(plugin)
  return credentials

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
