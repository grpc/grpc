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

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.h"
#include "src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.h"
#include "src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/global_config_generic.h"

namespace grpc_core {

void RegisterAppropriateDnsResolver(CoreConfiguration::Builder* builder) {
  if (IsEventEngineDnsEnabled()) {
    builder->resolver_registry()->RegisterResolverFactory(std::make_unique<EventEngineClientChannelResolver>());
  }
  // ---- Ares resolver ----
  if (UseAresDnsResolver()) {
    RegisterAresDnsResolver(builder);
    return;
  }

  // ---- EventEngine resolver ----
  // TODO(hork): change this logic when an EE resolver is available.

  // ---- Native resolver ----
  static const char* const resolver =
      GPR_GLOBAL_CONFIG_GET(grpc_dns_resolver).release();
  if (gpr_stricmp(resolver, "native") == 0 ||
      !builder->resolver_registry()->HasResolverFactory("dns")) {
    RegisterNativeDnsResolver(builder);
  }

  GPR_ASSERT(false &&
             "Unable to set DNS resolver! Likely a logic error in gRPC-core, "
             "please file a bug.");
}

}  // namespace grpc_core
