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

#include <map>
#include <utility>
#include <vector>

#include "src/core/util/grpc_check.h"

namespace grpc_core {

void XdsHttpFilterRegistry::RegisterFilter(
    std::unique_ptr<XdsHttpFilterFactory> factory) {
  GRPC_CHECK(
      top_level_config_map_.emplace(factory->ConfigProtoName(), factory.get())
          .second);
  auto override_proto_name = factory->OverrideConfigProtoName();
  if (!override_proto_name.empty()) {
    GRPC_CHECK(override_config_map_.emplace(override_proto_name, factory.get())
                   .second);
  }
  owning_list_.push_back(std::move(factory));
}

const XdsHttpFilterFactory* XdsHttpFilterRegistry::GetFilterForTopLevelType(
    absl::string_view proto_type_name) const {
  auto it = top_level_config_map_.find(proto_type_name);
  if (it == top_level_config_map_.end()) return nullptr;
  return it->second;
}

const XdsHttpFilterFactory* XdsHttpFilterRegistry::GetFilterForOverrideType(
    absl::string_view proto_type_name) const {
  auto it = override_config_map_.find(proto_type_name);
  if (it == override_config_map_.end()) return nullptr;
  return it->second;
}

void XdsHttpFilterRegistry::PopulateSymtab(upb_DefPool* symtab) const {
  for (const auto& factory : owning_list_) {
    factory->PopulateSymtab(symtab);
  }
}

}  // namespace grpc_core
