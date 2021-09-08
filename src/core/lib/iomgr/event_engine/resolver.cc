// Copyright 2021 The gRPC Authors
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

#ifdef GRPC_USE_EVENT_ENGINE
#include "absl/functional/bind_front.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine/iomgr.h"
#include "src/core/lib/iomgr/event_engine/promise.h"
#include "src/core/lib/iomgr/event_engine/resolved_address_internal.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/surface/init.h"
#include "src/core/lib/transport/error_utils.h"

namespace {
using ::grpc_event_engine::experimental::CreateGRPCResolvedAddress;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::Promise;

/// A fire-and-forget class representing an individual DNS request.
///
/// This provides a place to store the ownership of the DNSResolver object until
/// the request is complete.
class DnsRequest {
 public:
  DnsRequest(std::unique_ptr<EventEngine::DNSResolver> dns_resolver,
             absl::string_view address, absl::string_view default_port,
             grpc_closure* on_done, grpc_resolved_addresses** addresses)
      : dns_resolver_(std::move(dns_resolver)),
        cb_(on_done),
        addresses_(addresses) {
    dns_resolver_->LookupHostname(
        absl::bind_front(&DnsRequest::OnLookupComplete, this), address,
        default_port, absl::InfiniteFuture());
  }

 private:
  void OnLookupComplete(
      absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses) {
    grpc_core::ExecCtx exec_ctx;
    // Convert addresses to iomgr form.
    *addresses_ = static_cast<grpc_resolved_addresses*>(
        gpr_malloc(sizeof(grpc_resolved_addresses)));
    (*addresses_)->naddrs = addresses->size();
    (*addresses_)->addrs = static_cast<grpc_resolved_address*>(
        gpr_malloc(sizeof(grpc_resolved_address) * addresses->size()));
    for (size_t i = 0; i < addresses->size(); ++i) {
      (*addresses_)->addrs[i] = CreateGRPCResolvedAddress((*addresses)[i]);
    }
    grpc_closure* cb = cb_;
    delete this;
    grpc_core::Closure::Run(DEBUG_LOCATION, cb,
                            absl_status_to_grpc_error(addresses.status()));
  }

  std::unique_ptr<EventEngine::DNSResolver> dns_resolver_;
  grpc_closure* cb_;
  grpc_resolved_addresses** addresses_;
};

void resolve_address(const char* addr, const char* default_port,
                     grpc_pollset_set* /* interested_parties */,
                     grpc_closure* on_done,
                     grpc_resolved_addresses** addresses) {
  auto dns_resolver = grpc_iomgr_event_engine()->GetDNSResolver();
  if (!dns_resolver.ok()) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done,
                            absl_status_to_grpc_error(dns_resolver.status()));
    return;
  }
  new DnsRequest(std::move(*dns_resolver), addr, default_port, on_done,
                 addresses);
}

void blocking_handle_async_resolve_done(void* arg, grpc_error_handle error) {
  static_cast<Promise<grpc_error_handle>*>(arg)->Set(std::move(error));
}

grpc_error* blocking_resolve_address(const char* name, const char* default_port,
                                     grpc_resolved_addresses** addresses) {
  grpc_closure on_done;
  Promise<grpc_error_handle> evt;
  GRPC_CLOSURE_INIT(&on_done, blocking_handle_async_resolve_done, &evt,
                    grpc_schedule_on_exec_ctx);
  resolve_address(name, default_port, nullptr, &on_done, addresses);
  return evt.Get();
}

}  // namespace

grpc_address_resolver_vtable grpc_event_engine_resolver_vtable{
    resolve_address, blocking_resolve_address};

#endif  // GRPC_USE_EVENT_ENGINE
