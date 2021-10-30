/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/resolve_address.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/alloc.h>

namespace grpc_core {
const char* kDefaultSecurePort = "https";
}  // namespace grpc_core

grpc_address_resolver_vtable* grpc_resolve_address_impl;

void grpc_set_resolver_impl(grpc_address_resolver_vtable* vtable) {
  grpc_resolve_address_impl = vtable;
}

grpc_core::OrphanablePtr<grpc_core::AsyncResolveAddress> grpc_resolve_address(
    const char* addr, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_resolved_addresses** addresses) {
  return grpc_resolve_address_impl->resolve_address(
      addr, default_port, interested_parties, on_done, addresses);
}

void grpc_resolved_addresses_destroy(grpc_resolved_addresses* addresses) {
  if (addresses != nullptr) {
    gpr_free(addresses->addrs);
  }
  gpr_free(addresses);
}

grpc_error_handle grpc_blocking_resolve_address(
    const char* name, const char* default_port,
    grpc_resolved_addresses** addresses) {
  return grpc_resolve_address_impl->blocking_resolve_address(name, default_port,
                                                             addresses);
}
