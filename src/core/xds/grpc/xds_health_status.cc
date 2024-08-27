//
// Copyright 2022 gRPC authors.
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

#include "src/core/xds/grpc/xds_health_status.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "envoy/config/core/v3/health_check.upb.h"

namespace grpc_core {

absl::optional<XdsHealthStatus> XdsHealthStatus::FromUpb(uint32_t status) {
  switch (status) {
    case envoy_config_core_v3_UNKNOWN:
      return XdsHealthStatus(kUnknown);
    case envoy_config_core_v3_HEALTHY:
      return XdsHealthStatus(kHealthy);
    case envoy_config_core_v3_DRAINING:
      return XdsHealthStatus(kDraining);
    default:
      return absl::nullopt;
  }
}

absl::optional<XdsHealthStatus> XdsHealthStatus::FromString(
    absl::string_view status) {
  if (status == "UNKNOWN") return XdsHealthStatus(kUnknown);
  if (status == "HEALTHY") return XdsHealthStatus(kHealthy);
  if (status == "DRAINING") return XdsHealthStatus(kDraining);
  return absl::nullopt;
}

const char* XdsHealthStatus::ToString() const {
  switch (status_) {
    case kUnknown:
      return "UNKNOWN";
    case kHealthy:
      return "HEALTHY";
    case kDraining:
      return "DRAINING";
    default:
      return "<INVALID>";
  }
}

std::string XdsHealthStatusSet::ToString() const {
  std::vector<const char*> set;
  set.reserve(3);
  for (const auto& status :
       {XdsHealthStatus::kUnknown, XdsHealthStatus::kHealthy,
        XdsHealthStatus::kDraining}) {
    const XdsHealthStatus health_status(status);
    if (Contains(health_status)) set.push_back(health_status.ToString());
  }
  return absl::StrCat("{", absl::StrJoin(set, ", "), "}");
}

}  // namespace grpc_core
