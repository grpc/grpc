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

#include "src/core/ext/xds/xds_resource_type.h"

#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

bool XdsResourceType::IsType(absl::string_view resource_type,
                             bool* is_v2) const {
  if (resource_type == type_url()) return true;
  if (resource_type == v2_type_url()) {
    if (is_v2 != nullptr) *is_v2 = true;
    return true;
  }
  return false;
}

XdsResourceTypeRegistry* XdsResourceTypeRegistry::GetOrCreate() {
  static XdsResourceTypeRegistry* registry = new XdsResourceTypeRegistry();
  return registry;
}

const XdsResourceType* XdsResourceTypeRegistry::GetType(
    absl::string_view resource_type) {
  auto it = resource_types_.find(resource_type);
  if (it != resource_types_.end()) return it->second.get();
  auto it2 = v2_resource_types_.find(resource_type);
  if (it2 != v2_resource_types_.end()) return it2->second;
  return nullptr;
}

void XdsResourceTypeRegistry::RegisterType(
    std::unique_ptr<XdsResourceType> resource_type) {
  GPR_ASSERT(resource_types_.find(resource_type->type_url()) ==
             resource_types_.end());
  GPR_ASSERT(v2_resource_types_.find(resource_type->v2_type_url()) ==
             v2_resource_types_.end());
  v2_resource_types_.emplace(resource_type->v2_type_url(), resource_type.get());
  resource_types_.emplace(resource_type->type_url(), std::move(resource_type));
}

void XdsResourceTypeRegistry::ForEach(
    std::function<void(const XdsResourceType*)> func) {
  for (const auto& p : resource_types_) {
    func(p.second.get());
  }
}

}  // namespace grpc_core
