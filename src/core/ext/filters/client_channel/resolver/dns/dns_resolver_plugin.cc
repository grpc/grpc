// Copyright 2022 The gRPC Authors
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

#include "src/core/ext/filters/client_channel/resolver/dns/dns_resolver_plugin.h"

#include <memory>

#include "absl/strings/match.h"

#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.h"
#include "src/core/ext/filters/client_channel/resolver/dns/event_engine/event_engine_client_channel_resolver.h"
#include "src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/resolver/resolver_factory.h"

namespace grpc_core {

void RegisterDnsResolver(CoreConfiguration::Builder* builder) {
#ifdef GRPC_IOS_EVENT_ENGINE_CLIENT
  gpr_log(GPR_DEBUG, "Using EventEngine dns resolver");
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<EventEngineClientChannelDNSResolverFactory>());
  return;
#endif
  if (IsEventEngineDnsEnabled()) {
    gpr_log(GPR_DEBUG, "Using EventEngine dns resolver");
    builder->resolver_registry()->RegisterResolverFactory(
        std::make_unique<EventEngineClientChannelDNSResolverFactory>());
    return;
  }
  auto resolver = ConfigVars::Get().DnsResolver();
  // ---- Ares resolver ----
  if (ShouldUseAresDnsResolver(resolver)) {
    gpr_log(GPR_DEBUG, "Using ares dns resolver");
    RegisterAresDnsResolver(builder);
    return;
  }
  // ---- Native resolver ----
  if (absl::EqualsIgnoreCase(resolver, "native") ||
      !builder->resolver_registry()->HasResolverFactory("dns")) {
    gpr_log(GPR_DEBUG, "Using native dns resolver");
    RegisterNativeDnsResolver(builder);
    return;
  }
  Crash(
      "Unable to set DNS resolver! Likely a logic error in gRPC-core, "
      "please file a bug.");
}

}  // namespace grpc_core
