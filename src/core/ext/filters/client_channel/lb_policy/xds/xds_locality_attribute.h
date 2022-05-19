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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_LOCALITY_ATTRIBUTE_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_LOCALITY_ATTRIBUTE_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"

#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/resolver/resolver_attributes.h"

namespace grpc_core {

class XdsLocalityAttribute : public ResolverAttributeMap::AttributeInterface {
 public:
  explicit XdsLocalityAttribute(RefCountedPtr<XdsLocalityName> locality_name)
      : locality_name_(std::move(locality_name)) {}

  static UniqueTypeName Type();

  UniqueTypeName type() const override { return Type(); }

  RefCountedPtr<XdsLocalityName> locality_name() const {
    return locality_name_;
  }

  std::unique_ptr<AttributeInterface> Copy() const override {
    return absl::make_unique<XdsLocalityAttribute>(locality_name_->Ref());
  }

  int Compare(const AttributeInterface* other) const override {
    const auto* other_locality_attr =
        static_cast<const XdsLocalityAttribute*>(other);
    return locality_name_->Compare(*other_locality_attr->locality_name_);
  }

  std::string ToString() const override {
    return locality_name_->AsHumanReadableString();
  }

 private:
  RefCountedPtr<XdsLocalityName> locality_name_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_LOCALITY_ATTRIBUTE_H
