//
// Copyright 2018 gRPC authors.
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

#include "src/core/xds/grpc/xds_endpoint.h"

#include <string>

#include "src/core/util/string.h"

namespace grpc_core {

std::string XdsEndpointResource::Priority::Locality::ToString() const {
  std::string result = "{name=";
  StrAppend(result, name->human_readable_string().as_string_view());
  StrAppend(result, ", lb_weight=");
  StrAppend(result, std::to_string(lb_weight));
  StrAppend(result, ", endpoints=[");
  bool is_first = true;
  for (const EndpointAddresses& endpoint : endpoints) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, endpoint.ToString());
    is_first = false;
  }
  StrAppend(result, "]}");
  return result;
}

bool XdsEndpointResource::Priority::operator==(const Priority& other) const {
  if (localities.size() != other.localities.size()) return false;
  auto it1 = localities.begin();
  auto it2 = other.localities.begin();
  while (it1 != localities.end()) {
    if (*it1->first != *it2->first) return false;
    if (it1->second != it2->second) return false;
    ++it1;
    ++it2;
  }
  return true;
}

std::string XdsEndpointResource::Priority::ToString() const {
  std::string result = "[";
  bool is_first = true;
  for (const auto& [_, locality] : localities) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, locality.ToString());
    is_first = false;
  }
  StrAppend(result, "]");
  return result;
}

bool XdsEndpointResource::DropConfig::ShouldDrop(
    const std::string** category_name) {
  for (const auto& drop_category : drop_category_list_) {
    // Generate a random number in [0, 1000000).
    const uint32_t random = [&]() {
      MutexLock lock(&mu_);
      return absl::Uniform<uint32_t>(bit_gen_, 0, 1000000);
    }();
    if (random < drop_category.parts_per_million) {
      *category_name = &drop_category.name;
      return true;
    }
  }
  return false;
}

std::string XdsEndpointResource::DropConfig::ToString() const {
  std::string result = "{[";
  bool is_first = true;
  for (const DropCategory& category : drop_category_list_) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, category.name);
    StrAppend(result, "=");
    StrAppend(result, std::to_string(category.parts_per_million));
    is_first = false;
  }
  StrAppend(result, "], drop_all=");
  StrAppend(result, drop_all_ ? "true" : "false");
  StrAppend(result, "}");
  return result;
}

std::string XdsEndpointResource::ToString() const {
  std::string result = "priorities=[";
  for (size_t i = 0; i < priorities.size(); ++i) {
    if (i > 0) StrAppend(result, ", ");
    StrAppend(result, "priority ");
    StrAppend(result, std::to_string(i));
    StrAppend(result, ": ");
    StrAppend(result, priorities[i].ToString());
  }
  StrAppend(result, "], drop_config=");
  StrAppend(result,
            drop_config == nullptr ? "<null>" : drop_config->ToString());
  return result;
}

}  // namespace grpc_core
