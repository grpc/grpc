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

from grpc._cython._cygrpc cimport grpc
from grpc._cython._cygrpc cimport records


cdef class ChannelCredentials:

  def __cinit__(self):
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
      grpc.grpc_channel_credentials_release(self.c_credentials)


cdef class CallCredentials:

  def __cinit__(self):
    self.c_credentials = NULL
    self.references = []

  # The object *can* be invalid in Python if we fail to make the credentials
  # (and the core thus returns NULL credentials). Used primarily for debugging.
  @property
  def is_valid(self):
    return self.c_credentials != NULL

  def __dealloc__(self):
    if self.c_credentials != NULL:
      grpc.grpc_call_credentials_release(self.c_credentials)


cdef class ServerCredentials:

  def __cinit__(self):
    self.c_credentials = NULL

  def __dealloc__(self):
    if self.c_credentials != NULL:
      grpc.grpc_server_credentials_release(self.c_credentials)


def channel_credentials_google_default():
  cdef ChannelCredentials credentials = ChannelCredentials();
  credentials.c_credentials = grpc.grpc_google_default_credentials_create()
  return credentials

def channel_credentials_ssl(pem_root_certificates,
                           records.SslPemKeyCertPair ssl_pem_key_cert_pair):
  if pem_root_certificates is None:
    pass
  elif isinstance(pem_root_certificates, bytes):
    pass
  elif isinstance(pem_root_certificates, basestring):
    pem_root_certificates = pem_root_certificates.encode()
  else:
    raise TypeError("expected str or bytes for pem_root_certificates")
  cdef ChannelCredentials credentials = ChannelCredentials()
  cdef const char *c_pem_root_certificates = NULL
  if pem_root_certificates is not None:
    c_pem_root_certificates = pem_root_certificates
    credentials.references.append(pem_root_certificates)
  if ssl_pem_key_cert_pair is not None:
    credentials.c_credentials = grpc.grpc_ssl_credentials_create(
        c_pem_root_certificates, &ssl_pem_key_cert_pair.c_pair, NULL)
    credentials.references.append(ssl_pem_key_cert_pair)
  else:
    credentials.c_credentials = grpc.grpc_ssl_credentials_create(
      c_pem_root_certificates, NULL, NULL)

def channel_credentials_composite(
    ChannelCredentials credentials_1 not None,
    CallCredentials credentials_2 not None):
  if not credentials_1.is_valid or not credentials_2.is_valid:
    raise ValueError("passed credentials must both be valid")
  cdef ChannelCredentials credentials = ChannelCredentials()
  credentials.c_credentials = grpc.grpc_composite_channel_credentials_create(
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
  credentials.c_credentials = grpc.grpc_composite_call_credentials_create(
      credentials_1.c_credentials, credentials_2.c_credentials, NULL)
  credentials.references.append(credentials_1)
  credentials.references.append(credentials_2)
  return credentials

def call_credentials_google_compute_engine():
  cdef CallCredentials credentials = CallCredentials()
  credentials.c_credentials = (
      grpc.grpc_google_compute_engine_credentials_create(NULL))
  return credentials

#TODO rename to something like client_credentials_service_account_jwt_access.
def call_credentials_service_account_jwt_access(
    json_key, records.Timespec token_lifetime not None):
  if isinstance(json_key, bytes):
    pass
  elif isinstance(json_key, basestring):
    json_key = json_key.encode()
  else:
    raise TypeError("expected json_key to be str or bytes")
  cdef CallCredentials credentials = CallCredentials()
  credentials.c_credentials = (
      grpc.grpc_service_account_jwt_access_credentials_create(
          json_key, token_lifetime.c_time, NULL))
  credentials.references.append(json_key)
  return credentials

def call_credentials_google_refresh_token(json_refresh_token):
  if isinstance(json_refresh_token, bytes):
    pass
  elif isinstance(json_refresh_token, basestring):
    json_refresh_token = json_refresh_token.encode()
  else:
    raise TypeError("expected json_refresh_token to be str or bytes")
  cdef CallCredentials credentials = CallCredentials()
  credentials.c_credentials = grpc.grpc_google_refresh_token_credentials_create(
      json_refresh_token, NULL)
  credentials.references.append(json_refresh_token)
  return credentials

def call_credentials_google_iam(authorization_token, authority_selector):
  if isinstance(authorization_token, bytes):
    pass
  elif isinstance(authorization_token, basestring):
    authorization_token = authorization_token.encode()
  else:
    raise TypeError("expected authorization_token to be str or bytes")
  if isinstance(authority_selector, bytes):
    pass
  elif isinstance(authority_selector, basestring):
    authority_selector = authority_selector.encode()
  else:
    raise TypeError("expected authority_selector to be str or bytes")
  cdef CallCredentials credentials = CallCredentials()
  credentials.c_credentials = grpc.grpc_google_iam_credentials_create(
      authorization_token, authority_selector, NULL)
  credentials.references.append(authorization_token)
  credentials.references.append(authority_selector)
  return credentials

def server_credentials_ssl(pem_root_certs, pem_key_cert_pairs,
                           bint force_client_auth):
  if pem_root_certs is None:
    pass
  elif isinstance(pem_root_certs, bytes):
    pass
  elif isinstance(pem_root_certs, basestring):
    pem_root_certs = pem_root_certs.encode()
  else:
    raise TypeError("expected pem_root_certs to be str or bytes")
  pem_key_cert_pairs = list(pem_key_cert_pairs)
  for pair in pem_key_cert_pairs:
    if not isinstance(pair, records.SslPemKeyCertPair):
      raise TypeError("expected pem_key_cert_pairs to be sequence of "
                      "records.SslPemKeyCertPair")
  cdef ServerCredentials credentials = ServerCredentials()
  credentials.references.append(pem_key_cert_pairs)
  credentials.references.append(pem_root_certs)
  credentials.c_ssl_pem_key_cert_pairs_count = len(pem_key_cert_pairs)
  credentials.c_ssl_pem_key_cert_pairs = (
      <grpc.grpc_ssl_pem_key_cert_pair *>grpc.gpr_malloc(
          sizeof(grpc.grpc_ssl_pem_key_cert_pair) *
              credentials.c_ssl_pem_key_cert_pairs_count
      ))
  for i in range(credentials.c_ssl_pem_key_cert_pairs_count):
    credentials.c_ssl_pem_key_cert_pairs[i] = (
        (<records.SslPemKeyCertPair>pem_key_cert_pairs[i]).c_pair)
  credentials.c_credentials = grpc.grpc_ssl_server_credentials_create(
      pem_root_certs, credentials.c_ssl_pem_key_cert_pairs,
      credentials.c_ssl_pem_key_cert_pairs_count, force_client_auth, NULL)
  return credentials

