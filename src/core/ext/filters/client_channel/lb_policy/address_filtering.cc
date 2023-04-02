//
// Copyright 2020 gRPC authors.
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

#include "src/core/ext/filters/client_channel/lb_policy/address_filtering.h"

#include <stddef.h>

#include <algorithm>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#define GRPC_ARG_HIERARCHICAL_PATH "grpc.internal.address.hierarchical_path"

namespace grpc_core {

const char* kHierarchicalPathAttributeKey = "hierarchical_path";

namespace {

class HierarchicalPathAttribute : public ServerAddress::AttributeInterface {
 public:
  explicit HierarchicalPathAttribute(std::vector<std::string> path)
      : path_(std::move(path)) {}

  std::unique_ptr<AttributeInterface> Copy() const override {
    return std::make_unique<HierarchicalPathAttribute>(path_);
  }

  int Cmp(const AttributeInterface* other) const override {
    const std::vector<std::string>& other_path =
        static_cast<const HierarchicalPathAttribute*>(other)->path_;
    for (size_t i = 0; i < path_.size(); ++i) {
      if (other_path.size() == i) return 1;
      int r = path_[i].compare(other_path[i]);
      if (r != 0) return r;
    }
    if (other_path.size() > path_.size()) return -1;
    return 0;
  }

  std::string ToString() const override {
    return absl::StrCat("[", absl::StrJoin(path_, ", "), "]");
  }

  const std::vector<std::string>& path() const { return path_; }

 private:
  std::vector<std::string> path_;
};

}  // namespace

std::unique_ptr<ServerAddress::AttributeInterface>
MakeHierarchicalPathAttribute(std::vector<std::string> path) {
  return std::make_unique<HierarchicalPathAttribute>(std::move(path));
}

absl::StatusOr<HierarchicalAddressMap> MakeHierarchicalAddressMap(
    const absl::StatusOr<ServerAddressList>& addresses) {
  if (!addresses.ok()) return addresses.status();
  HierarchicalAddressMap result;
  for (const ServerAddress& address : *addresses) {
    const HierarchicalPathAttribute* path_attribute =
        static_cast<const HierarchicalPathAttribute*>(
            address.GetAttribute(kHierarchicalPathAttributeKey));
    if (path_attribute == nullptr) continue;
    const std::vector<std::string>& path = path_attribute->path();
    auto it = path.begin();
    ServerAddressList& target_list = result[*it];
    std::unique_ptr<HierarchicalPathAttribute> new_attribute;
    ++it;
    if (it != path.end()) {
      std::vector<std::string> remaining_path(it, path.end());
      new_attribute = std::make_unique<HierarchicalPathAttribute>(
          std::move(remaining_path));
    }
    target_list.emplace_back(address.WithAttribute(
        kHierarchicalPathAttributeKey, std::move(new_attribute)));
  }
  return result;
}

}  // namespace grpc_core
