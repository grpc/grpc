# Copyright 2016, Google Inc.
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

from libc.string cimport memcpy

import pkg_resources


cdef grpc_ssl_roots_override_result ssl_roots_override_callback(
    char **pem_root_certs) nogil:
  with gil:
    temporary_pem_root_certs = pkg_resources.resource_string(
        __name__.rstrip('.cygrpc'), '_credentials/roots.pem')
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
  
