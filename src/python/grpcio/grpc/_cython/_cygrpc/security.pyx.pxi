# Copyright 2016, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from libc.string cimport memcpy

import pkg_resources


cdef grpc_ssl_roots_override_result ssl_roots_override_callback(
    char **pem_root_certs) with gil:
  temporary_pem_root_certs = pkg_resources.resource_string(
      __name__.rstrip('.cygrpc'), '_credentials/roots.pem')
  pem_root_certs[0] = <char *>gpr_malloc(len(temporary_pem_root_certs) + 1)
  memcpy(
      pem_root_certs[0], <char *>temporary_pem_root_certs,
      len(temporary_pem_root_certs))
  pem_root_certs[0][len(temporary_pem_root_certs)] = '\0'
  return GRPC_SSL_ROOTS_OVERRIDE_OK
