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

#ifndef GRPC_CORE_LIB_RESOLVER_RESOLVER_ATTRIBUTES_H
#define GRPC_CORE_LIB_RESOLVER_RESOLVER_ATTRIBUTES_H

#include <grpc/support/port_platform.h>

#include <map>
#include <memory>
#include <vector>

#include "src/core/lib/gprpp/unique_type_name.h"

namespace grpc_core {

class ResolverAttributeMap {
 public:
  // Base class for resolver-supplied attributes.
  class AttributeInterface {
   public:
    virtual ~AttributeInterface() = default;

    // The type name for this attribute.
    // There can be only one attribute of a given type in a given AttributeMap.
    virtual UniqueTypeName type() const = 0;

    // Creates a copy of the attribute.
    virtual std::unique_ptr<AttributeInterface> Copy() const = 0;

    // Compares this attribute with another.
    virtual int Compare(const AttributeInterface* other) const = 0;

    // Returns a human-readable representation of the attribute.
    virtual std::string ToString() const = 0;
  };

  ResolverAttributeMap() = default;

  explicit ResolverAttributeMap(
      std::vector<std::unique_ptr<AttributeInterface>> attributes);

  // Copyable.
  ResolverAttributeMap(const ResolverAttributeMap& other);
  ResolverAttributeMap& operator=(const ResolverAttributeMap& other);

  // Movable.
  ResolverAttributeMap(ResolverAttributeMap&& other) noexcept;
  ResolverAttributeMap& operator=(ResolverAttributeMap&& other) noexcept;

  int Compare(const ResolverAttributeMap& other) const;

  // Returns the attribute of the specified type, or null if not present.
  const AttributeInterface* Get(UniqueTypeName type) const;

  // Adds attribute to the map.
  void Set(std::unique_ptr<AttributeInterface> attribute);

  // Removes the attribute from the map.
  void Remove(UniqueTypeName type);

  std::string ToString() const;

  bool empty() const { return map_.empty(); }

 private:
  std::map<UniqueTypeName, std::unique_ptr<AttributeInterface>> map_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_RESOLVER_RESOLVER_ATTRIBUTES_H
