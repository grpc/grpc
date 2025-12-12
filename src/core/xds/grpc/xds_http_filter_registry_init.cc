//
// Copyright 2025 gRPC authors.
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

#include "src/core/config/core_configuration.h"
#include "src/core/xds/grpc/xds_http_fault_filter.h"
#include "src/core/xds/grpc/xds_http_filter_registry.h"
#include "src/core/xds/grpc/xds_http_gcp_authn_filter.h"
#include "src/core/xds/grpc/xds_http_rbac_filter.h"
#include "src/core/xds/grpc/xds_http_router_filter.h"
#include "src/core/xds/grpc/xds_http_stateful_session_filter.h"

namespace grpc_core {

void RegisterXdsHttpFilters(CoreConfiguration::Builder* builder) {
  auto* xds_http_filter_registry_builder = builder->xds_http_filter_registry();
  xds_http_filter_registry_builder->RegisterFilter(
      std::make_unique<XdsHttpRouterFilter>());
  xds_http_filter_registry_builder->RegisterFilter(
      std::make_unique<XdsHttpFaultFilter>());
  xds_http_filter_registry_builder->RegisterFilter(
      std::make_unique<XdsHttpRbacFilter>());
  xds_http_filter_registry_builder->RegisterFilter(
      std::make_unique<XdsHttpStatefulSessionFilter>());
  xds_http_filter_registry_builder->RegisterFilter(
      std::make_unique<XdsHttpGcpAuthnFilter>());
}

}  // namespace grpc_core
