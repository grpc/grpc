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


def _spawn_callback_in_thread(cb_func, args):
  t = ForkManagedThread(target=cb_func, args=args)
  t.setDaemon(True)
  t.start()

async_callback_func = _spawn_callback_in_thread

def set_async_callback_func(callback_func):
  global async_callback_func
  async_callback_func = callback_func

def _spawn_callback_async(callback, args):
  async_callback_func(callback, args)


cdef class CallCredentials:

  cdef grpc_call_credentials *c(self) except *:
    raise NotImplementedError()


cdef int _get_metadata(void *state,
                       grpc_auth_metadata_context context,
                       grpc_credentials_plugin_metadata_cb cb,
                       void *user_data,
                       grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
                       size_t *num_creds_md,
                       grpc_status_code *status,
                       const char **error_details) except * with gil:
  cdef size_t metadata_count
  cdef grpc_metadata *c_metadata
  def callback(metadata, grpc_status_code status, bytes error_details):
    cdef char* c_error_details = NULL
    if error_details is not None:
      c_error_details = <char*> error_details
    if status == StatusCode.ok:
      _store_c_metadata(metadata, &c_metadata, &metadata_count)
      with nogil:
        cb(user_data, c_metadata, metadata_count, status, NULL)
      _release_c_metadata(c_metadata, metadata_count)
    else:
      with nogil:
        cb(user_data, NULL, 0, status, c_error_details)
  args = context.service_url, context.method_name, callback,
  plugin = <object>state
  if plugin._stored_ctx is not None:
    plugin._stored_ctx.copy().run(_spawn_callback_async, plugin, args)
  else:
    _spawn_callback_async(<object>state, args)
  return 0  # Asynchronous return


cdef void _destroy(void *state) except * with gil:
  cpython.Py_DECREF(<object>state)
  grpc_shutdown()


cdef class MetadataPluginCallCredentials(CallCredentials):

  def __cinit__(self, metadata_plugin, name):
    self._metadata_plugin = metadata_plugin
    self._name = name

  cdef grpc_call_credentials *c(self) except *:
    cdef grpc_metadata_credentials_plugin c_metadata_plugin
    c_metadata_plugin.get_metadata = _get_metadata
    c_metadata_plugin.destroy = _destroy
    c_metadata_plugin.state = <void *>self._metadata_plugin
    c_metadata_plugin.type = self._name
    cpython.Py_INCREF(self._metadata_plugin)
    fork_handlers_and_grpc_init()
    # TODO(yihuazhang): Expose min_security_level via the Python API so that
    # applications can decide what minimum security level their plugins require.
    return grpc_metadata_credentials_create_from_plugin(c_metadata_plugin, GRPC_PRIVACY_AND_INTEGRITY, NULL)


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

  cdef grpc_call_credentials *c(self) except *:
    return _composition(self._call_credentialses)


cdef class ChannelCredentials:

  cdef grpc_channel_credentials *c(self) except *:
    raise NotImplementedError()


cdef class SSLSessionCacheLRU:

  def __cinit__(self, capacity):
    fork_handlers_and_grpc_init()
    self._cache = grpc_ssl_session_cache_create_lru(capacity)

  def __int__(self):
    return <uintptr_t>self._cache

  def __dealloc__(self):
    if self._cache != NULL:
        grpc_ssl_session_cache_destroy(self._cache)
    grpc_shutdown()


cdef class SSLChannelCredentials(ChannelCredentials):

  def __cinit__(self, pem_root_certificates, private_key, certificate_chain):
    if pem_root_certificates is not None and not isinstance(pem_root_certificates, bytes):
      raise TypeError('expected certificate to be bytes, got %s' % (type(pem_root_certificates)))
    self._pem_root_certificates = pem_root_certificates
    self._private_key = private_key
    self._certificate_chain = certificate_chain

  cdef grpc_channel_credentials *c(self) except *:
    cdef const char *c_pem_root_certificates
    cdef grpc_ssl_pem_key_cert_pair c_pem_key_certificate_pair
    if self._pem_root_certificates is None:
      c_pem_root_certificates = NULL
    else:
      c_pem_root_certificates = self._pem_root_certificates
    if self._private_key is None and self._certificate_chain is None:
      with nogil:
        return grpc_ssl_credentials_create(
            c_pem_root_certificates, NULL, NULL, NULL)
    else:
      if self._private_key:
        c_pem_key_certificate_pair.private_key = self._private_key
      else:
        c_pem_key_certificate_pair.private_key = NULL
      if self._certificate_chain:
        c_pem_key_certificate_pair.certificate_chain = self._certificate_chain
      else:
        c_pem_key_certificate_pair.certificate_chain = NULL
      with nogil:
        return grpc_ssl_credentials_create(
            c_pem_root_certificates, &c_pem_key_certificate_pair, NULL, NULL)


cdef class CompositeChannelCredentials(ChannelCredentials):

  def __cinit__(self, call_credentialses, channel_credentials):
    self._call_credentialses = call_credentialses
    self._channel_credentials = channel_credentials

  cdef grpc_channel_credentials *c(self) except *:
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


cdef class XDSChannelCredentials(ChannelCredentials):

    def __cinit__(self, fallback_credentials):
        self._fallback_credentials = fallback_credentials

    cdef grpc_channel_credentials *c(self) except *:
      cdef grpc_channel_credentials *c_fallback_creds = self._fallback_credentials.c()
      cdef grpc_channel_credentials *xds_creds = grpc_xds_credentials_create(c_fallback_creds)
      grpc_channel_credentials_release(c_fallback_creds)
      return xds_creds


cdef class ServerCertificateConfig:

  def __cinit__(self):
    fork_handlers_and_grpc_init()
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
    fork_handlers_and_grpc_init()
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

cdef grpc_ssl_certificate_config_reload_status _server_cert_config_fetcher_wrapper(
        void* user_data, grpc_ssl_server_certificate_config **config) with gil:
  # This is a credentials.ServerCertificateConfig
  cdef ServerCertificateConfig cert_config = None
  if not user_data:
    raise ValueError('internal error: user_data must be specified')
  credentials = <ServerCredentials>user_data
  if not credentials.initial_cert_config_fetched:
    # C-core is asking for the initial cert config
    credentials.initial_cert_config_fetched = True
    cert_config = credentials.initial_cert_config._certificate_configuration
  else:
    user_cb = credentials.cert_config_fetcher
    try:
      cert_config_wrapper = user_cb()
    except Exception:
      _LOGGER.exception('Error fetching certificate config')
      return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL
    if cert_config_wrapper is None:
      return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED
    elif not isinstance(
        cert_config_wrapper, grpc.ServerCertificateConfiguration):
      _LOGGER.error(
          'Error fetching certificate configuration: certificate '
          'configuration must be of type grpc.ServerCertificateConfiguration, '
          'not %s' % type(cert_config_wrapper).__name__)
      return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL
    else:
      cert_config = cert_config_wrapper._certificate_configuration
  config[0] = <grpc_ssl_server_certificate_config*>cert_config.c_cert_config
  # our caller will assume ownership of memory, so we have to recreate
  # a copy of c_cert_config here
  cert_config.c_cert_config = grpc_ssl_server_certificate_config_create(
      cert_config.c_pem_root_certs, cert_config.c_ssl_pem_key_cert_pairs,
      cert_config.c_ssl_pem_key_cert_pairs_count)
  return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW


class LocalConnectionType:
  uds = UDS
  local_tcp = LOCAL_TCP

cdef class LocalChannelCredentials(ChannelCredentials):

  def __cinit__(self, grpc_local_connect_type local_connect_type):
    self._local_connect_type = local_connect_type

  cdef grpc_channel_credentials *c(self) except *:
    cdef grpc_local_connect_type local_connect_type
    local_connect_type = self._local_connect_type
    return grpc_local_credentials_create(local_connect_type)

def channel_credentials_local(grpc_local_connect_type local_connect_type):
  return LocalChannelCredentials(local_connect_type)

cdef class InsecureChannelCredentials(ChannelCredentials):

  cdef grpc_channel_credentials *c(self) except *:
    return grpc_insecure_credentials_create()

def channel_credentials_insecure():
  return InsecureChannelCredentials()

def server_credentials_local(grpc_local_connect_type local_connect_type):
  cdef ServerCredentials credentials = ServerCredentials()
  credentials.c_credentials = grpc_local_server_credentials_create(local_connect_type)
  return credentials

def xds_server_credentials(ServerCredentials fallback_credentials):
  cdef ServerCredentials credentials = ServerCredentials()
  credentials.c_credentials = grpc_xds_server_credentials_create(fallback_credentials.c_credentials)
  # NOTE: We do not need to call grpc_server_credentials_release on the
  # fallback credentials here because this will be done by the __dealloc__
  # method of its Cython wrapper.
  return credentials

def insecure_server_credentials():
  cdef ServerCredentials credentials = ServerCredentials()
  credentials.c_credentials = grpc_insecure_server_credentials_create()
  return credentials

cdef class ALTSChannelCredentials(ChannelCredentials):

  def __cinit__(self, list service_accounts):
    self.c_options = grpc_alts_credentials_client_options_create()
    cdef str account
    for account in service_accounts:
      grpc_alts_credentials_client_options_add_target_service_account(
          self.c_options, str_to_bytes(account))
 
  def __dealloc__(self):
    if self.c_options != NULL:
      grpc_alts_credentials_options_destroy(self.c_options)

  cdef grpc_channel_credentials *c(self) except *:
    return grpc_alts_credentials_create(self.c_options)
    

def channel_credentials_alts(list service_accounts):
  return ALTSChannelCredentials(service_accounts)


def server_credentials_alts():
  cdef ServerCredentials credentials = ServerCredentials()
  cdef grpc_alts_credentials_options* c_options = grpc_alts_credentials_server_options_create()
  credentials.c_credentials = grpc_alts_server_credentials_create(c_options)
  # Options can be destroyed as deep copy was performed.
  grpc_alts_credentials_options_destroy(c_options)
  return credentials


cdef class ComputeEngineChannelCredentials(ChannelCredentials):
  cdef grpc_channel_credentials* _c_creds
  cdef grpc_call_credentials* _call_creds

  def __cinit__(self, CallCredentials call_creds):
    self._c_creds = NULL
    self._call_creds = call_creds.c()
    if self._call_creds == NULL:
      raise ValueError("Call credentials may not be NULL.")

  cdef grpc_channel_credentials *c(self) except *:
    self._c_creds = grpc_google_default_credentials_create(self._call_creds)
    return self._c_creds


def channel_credentials_compute_engine(call_creds):
  return ComputeEngineChannelCredentials(call_creds)
