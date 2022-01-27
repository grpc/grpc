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

void grpc_chttp2_plugin_init(void);
void grpc_chttp2_plugin_shutdown(void);
void grpc_client_channel_init(void);
void grpc_client_channel_shutdown(void);
void grpc_resolver_fake_init(void);
void grpc_resolver_fake_shutdown(void);
void grpc_lb_policy_grpclb_init(void);
void grpc_lb_policy_grpclb_shutdown(void);
void grpc_lb_policy_priority_init(void);
void grpc_lb_policy_priority_shutdown(void);
void grpc_lb_policy_weighted_target_init(void);
void grpc_lb_policy_weighted_target_shutdown(void);
void grpc_lb_policy_pick_first_init(void);
void grpc_lb_policy_pick_first_shutdown(void);
void grpc_lb_policy_round_robin_init(void);
void grpc_lb_policy_round_robin_shutdown(void);
void grpc_resolver_dns_ares_init(void);
void grpc_resolver_dns_ares_shutdown(void);
void grpc_resolver_dns_native_init(void);
void grpc_resolver_dns_native_shutdown(void);
void grpc_resolver_sockaddr_init(void);
void grpc_resolver_sockaddr_shutdown(void);
void grpc_message_size_filter_init(void);
void grpc_message_size_filter_shutdown(void);
namespace grpc_core {
void FaultInjectionFilterInit(void);
void FaultInjectionFilterShutdown(void);
void GrpcLbPolicyRingHashInit(void);
void GrpcLbPolicyRingHashShutdown(void);
#ifndef GRPC_NO_RLS
void RlsLbPluginInit();
void RlsLbPluginShutdown();
#endif  // !GRPC_NO_RLS
void ServiceConfigParserInit(void);
void ServiceConfigParserShutdown(void);
}  // namespace grpc_core

#ifndef GRPC_NO_XDS
namespace grpc_core {
void RbacFilterInit(void);
void RbacFilterShutdown(void);
void XdsClientGlobalInit();
void XdsClientGlobalShutdown();
}  // namespace grpc_core
void grpc_certificate_provider_registry_init(void);
void grpc_certificate_provider_registry_shutdown(void);
namespace grpc_core {
void FileWatcherCertificateProviderInit();
void FileWatcherCertificateProviderShutdown();
}  // namespace grpc_core
void grpc_lb_policy_cds_init(void);
void grpc_lb_policy_cds_shutdown(void);
void grpc_lb_policy_xds_cluster_impl_init(void);
void grpc_lb_policy_xds_cluster_impl_shutdown(void);
void grpc_lb_policy_xds_cluster_resolver_init(void);
void grpc_lb_policy_xds_cluster_resolver_shutdown(void);
void grpc_lb_policy_xds_cluster_manager_init(void);
void grpc_lb_policy_xds_cluster_manager_shutdown(void);
void grpc_resolver_xds_init(void);
void grpc_resolver_xds_shutdown(void);
namespace grpc_core {
void GoogleCloud2ProdResolverInit();
void GoogleCloud2ProdResolverShutdown();
}  // namespace grpc_core
#endif

#ifdef GPR_SUPPORT_BINDER_TRANSPORT
void grpc_resolver_binder_init(void);
void grpc_resolver_binder_shutdown(void);
#endif

void grpc_register_built_in_plugins(void) {
  grpc_register_plugin(grpc_chttp2_plugin_init, grpc_chttp2_plugin_shutdown);
  grpc_register_plugin(grpc_core::ServiceConfigParserInit,
                       grpc_core::ServiceConfigParserShutdown);
  grpc_register_plugin(grpc_client_channel_init, grpc_client_channel_shutdown);
  grpc_register_plugin(grpc_resolver_fake_init, grpc_resolver_fake_shutdown);
  grpc_register_plugin(grpc_lb_policy_grpclb_init,
                       grpc_lb_policy_grpclb_shutdown);
#ifndef GRPC_NO_RLS
  grpc_register_plugin(grpc_core::RlsLbPluginInit,
                       grpc_core::RlsLbPluginShutdown);
#endif  // !GRPC_NO_RLS
  grpc_register_plugin(grpc_lb_policy_priority_init,
                       grpc_lb_policy_priority_shutdown);
  grpc_register_plugin(grpc_lb_policy_weighted_target_init,
                       grpc_lb_policy_weighted_target_shutdown);
  grpc_register_plugin(grpc_lb_policy_pick_first_init,
                       grpc_lb_policy_pick_first_shutdown);
  grpc_register_plugin(grpc_lb_policy_round_robin_init,
                       grpc_lb_policy_round_robin_shutdown);
  grpc_register_plugin(grpc_core::GrpcLbPolicyRingHashInit,
                       grpc_core::GrpcLbPolicyRingHashShutdown);
  grpc_register_plugin(grpc_resolver_dns_ares_init,
                       grpc_resolver_dns_ares_shutdown);
  grpc_register_plugin(grpc_resolver_dns_native_init,
                       grpc_resolver_dns_native_shutdown);
  grpc_register_plugin(grpc_resolver_sockaddr_init,
                       grpc_resolver_sockaddr_shutdown);
  grpc_register_plugin(grpc_message_size_filter_init,
                       grpc_message_size_filter_shutdown);
  grpc_register_plugin(grpc_core::FaultInjectionFilterInit,
                       grpc_core::FaultInjectionFilterShutdown);
#ifndef GRPC_NO_XDS
  // rbac_filter is being guarded with GRPC_NO_XDS to avoid a dependency on the re2 library by default
  grpc_register_plugin(grpc_core::RbacFilterInit, grpc_core::RbacFilterShutdown);
  grpc_register_plugin(grpc_core::XdsClientGlobalInit,
                       grpc_core::XdsClientGlobalShutdown);
  grpc_register_plugin(grpc_certificate_provider_registry_init,
                       grpc_certificate_provider_registry_shutdown);
  grpc_register_plugin(grpc_core::FileWatcherCertificateProviderInit,
                       grpc_core::FileWatcherCertificateProviderShutdown);
  grpc_register_plugin(grpc_lb_policy_cds_init, grpc_lb_policy_cds_shutdown);
  grpc_register_plugin(grpc_lb_policy_xds_cluster_impl_init,
                       grpc_lb_policy_xds_cluster_impl_shutdown);
  grpc_register_plugin(grpc_lb_policy_xds_cluster_resolver_init,
                       grpc_lb_policy_xds_cluster_resolver_shutdown);
  grpc_register_plugin(grpc_lb_policy_xds_cluster_manager_init,
                       grpc_lb_policy_xds_cluster_manager_shutdown);
  grpc_register_plugin(grpc_resolver_xds_init, grpc_resolver_xds_shutdown);
  grpc_register_plugin(grpc_core::GoogleCloud2ProdResolverInit,
                       grpc_core::GoogleCloud2ProdResolverShutdown);
#endif

#ifdef GPR_SUPPORT_BINDER_TRANSPORT
  grpc_register_plugin(grpc_resolver_binder_init,
                       grpc_resolver_binder_shutdown);
#endif
}

namespace grpc_core {

extern void BuildClientChannelConfiguration(
    CoreConfiguration::Builder* builder);
extern void SecurityRegisterHandshakerFactories(
    CoreConfiguration::Builder* builder);
extern void RegisterClientAuthorityFilter(CoreConfiguration::Builder* builder);
extern void RegisterClientIdleFilter(CoreConfiguration::Builder* builder);
extern void RegisterDeadlineFilter(CoreConfiguration::Builder* builder);
extern void RegisterGrpcLbLoadReportingFilter(
    CoreConfiguration::Builder* builder);
extern void RegisterHttpFilters(CoreConfiguration::Builder* builder);
extern void RegisterMaxAgeFilter(CoreConfiguration::Builder* builder);
extern void RegisterMessageSizeFilter(CoreConfiguration::Builder* builder);
extern void RegisterSecurityFilters(CoreConfiguration::Builder* builder);
extern void RegisterServiceConfigChannelArgFilter(
    CoreConfiguration::Builder* builder);
extern void RegisterResourceQuota(CoreConfiguration::Builder* builder);
#ifndef GRPC_NO_XDS
extern void RegisterXdsChannelStackModifier(
    CoreConfiguration::Builder* builder);
#endif

void BuildCoreConfiguration(CoreConfiguration::Builder* builder) {
  BuildClientChannelConfiguration(builder);
  SecurityRegisterHandshakerFactories(builder);
  RegisterClientAuthorityFilter(builder);
  RegisterClientIdleFilter(builder);
  RegisterGrpcLbLoadReportingFilter(builder);
  RegisterHttpFilters(builder);
  RegisterMaxAgeFilter(builder);
  RegisterDeadlineFilter(builder);
  RegisterMessageSizeFilter(builder);
  RegisterServiceConfigChannelArgFilter(builder);
  RegisterResourceQuota(builder);
#ifndef GRPC_NO_XDS
  RegisterXdsChannelStackModifier(builder);
#endif
  // Run last so it gets a consistent location.
  // TODO(ctiller): Is this actually necessary?
  RegisterSecurityFilters(builder);
  RegisterBuiltins(builder);
}

}  // namespace grpc_core
