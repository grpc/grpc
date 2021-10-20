//
// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/builtins.h"

#ifndef GRPC_NO_XDS
namespace grpc_core {
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

void grpc_register_extra_plugins() {
#ifndef GRPC_NO_XDS
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
}
