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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_ATTRIBUTES_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_ATTRIBUTES_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/resolver/server_address.h"

namespace grpc_core {

extern const char* kXdsLocalityNameAttributeKey;

class XdsLocalityAttribute : public ServerAddress::AttributeInterface {
 public:
  XdsLocalityAttribute(RefCountedPtr<XdsLocalityName> locality_name,
                       uint32_t weight)
      : locality_name_(std::move(locality_name)), weight_(weight) {}

  RefCountedPtr<XdsLocalityName> locality_name() const {
    return locality_name_;
  }

  uint32_t weight() const { return weight_; }

  std::unique_ptr<AttributeInterface> Copy() const override {
    return std::make_unique<XdsLocalityAttribute>(locality_name_->Ref(),
                                                  weight_);
  }

  int Cmp(const AttributeInterface* other) const override;

  std::string ToString() const override;

 private:
  RefCountedPtr<XdsLocalityName> locality_name_;
  uint32_t weight_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_ATTRIBUTES_H
