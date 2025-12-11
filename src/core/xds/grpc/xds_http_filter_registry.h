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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_HTTP_FILTER_REGISTRY_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_HTTP_FILTER_REGISTRY_H

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "src/core/xds/grpc/xds_http_filter.h"
#include "upb/reflection/def.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class XdsHttpFilterRegistry final {
 public:
  XdsHttpFilterRegistry() = default;

  // Not copyable.
  XdsHttpFilterRegistry(const XdsHttpFilterRegistry&) = delete;
  XdsHttpFilterRegistry& operator=(const XdsHttpFilterRegistry&) = delete;

  // Movable.
  XdsHttpFilterRegistry(XdsHttpFilterRegistry&& other) noexcept
      : owning_list_(std::move(other.owning_list_)),
        registry_map_(std::move(other.registry_map_)) {}
  XdsHttpFilterRegistry& operator=(XdsHttpFilterRegistry&& other) noexcept {
    owning_list_ = std::move(other.owning_list_);
    registry_map_ = std::move(other.registry_map_);
    return *this;
  }

  void RegisterFilter(std::unique_ptr<XdsHttpFilterImpl> filter);

  const XdsHttpFilterImpl* GetFilterForType(
      absl::string_view proto_type_name) const;

  void PopulateSymtab(upb_DefPool* symtab) const;

 private:
  std::vector<std::unique_ptr<XdsHttpFilterImpl>> owning_list_;
  std::map<absl::string_view, XdsHttpFilterImpl*> registry_map_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_HTTP_FILTER_REGISTRY_H
