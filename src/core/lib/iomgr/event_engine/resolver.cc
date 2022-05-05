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
#include "src/core/lib/iomgr/resolve_address_impl.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/surface/init.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_event_engine {
namespace experimental {
namespace {
void OnLookupComplete(
    LookupHostnameCallback on_done,
    absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses) {
  ExecCtx exec_ctx;
  if (!addresses.ok()) {
    on_done(addresses.status());
    return;
  }
  // Convert addresses to iomgr form.
  std::vector<grpc_resolved_address> result;
  results.reserve(addresses->size());
  for (size_t i = 0; i < addresses->size(); ++i) {
    results.push_back(CreateGRPCResolvedAddress(addresses[i]));
  }
  on_done(std::move(result));
}
}  // namespace

DNSResolver* EventEngineDNSResolver::GetOrCreate() {
  static EventEngineDNSResolver* instance = new EventEngineDNSResolver(
      std::move(GetDefaultEventEngine()->GetDNSResolver()));
  return instance;
}

TaskHandle EventEngineDNSResolver::ResolveName(
    absl::string_view name, absl::string_view default_port,
    grpc_pollset_set* /* interested_parties */,
    std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_done) {
  return dns_resolver_->LookupHostname(
      absl::bind_front(&EventEngineDNSRequest::OnLookupComplete, on_done), name,
      default_port, absl::InfiniteFuture());
}

absl::StatusOr<std::vector<grpc_resolved_address>>
EventEngineDNSResolver::ResolveNameBlocking(absl::string_view name,
                                            absl::string_view default_port) {
  grpc_closure on_done;
  Promise<absl::StatusOr<std::vector<grpc_resolved_address>>> evt;
  ResolveName(
      name, default_port,
      [&evt](void(absl::StatusOr<std::vector<grpc_resolved_address>> result) {
        evt.Set(std::move(result));
      }));
  return evt.Get();
}

bool Cancel(TaskHandle handle) override {
  return dns_resolver_->Cancel(handle);
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_USE_EVENT_ENGINE
