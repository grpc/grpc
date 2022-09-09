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
#endif

void grpc_register_extra_plugins() {
#ifndef GRPC_NO_XDS
  grpc_register_plugin(grpc_core::XdsClientGlobalInit,
                       grpc_core::XdsClientGlobalShutdown);
#endif
}

namespace grpc_core {
#ifndef GRPC_NO_XDS
extern void RbacFilterRegister(CoreConfiguration::Builder* builder);
extern void RegisterXdsChannelStackModifier(
    CoreConfiguration::Builder* builder);
extern void RegisterChannelDefaultCreds(CoreConfiguration::Builder* builder);
extern void RegisterXdsResolver(CoreConfiguration::Builder* builder);
extern void RegisterCloud2ProdResolver(CoreConfiguration::Builder* builder);
extern void RegisterXdsClusterManagerLbPolicy(
    CoreConfiguration::Builder* builder);
extern void RegisterXdsClusterImplLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterCdsLbPolicy(CoreConfiguration::Builder* builder);
extern void RegisterXdsClusterResolverLbPolicy(
    CoreConfiguration::Builder* builder);
extern void RegisterFileWatcherCertificateProvider(
    CoreConfiguration::Builder* builder);
#endif
void RegisterExtraFilters(CoreConfiguration::Builder* builder) {
  // Use builder to avoid unused-parameter warning.
  (void)builder;
#ifndef GRPC_NO_XDS
  // rbac_filter is being guarded with GRPC_NO_XDS to avoid a dependency on the
  // re2 library by default
  RbacFilterRegister(builder);
  RegisterXdsChannelStackModifier(builder);
  RegisterChannelDefaultCreds(builder);
  RegisterXdsResolver(builder);
  RegisterCloud2ProdResolver(builder);
  RegisterXdsClusterManagerLbPolicy(builder);
  RegisterXdsClusterImplLbPolicy(builder);
  RegisterCdsLbPolicy(builder);
  RegisterXdsClusterResolverLbPolicy(builder);
  RegisterFileWatcherCertificateProvider(builder);
#endif
}
}  // namespace grpc_core
