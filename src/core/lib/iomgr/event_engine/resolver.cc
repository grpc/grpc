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
#if defined(GRPC_EVENT_ENGINE_TEST)

#include <grpc/support/port_platform.h>

#include "absl/functional/bind_front.h"
#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine/resolved_address_internal.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/transport/error_utils.h"

namespace {
using ::grpc_event_engine::experimental::CreateGRPCResolvedAddress;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;

EventEngine::DNSResolver::LookupHostnameCallback
event_engine_closure_to_lookup_hostname_callback(grpc_closure* closure) {
  (void)closure;
  return [](absl::Status, std::vector<EventEngine::ResolvedAddress>) {};
}

EventEngine::DNSResolver::LookupSRVCallback
event_engine_closure_to_lookup_srv_callback(grpc_closure* closure) {
  (void)closure;
  return [](absl::Status, std::vector<EventEngine::DNSResolver::SRVRecord>) {};
}

EventEngine::DNSResolver::LookupTXTCallback
event_engine_closure_to_lookup_txt_callback(grpc_closure* closure) {
  (void)closure;
  return [](absl::Status, std::string) {};
}

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
  void OnLookupComplete(absl::Status status,
                        std::vector<EventEngine::ResolvedAddress> addresses) {
    grpc_core::ExecCtx exec_ctx;
    // Convert addresses to iomgr form.
    *addresses_ = static_cast<grpc_resolved_addresses*>(
        gpr_malloc(sizeof(grpc_resolved_addresses)));
    (*addresses_)->naddrs = addresses.size();
    (*addresses_)->addrs = static_cast<grpc_resolved_address*>(
        gpr_malloc(sizeof(grpc_resolved_address) * addresses.size()));
    for (size_t i = 0; i < addresses.size(); ++i) {
      (*addresses_)->addrs[i] = CreateGRPCResolvedAddress(addresses[i]);
    }
    // Delete ourselves and invoke closure.
    grpc_closure* cb = cb_;
    delete this;
    grpc_core::Closure::Run(DEBUG_LOCATION, cb,
                            absl_status_to_grpc_error(status));
  }

  std::unique_ptr<EventEngine::DNSResolver> dns_resolver_;
  grpc_closure* cb_;
  grpc_resolved_addresses** addresses_;
};

void resolve_address(const char* addr, const char* default_port,
                     grpc_pollset_set* /* interested_parties */,
                     grpc_closure* on_done,
                     grpc_resolved_addresses** addresses) {
  std::shared_ptr<EventEngine> event_engine = GetDefaultEventEngine();
  auto dns_resolver = event_engine->GetDNSResolver();
  if (!dns_resolver.ok()) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done,
                            absl_status_to_grpc_error(dns_resolver.status()));
    return;
  }
  new DnsRequest(std::move(*dns_resolver), addr, default_port, on_done,
                 addresses);
}

grpc_error* blocking_resolve_address(const char* name, const char* default_port,
                                     grpc_resolved_addresses** addresses) {
  (void)name;
  (void)default_port;
  (void)addresses;
  return GRPC_ERROR_NONE;
}

}  // namespace

grpc_address_resolver_vtable grpc_event_engine_resolver_vtable{
    resolve_address, blocking_resolve_address};

#endif  // GRPC_EVENT_ENGINE_TEST
