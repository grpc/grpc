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

#include <map>
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include "src/core/ext/xds/upb_utils.h"

#ifndef GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_H
#define GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_H

namespace grpc_core {

class XdsResourceType {
 public:
  // A base type for resource data.
  // Subclasses will extend this, and their DecodeResults will be
  // downcastable to their extended type.
  struct ResourceData {};

  // Result returned by Decode().
  struct DecodeResult {
    std::string name;
    absl::StatusOr<std::unique_ptr<ResourceData>> resource;
  };

  virtual ~XdsResourceType() = default;

  // Returns v3 resource type.
  virtual absl::string_view type_url() const = 0;

  // Returns v2 resource type.
  virtual absl::string_view v2_type_url() const = 0;

  // Decodes and validates a serialized resource proto.
  // If the resource fails protobuf deserialization, returns non-OK status.
  // If the deserialized resource fails validation, returns a DecodeResult
  // whose resource field is set to a non-OK status.
  // Otherwise, returns a DecodeResult with a valid resource.
  virtual absl::StatusOr<DecodeResult> Decode(
      const XdsEncodingContext& context, absl::string_view serialized_resource,
      bool is_v2) const = 0;

  // Returns true if r1 and r2 are equal.
  // Must be invoked only on resources returned by this object's Decode() method.
  virtual bool ResourcesEqual(const ResourceData* r1, const ResourceData* r2)
      const = 0;

  // Returns a copy of resource.
  // Must be invoked only on resources returned by this object's Decode() method.
  virtual std::unique_ptr<ResourceData> CopyResource(const ResourceData* resource)
      const = 0;

  // Indicates whether the resource type requires that all resources must
  // be present in every SotW response from the server.  If true, a
  // response that does not include a previously seen resource will be
  // interpreted as a deletion of that resource.
  virtual bool AllResourcesRequiredInSotW() const { return false; }

  // Convenient method for checking if a resource type matches this type.
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

  const XdsResourceType* GetType(absl::string_view resource_type) {
    auto it = resource_types_.find(resource_type);
    if (it != resource_types_.end()) return it->second.get();
    auto it2 = v2_resource_types_.find(resource_type);
    if (it2 != v2_resource_types_.end()) return it2->second;
    return nullptr;
  }

  void RegisterType(std::unique_ptr<XdsResourceType> resource_type) {
    GPR_ASSERT(resource_types_.find(resource_type->type_url()) ==
               resource_types_.end());
    GPR_ASSERT(v2_resource_types_.find(resource_type->v2_type_url()) ==
               v2_resource_types_.end());
    v2_resource_types_.emplace(resource_type->v2_type_url(), resource_type.get());
    resource_types_.emplace(resource_type->type_url(), std::move(resource_type));
  }

 private:
  static void Create() { g_registry_ = new XdsResourceTypeRegistry(); }

  std::map<absl::string_view /*resource_type*/, std::unique_ptr<XdsResourceType>>
      resource_types_;
  std::map<absl::string_view /*v2_resource_type*/, XdsResourceType*>
      v2_resource_types_;

  static XdsResourceTypeRegistry* g_registry_;
};

// FIXME: maybe move the ResourceName code back to xds_client.cc?

struct XdsResourceName {
  std::string authority;
  std::string id;
};

absl::StatusOr<XdsResourceName> ParseXdsResourceName(absl::string_view name,
                                                     const XdsResourceType* type);

std::string ConstructFullXdsResourceName(absl::string_view authority,
                                         absl::string_view resource_type,
                                         absl::string_view id);

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_H
