# Copyright 2019 gRPC authors.
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
# distutils: language=c++

from libc cimport string
from libc.stdlib cimport malloc
from libcpp.string cimport string as cppstring

cdef grpc_error_handle grpc_error_none():
  return <grpc_error_handle>0

cdef grpc_error_handle socket_error(str syscall, str err):
  error_str = "{} failed: {}".format(syscall, err)
  error_bytes = str_to_bytes(error_str)
  return grpc_socket_error(error_bytes)

cdef resolved_addr_to_tuple(grpc_resolved_address* address):
  cdef cppstring res_str
  port = grpc_sockaddr_get_port(address)
  res_str = grpc_sockaddr_to_string(address, False)
  byte_str = _decode(res_str)
  if byte_str.endswith(':' + str(port)):
    byte_str = byte_str[:(0 - len(str(port)) - 1)]
  byte_str = byte_str.lstrip('[')
  byte_str = byte_str.rstrip(']')
  byte_str = '{}'.format(byte_str)
  return byte_str, port

cdef sockaddr_to_tuple(const grpc_sockaddr* address, size_t length):
  cdef grpc_resolved_address c_addr
  string.memcpy(<void*>c_addr.addr, <void*> address, length)
  c_addr.len = length
  return resolved_addr_to_tuple(&c_addr)

cdef sockaddr_is_ipv4(const grpc_sockaddr* address, size_t length):
  cdef grpc_resolved_address c_addr
  string.memcpy(<void*>c_addr.addr, <void*> address, length)
  c_addr.len = length
  return grpc_sockaddr_get_uri_scheme(&c_addr) == b'ipv4'

cdef grpc_resolved_addresses* tuples_to_resolvaddr(tups):
  cdef grpc_resolved_addresses* addresses
  tups_set = set((tup[4][0], tup[4][1]) for tup in tups)
  addresses = <grpc_resolved_addresses*> malloc(sizeof(grpc_resolved_addresses))
  addresses.naddrs = len(tups_set)
  addresses.addrs = <grpc_resolved_address*> malloc(sizeof(grpc_resolved_address) * len(tups_set))
  i = 0
  for tup in set(tups_set):
    hostname = str_to_bytes(tup[0])
    grpc_string_to_sockaddr(&addresses.addrs[i], hostname, tup[1])
    i += 1
  return addresses
