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
#include "src/core/resolver/dns/dns_resolver_plugin.h"

#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/config/config_vars.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/resolver/dns/event_engine/event_engine_client_channel_resolver.h"
#include "src/core/resolver/resolver_factory.h"
#include "src/core/util/crash.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"

namespace grpc_core {

void RegisterDnsResolver(CoreConfiguration::Builder* builder) {
  VLOG(2) << "Using EventEngine dns resolver";
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<EventEngineClientChannelDNSResolverFactory>());
}

}  // namespace grpc_core
