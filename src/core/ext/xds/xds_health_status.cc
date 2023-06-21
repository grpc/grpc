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

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_health_status.h"

#include "absl/strings/str_cat.h"
#include "envoy/config/core/v3/health_check.upb.h"

#include "src/core/lib/gpr/useful.h"

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

bool operator<(const XdsHealthStatus& hs1, const XdsHealthStatus& hs2) {
  return hs1.status() < hs2.status();
}

}  // namespace grpc_core
