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
#include "src/core/lib/event_engine/event_engine_factory.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine/promise.h"
#include "src/core/lib/iomgr/event_engine/resolved_address_internal.h"
#include "src/core/lib/iomgr/event_engine/resolver.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/surface/init.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {
namespace experimental {
namespace {
using ::grpc_event_engine::experimental::CreateGRPCResolvedAddress;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::Promise;

/// A fire-and-forget class representing an individual DNS request.
///
/// This provides a place to store the ownership of the DNSResolver object until
/// the request is complete.
class EventEngineDNSRequest : DNSRequest {
 public:
  EventEngineDNSRequest(std::unique_ptr<EventEngine::DNSResolver> dns_resolver,
                        absl::string_view name, absl::string_view default_port,
                        grpc_closure* on_done,
                        grpc_resolved_addresses** addresses)
      : dns_resolver_(std::move(dns_resolver)),
        name_(std::string(name)),
        default_port_(std::string(default_port)),
        on_done_(std::move(on_done)) {}

  void Start() override {
    if (dns_resolver == nullptr) {
      new DNSCallbackExecCtxScheduler(
          std::move(on_done_),
          absl::UnknownError("Failed to get DNS Resolver."));
      return;
    }
    Ref().release();  // ref held by pending resolution
    dns_resolver_->LookupHostname(
        absl::bind_front(&EventEngineDNSRequest::OnLookupComplete, this), name_,
        default_port_, absl::InfiniteFuture());
  }

  // TOOD(hork): implement cancellation; currently it's a no-op
  void Orphan() override { Unref(); }

 private:
  void OnLookupComplete(
      absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses) {
    ExecCtx exec_ctx;
    // Convert addresses to iomgr form.
    grpc_resolved_addresses* result = static_cast<grpc_resolved_addresses*>(
        gpr_malloc(sizeof(grpc_resolved_addresses)));
    result->naddrs = addresses->size();
    result->addrs = static_cast<grpc_resolved_address*>(
        gpr_malloc(sizeof(grpc_resolved_address) * addresses->size()));
    for (size_t i = 0; i < addresses->size(); ++i) {
      result->addrs[i] = CreateGRPCResolvedAddress(addresses[i]);
    }
    if (addresses.ok()) {
      on_done_(result);
    } else {
      grpc_resolved_addresses_destroy(result);
      on_done_(addresses.status());
    }
    Unref();
  }

  std::unique_ptr<EventEngine::DNSResolver> dns_resolver_;
  const std::string name_;
  const std::string default_port_;
  const std::function<void(absl::StatusOr<grpc_resolved_addresses*>)> on_done_;
};

EventEngineDNSResolver* g_dns_resolver;

}  // namespace

DNSResolver* EventEngineDNSResolver::GetOrCreate() {
  if (g_dns_resolver == nullptr) {
    g_dns_resolver = new EventEngineDNSResolver();
  }
  return g_dns_resolver;
}

OrphanablePtr<DNSRequest> EventEngineDNSResolver::CreateRequest(
    absl::string_view name, absl::string_view default_port,
    grpc_pollset_set* /* interested_parties */,
    std::function<void(absl::StatusOr<grpc_resolved_addresses*>)> on_done) {
  std::unique_ptr<EventEngine::DNSResolver> dns_resolver =
      GetDefaultEventEngine()->GetDNSResolver();
  return MakeOrphanable<EventEngineDNSRequest>(
      std::move(dns_resolver), name, default_port, std::move(on_done));
}

absl::StatusOr<grpc_resolved_addresses*>
EventEngineDNSResolver::BlockingResolveAddress(absl::string_view name,
                                               absl::string_view default_port) {
  grpc_closure on_done;
  Promise<absl::StatusOr<grpc_resolved_addresses*>> evt;
  auto r = CreateRequest(
      name, default_port,
      [&evt](void(absl::StatusOr<grpc_resolved_addresses*> result) {
        evt.Set(std::move(result));
      }));
  r->Start();
  return evt.Get();
}

}  // namespace experimental
}  // namespace grpc_core

#endif  // GRPC_USE_EVENT_ENGINE
