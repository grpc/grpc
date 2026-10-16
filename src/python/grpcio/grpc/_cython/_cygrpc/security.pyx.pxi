# Copyright 2016 gRPC authors.
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

from libc.string cimport memcpy

import pkgutil


cdef grpc_ssl_roots_override_result ssl_roots_override_callback(
    char **pem_root_certs) noexcept nogil:
  with gil:
    pkg = __name__
    if pkg.endswith('.cygrpc'):
      pkg = pkg[:-len('.cygrpc')]
    temporary_pem_root_certs = pkgutil.get_data(pkg, '_credentials/roots.pem')
    pem_root_certs[0] = <char *>gpr_malloc(len(temporary_pem_root_certs) + 1)
    memcpy(
        pem_root_certs[0], <char *>temporary_pem_root_certs,
        len(temporary_pem_root_certs))
    pem_root_certs[0][len(temporary_pem_root_certs)] = '\0'

  return GRPC_SSL_ROOTS_OVERRIDE_OK


def peer_identities(Call call):
  cdef grpc_auth_context* auth_context
  cdef grpc_auth_property_iterator properties
  cdef const grpc_auth_property* property

  auth_context = grpc_call_auth_context(call.c_call)
  if auth_context == NULL:
    return None
  properties = grpc_auth_context_peer_identity(auth_context)
  identities = []
  while True:
    property = grpc_auth_property_iterator_next(&properties)
    if property == NULL:
      break
    if property.value != NULL:
      identities.append(<bytes>(property.value))
  grpc_auth_context_release(auth_context)
  return identities if identities else None

def peer_identity_key(Call call):
  cdef grpc_auth_context* auth_context
  cdef const char* c_key
  auth_context = grpc_call_auth_context(call.c_call)
  if auth_context == NULL:
    return None
  c_key = grpc_auth_context_peer_identity_property_name(auth_context)
  if c_key == NULL:
    key = None
  else:
    key = <bytes> grpc_auth_context_peer_identity_property_name(auth_context)
  grpc_auth_context_release(auth_context)
  return key

def auth_context(Call call):
  cdef grpc_auth_context* auth_context
  cdef grpc_auth_property_iterator properties
  cdef const grpc_auth_property* property

  auth_context = grpc_call_auth_context(call.c_call)
  if auth_context == NULL:
    return {}
  properties = grpc_auth_context_property_iterator(auth_context)
  py_auth_context = {}
  while True:
    property = grpc_auth_property_iterator_next(&properties)
    if property == NULL:
      break
    if property.name != NULL and property.value != NULL:
      key = <bytes> property.name
      if key in py_auth_context:
        py_auth_context[key].append(<bytes>(property.value))
      else:
        py_auth_context[key] = [<bytes> property.value]
  grpc_auth_context_release(auth_context)
  return py_auth_context
  
