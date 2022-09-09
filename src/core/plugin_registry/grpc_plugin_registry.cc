/*
 *
 * Copyright 2016 gRPC authors.
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

#include <grpc/grpc.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/builtins.h"
#include "src/core/lib/transport/http_connect_handshaker.h"
#include "src/core/lib/transport/tcp_connect_handshaker.h"

extern void grpc_register_extra_plugins(void);

void grpc_resolver_dns_ares_init(void);
void grpc_resolver_dns_ares_shutdown(void);

void grpc_register_built_in_plugins(void) {
  grpc_register_plugin(grpc_resolver_dns_ares_init,
                       grpc_resolver_dns_ares_shutdown);
  grpc_register_extra_plugins();
}

namespace grpc_core {

extern void BuildClientChannelConfiguration(
    CoreConfiguration::Builder* builder);
extern void SecurityRegisterHandshakerFactories(
    CoreConfiguration::Builder* builder);
extern void RegisterClientAuthorityFilter(CoreConfiguration::Builder* builder);
extern void RegisterChannelIdleFilters(CoreConfiguration::Builder* builder);
extern void RegisterDeadlineFilter(CoreConfiguration::Builder* builder);
extern void RegisterGrpcLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterHttpFilters(CoreConfiguration::Builder* builder);
extern void RegisterMessageSizeFilter(CoreConfiguration::Builder* builder);
extern void RegisterSecurityFilters(CoreConfiguration::Builder* builder);
extern void RegisterServiceConfigChannelArgFilter(
    CoreConfiguration::Builder* builder);
extern void RegisterExtraFilters(CoreConfiguration::Builder* builder);
extern void RegisterResourceQuota(CoreConfiguration::Builder* builder);
extern void FaultInjectionFilterRegister(CoreConfiguration::Builder* builder);
extern void RegisterNativeDnsResolver(CoreConfiguration::Builder* builder);
extern void RegisterAresDnsResolver(CoreConfiguration::Builder* builder);
extern void RegisterSockaddrResolver(CoreConfiguration::Builder* builder);
extern void RegisterFakeResolver(CoreConfiguration::Builder* builder);
extern void RegisterPriorityLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterOutlierDetectionLbPolicy(
    CoreConfiguration::Builder* builder);
extern void RegisterWeightedTargetLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterPickFirstLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterRoundRobinLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterRingHashLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterHttpProxyMapper(CoreConfiguration::Builder* builder);
#ifndef GRPC_NO_RLS
extern void RegisterRlsLbPolicy(CoreConfiguration::Builder* builder);
#endif  // !GRPC_NO_RLS
#ifdef GPR_SUPPORT_BINDER_TRANSPORT
extern void RegisterBinderResolver(CoreConfiguration::Builder* builder);
#endif

void BuildCoreConfiguration(CoreConfiguration::Builder* builder) {
  // The order of the handshaker registration is crucial here.
  // We want TCP connect handshaker to be registered last so that it is added to
  // the start of the handshaker list.
  RegisterHttpConnectHandshaker(builder);
  RegisterTCPConnectHandshaker(builder);
  RegisterPriorityLbPolicy(builder);
  RegisterOutlierDetectionLbPolicy(builder);
  RegisterWeightedTargetLbPolicy(builder);
  RegisterPickFirstLbPolicy(builder);
  RegisterRoundRobinLbPolicy(builder);
  RegisterRingHashLbPolicy(builder);
  BuildClientChannelConfiguration(builder);
  SecurityRegisterHandshakerFactories(builder);
  RegisterClientAuthorityFilter(builder);
  RegisterChannelIdleFilters(builder);
  RegisterGrpcLbPolicy(builder);
  RegisterHttpFilters(builder);
  RegisterDeadlineFilter(builder);
  RegisterMessageSizeFilter(builder);
  RegisterServiceConfigChannelArgFilter(builder);
  RegisterResourceQuota(builder);
  FaultInjectionFilterRegister(builder);
  RegisterAresDnsResolver(builder);
  RegisterNativeDnsResolver(builder);
  RegisterSockaddrResolver(builder);
  RegisterFakeResolver(builder);
  RegisterHttpProxyMapper(builder);
#ifdef GPR_SUPPORT_BINDER_TRANSPORT
  RegisterBinderResolver(builder);
#endif
#ifndef GRPC_NO_RLS
  RegisterRlsLbPolicy(builder);
#endif  // !GRPC_NO_RLS
  // Run last so it gets a consistent location.
  // TODO(ctiller): Is this actually necessary?
  RegisterSecurityFilters(builder);
  RegisterExtraFilters(builder);
  RegisterBuiltins(builder);
}

}  // namespace grpc_core
