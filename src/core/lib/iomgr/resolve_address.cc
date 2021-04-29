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

#include "src/core/lib/iomgr/resolve_address.h"
#include <grpc/event_engine/event_engine.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>

grpc_address_resolver_vtable* grpc_resolve_address_impl;

void grpc_set_resolver_impl(grpc_address_resolver_vtable* vtable) {
  grpc_resolve_address_impl = vtable;
}

void grpc_resolve_address(const char* addr, const char* default_port,
                          grpc_pollset_set* interested_parties,
                          grpc_closure* on_done,
                          grpc_resolved_addresses** addresses) {
#if 0
  grpc_resolve_address_impl->resolve_address(
      addr, default_port, interested_parties, on_done, addresses);
#else
  std::unique_ptr<grpc_event_engine::experimental::EventEngine::DNSResolver>
      resolver = grpc_event_engine::experimental::GetDefaultEventEngine()
                     ->GetDNSResolver()
                     .value();
  resolver->LookupHostname(
      [addresses, on_done](
          absl::Status status,
          std::vector<
              grpc_event_engine::experimental::EventEngine::ResolvedAddress>
              vaddresses) {
        if (!status.ok()) abort();
        grpc_resolved_addresses* a = *addresses =
            (grpc_resolved_addresses*)gpr_malloc(
                sizeof(grpc_resolved_addresses));
        a->addrs = (grpc_resolved_address*)gpr_malloc(
            sizeof(grpc_resolved_address) * vaddresses.size());
        for (size_t i = 0; i < vaddresses.size(); i++) {
          auto& r = vaddresses[i];
          memcpy(&a->addrs[i].addr, r.address(), r.size());
          a->addrs[i].len = r.size();
          break;
        }
        a->naddrs = vaddresses.size();
        grpc_core::ExecCtx ctx;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
      },
      addr, default_port, absl::InfiniteFuture());
#endif
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
