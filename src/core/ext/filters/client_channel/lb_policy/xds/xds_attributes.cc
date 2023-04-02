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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_attributes.h"

#include "absl/strings/str_cat.h"

#include "src/core/lib/gpr/useful.h"

namespace grpc_core {

const char* kXdsLocalityNameAttributeKey = "xds_locality_name";

int XdsLocalityAttribute::Cmp(const AttributeInterface* other) const {
  const auto* other_locality_attr =
      static_cast<const XdsLocalityAttribute*>(other);
  int r = locality_name_->Compare(*other_locality_attr->locality_name_);
  if (r != 0) return r;
  return QsortCompare(weight_, other_locality_attr->weight_);
}

std::string XdsLocalityAttribute::ToString() const {
  return absl::StrCat("{name=", locality_name_->AsHumanReadableString(),
                      ", weight=", weight_, "}");
}

}  // namespace grpc_core
