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

#include "src/core/xds/grpc/xds_http_filter_registry.h"

#include "src/core/util/grpc_check.h"
#include "src/core/xds/grpc/xds_http_fault_filter.h"
#include "src/core/xds/grpc/xds_http_gcp_authn_filter.h"
#include "src/core/xds/grpc/xds_http_rbac_filter.h"
#include "src/core/xds/grpc/xds_http_router_filter.h"
#include "src/core/xds/grpc/xds_http_stateful_session_filter.h"

namespace grpc_core {

XdsHttpFilterRegistry::XdsHttpFilterRegistry(bool register_builtins) {
  if (register_builtins) {
    RegisterFilter(std::make_unique<XdsHttpRouterFilter>());
    RegisterFilter(std::make_unique<XdsHttpFaultFilter>());
    RegisterFilter(std::make_unique<XdsHttpRbacFilter>());
    RegisterFilter(std::make_unique<XdsHttpStatefulSessionFilter>());
    RegisterFilter(std::make_unique<XdsHttpGcpAuthnFilter>());
  }
}

void XdsHttpFilterRegistry::RegisterFilter(
    std::unique_ptr<XdsHttpFilterImpl> filter) {
  GRPC_CHECK(
      registry_map_.emplace(filter->ConfigProtoName(), filter.get()).second);
  auto override_proto_name = filter->OverrideConfigProtoName();
  if (!override_proto_name.empty()) {
    GRPC_CHECK(registry_map_.emplace(override_proto_name, filter.get()).second);
  }
  owning_list_.push_back(std::move(filter));
}

const XdsHttpFilterImpl* XdsHttpFilterRegistry::GetFilterForType(
    absl::string_view proto_type_name) const {
  auto it = registry_map_.find(proto_type_name);
  if (it == registry_map_.end()) return nullptr;
  return it->second;
}

void XdsHttpFilterRegistry::PopulateSymtab(upb_DefPool* symtab) const {
  for (const auto& filter : owning_list_) {
    filter->PopulateSymtab(symtab);
  }
}

}  // namespace grpc_core
