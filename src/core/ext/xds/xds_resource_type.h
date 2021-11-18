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

#ifndef GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_H
#define GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_H

#include <grpc/support/port_platform.h>

#include <map>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/status/statusor.h"

namespace grpc_core {

class XdsResourceTypeInterface {
 public:
  // A base type for resource data.
  struct ResourceData {};

  virtual ~ResourceTypeInterface() = default;

  virtual absl::string_view type_url() const = 0;

  virtual absl::string_view v2_type_url() const = 0;

  virtual absl::StatusOr<std::unique_ptr<ResourceData>> Decode(
      absl::string_view serialized_resource, bool is_v2) const = 0;

  virtual std::string Encode(const ResourceData* resource) = 0;

  virtual bool AllResourcesRequiredInSotW() const { return false; }

  bool IsType(absl::string_view resource_type, bool* is_v2) const {
    if (resource_type == type_url()) return true;
    if (resource_type == v2_type_url()) {
      if (is_v2 != nullptr) *is_v2 = true;
      return true;
    }
    return false;
  }
};

class XdsResourceTypeRegistry {
 public:
  static XdsResourceTypeRegistry* GetOrCreate() {
    static gpr_once once = GPR_ONCE_INIT;
    gpr_once_init(&once, Create);
    return g_registry_;
  }

  const XdsResourceTypeInterface* GetType(absl::string_view resource_type) {
    auto it = resource_types_.find(resource_type);
    if (it != resource_types_.end()) return it->second.get();
    auto it2 = v2_resource_types_.find(resource_type);
    if (it2 != v2_resource_types_.end()) return it2->second.get();
    return nullptr;
  }

  void RegisterType(std::unique_ptr<XdsResourceTypeInterface> resource_type) {
    GPR_ASSERT(resource_types_.find(resource_type->type_url()) ==
               resource_types_.end());
    GPR_ASSERT(v2_resource_types_.find(resource_type->v2_type_url()) ==
               v2_resource_types_.end());
    v2_resource_types_.emplace(resource_type->v2_type_url(), resource_type.get());
    resource_types_.emplace(resource_type->type_url(), std::move(resource_type));
  }

 private:
  static void Create() { g_registry_ = new XdsResourceTypeRegistry(); }

  std::map<absl::string_view /*resource_type*/,
           std::unique_ptr<XdsResourceTypeInterface>> resource_types_;
  std::map<absl::string_view /*v2_resource_type*/, XdsResourceTypeInterface*>
      v2_resource_types_;

  static XdsResourceTypeRegistry* g_registry_ = nullptr;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_H
