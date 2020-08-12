// Copyright 2020 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/sockaddr.h"

#include <inttypes.h>

#include <string>

#include "absl/strings/str_format.h"

#include "test/core/util/eval_args_mock_endpoint.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

typedef struct eval_args_mock_endpoint {
  grpc_endpoint base;
  std::string peer;
  std::string local_address;
} eval_args_mock_endpoint;

static void eame_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                      grpc_closure* cb, bool unused) {}

static void eame_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                       grpc_closure* cb, void* unused) {}

static void eame_add_to_pollset(grpc_endpoint* ep, grpc_pollset* unused) {}

static void eame_add_to_pollset_set(grpc_endpoint* ep,
                                    grpc_pollset_set* unused) {}

static void eame_delete_from_pollset_set(grpc_endpoint* ep,
                                         grpc_pollset_set* unused) {}

static void eame_shutdown(grpc_endpoint* ep, grpc_error* why) {}

static void eame_destroy(grpc_endpoint* ep) {}

static absl::string_view eame_get_peer(grpc_endpoint* ep) {
  eval_args_mock_endpoint* m = reinterpret_cast<eval_args_mock_endpoint*>(ep);
  return m->peer;
}

static absl::string_view eame_get_local_address(grpc_endpoint* ep) {
  eval_args_mock_endpoint* m = reinterpret_cast<eval_args_mock_endpoint*>(ep);
  return m->local_address;
}

static grpc_resource_user* eame_get_resource_user(grpc_endpoint* ep) {
  return nullptr;
}

static int eame_get_fd(grpc_endpoint* unused) { return -1; }

static bool eame_can_track_err(grpc_endpoint* unused) { return false; }

static const grpc_endpoint_vtable vtable = {eame_read,
                                            eame_write,
                                            eame_add_to_pollset,
                                            eame_add_to_pollset_set,
                                            eame_delete_from_pollset_set,
                                            eame_shutdown,
                                            eame_destroy,
                                            eame_get_resource_user,
                                            eame_get_peer,
                                            eame_get_local_address,
                                            eame_get_fd,
                                            eame_can_track_err};

static std::string name_and_port_to_uri(const char* addr, const int port) {
  grpc_sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  inet_aton(addr, &address.sin_addr);
  grpc_resolved_address resolved;
  memset(&resolved, 0, sizeof(resolved));
  memcpy(resolved.addr, &address, sizeof(address));
  resolved.len = sizeof(address);
  return grpc_sockaddr_to_uri(&resolved);
}

grpc_endpoint* grpc_eval_args_mock_endpoint_create(const char* local_address,
                                                   const int local_port,
                                                   const char* peer_address,
                                                   const int peer_port) {
  eval_args_mock_endpoint* m = new eval_args_mock_endpoint;
  m->base.vtable = &vtable;
  m->peer = name_and_port_to_uri(peer_address, peer_port);
  m->local_address = name_and_port_to_uri(local_address, local_port);
  return &m->base;
}

void grpc_eval_args_mock_endpoint_destroy(grpc_endpoint* ep) {
  eval_args_mock_endpoint* m = reinterpret_cast<eval_args_mock_endpoint*>(ep);
  delete m;
}
