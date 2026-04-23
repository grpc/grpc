//
//
// Copyright 2016 gRPC authors.
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
//
//

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include "src/core/config/core_configuration.h"
#include "src/core/handshaker/endpoint_info/endpoint_info_handshaker.h"
#include "src/core/handshaker/http_connect/http_connect_client_handshaker.h"
#include "src/core/handshaker/tcp_connect/tcp_connect_handshaker.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/lame_client.h"
#include "src/core/server/server.h"
#include "src/core/server/server_call_tracer_filter.h"
#include "src/core/server/server_config_selector_filter.h"

namespace grpc_event_engine {
namespace experimental {
extern void RegisterEventEngineChannelArgPreconditioning(
    grpc_core::CoreConfiguration::Builder* builder);
}  // namespace experimental
}  // namespace grpc_event_engine

namespace grpc_core {

extern void BuildClientChannelConfiguration(
    CoreConfiguration::Builder* builder);
extern void SecurityRegisterHandshakerFactories(
    CoreConfiguration::Builder* builder);
extern void RegisterAuthComparators(CoreConfiguration::Builder* builder);
extern void RegisterClientAuthorityFilter(CoreConfiguration::Builder* builder);
extern void RegisterLegacyChannelIdleFilters(
    CoreConfiguration::Builder* builder);
extern void RegisterGrpcLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterHttpFilters(CoreConfiguration::Builder* builder);
extern void RegisterMessageSizeFilter(CoreConfiguration::Builder* builder);
extern void RegisterSecurityFilters(CoreConfiguration::Builder* builder);
extern void RegisterServiceConfigChannelArgFilter(
    CoreConfiguration::Builder* builder);
extern void RegisterExtraFilters(CoreConfiguration::Builder* builder);
extern void RegisterResourceQuota(CoreConfiguration::Builder* builder);
extern void FaultInjectionFilterRegister(CoreConfiguration::Builder* builder);
extern void RegisterDnsResolver(CoreConfiguration::Builder* builder);
extern void RegisterBackendMetricFilter(CoreConfiguration::Builder* builder);
extern void RegisterSockaddrResolver(CoreConfiguration::Builder* builder);
extern void RegisterFakeResolver(CoreConfiguration::Builder* builder);
extern void RegisterPriorityLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterOutlierDetectionLbPolicy(
    CoreConfiguration::Builder* builder);
extern void RegisterWeightedTargetLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterPickFirstLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterRingHashLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterRoundRobinLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterWeightedRoundRobinLbPolicy(
    CoreConfiguration::Builder* builder);
extern void RegisterHttpProxyMapper(CoreConfiguration::Builder* builder);
extern void RegisterConnectedChannel(CoreConfiguration::Builder* builder);
extern void RegisterLoadBalancedCallDestination(
    CoreConfiguration::Builder* builder);
extern void RegisterChttp2Transport(CoreConfiguration::Builder* builder);
extern void RegisterFusedFilters(CoreConfiguration::Builder* builder);
#ifndef GRPC_NO_RLS
extern void RegisterRlsLbPolicy(CoreConfiguration::Builder* builder);
#endif  // !GRPC_NO_RLS

namespace {

void RegisterBuiltins(CoreConfiguration::Builder* builder) {
  RegisterServerCallTracerFilter(builder);
  builder->channel_init()
      ->RegisterV2Filter<LameClientFilter>(GRPC_CLIENT_LAME_CHANNEL)
      .Terminal();
  if (!IsXdsServerFilterChainPerRouteEnabled()) {
    builder->channel_init()
        ->RegisterFilter(GRPC_SERVER_CHANNEL, &Server::kServerTopFilter)
        .SkipV3()
        .BeforeAll();
  } else {
    // Register server top filter twice:
    // - For GRPC_SERVER_CHANNEL, only if GRPC_ARG_BELOW_DYNAMIC_FILTERS
    //   is *not* set.  This covers the case where dynamic filters are
    //   *not* used.
    // - For GRPC_SERVER_TOP_CHANNEL.  This covers the case where
    //   dynamic filters *are* used.
    // See the comment in channel_stack_type.h for details.
    builder->channel_init()
        ->RegisterFilter(GRPC_SERVER_CHANNEL, &Server::kServerTopFilter)
        .SkipV3()
        .If([](const ChannelArgs& args) {
          return !args.Contains(GRPC_ARG_BELOW_DYNAMIC_FILTERS);
        })
        .BeforeAll();
    builder->channel_init()
        ->RegisterFilter(GRPC_SERVER_TOP_CHANNEL, &Server::kServerTopFilter)
        .SkipV3()
        .BeforeAll();
    // Also register the server config selector filter.
    builder->channel_init()
        ->RegisterFilter(GRPC_SERVER_TOP_CHANNEL,
                         &ServerConfigSelectorFilterV1::kFilterVtable)
        .SkipV3()
        .Terminal();
  }
}

}  // namespace

void BuildCoreConfiguration(CoreConfiguration::Builder* builder) {
  grpc_event_engine::experimental::RegisterEventEngineChannelArgPreconditioning(
      builder);
  // The order of the handshaker registration is crucial here.
  // We want TCP connect handshaker to be registered last so that it is added
  // to the start of the handshaker list.
  RegisterEndpointInfoHandshaker(builder);
  RegisterHttpConnectClientHandshaker(builder);
  RegisterTCPConnectHandshaker(builder);
  RegisterChttp2Transport(builder);
#ifndef GRPC_MINIMAL_LB_POLICY
  RegisterPriorityLbPolicy(builder);
  RegisterOutlierDetectionLbPolicy(builder);
  RegisterWeightedTargetLbPolicy(builder);
#endif
  RegisterPickFirstLbPolicy(builder);
#ifndef GRPC_MINIMAL_LB_POLICY
  RegisterRoundRobinLbPolicy(builder);
  RegisterRingHashLbPolicy(builder);
  RegisterWeightedRoundRobinLbPolicy(builder);
#endif
  BuildClientChannelConfiguration(builder);
  SecurityRegisterHandshakerFactories(builder);
  RegisterClientAuthorityFilter(builder);
  RegisterLegacyChannelIdleFilters(builder);
  RegisterConnectedChannel(builder);
  RegisterGrpcLbPolicy(builder);
  RegisterHttpFilters(builder);
  RegisterMessageSizeFilter(builder);
  RegisterServiceConfigChannelArgFilter(builder);
  RegisterResourceQuota(builder);
  FaultInjectionFilterRegister(builder);
  RegisterDnsResolver(builder);
  RegisterSockaddrResolver(builder);
  RegisterFakeResolver(builder);
  RegisterHttpProxyMapper(builder);
  RegisterLoadBalancedCallDestination(builder);
#ifndef GRPC_NO_RLS
  RegisterRlsLbPolicy(builder);
#endif  // !GRPC_NO_RLS
  // Run last so it gets a consistent location.
  // TODO(ctiller): Is this actually necessary?
  RegisterBackendMetricFilter(builder);
  RegisterSecurityFilters(builder);
  RegisterExtraFilters(builder);
  RegisterFusedFilters(builder);
  RegisterBuiltins(builder);
}

}  // namespace grpc_core
