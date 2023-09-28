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

#ifndef GRPC_SRC_CORE_EXT_XDS_XDS_HEALTH_STATUS_H
#define GRPC_SRC_CORE_EXT_XDS_XDS_HEALTH_STATUS_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

#include "src/core/lib/resolver/server_address.h"

// Channel arg key for xDS health status.
// Value is an XdsHealthStatus::HealthStatus enum.
#define GRPC_ARG_XDS_HEALTH_STATUS \
  GRPC_ARG_NO_SUBCHANNEL_PREFIX "xds_health_status"

namespace grpc_core {

class XdsHealthStatus {
 public:
  enum HealthStatus { kUnknown, kHealthy, kDraining };

  // Returns an XdsHealthStatus for supported enum values, else nullopt.
  static absl::optional<XdsHealthStatus> FromUpb(uint32_t status);
  static absl::optional<XdsHealthStatus> FromString(absl::string_view status);

  explicit XdsHealthStatus(HealthStatus status) : status_(status) {}

  HealthStatus status() const { return status_; }

  bool operator==(const XdsHealthStatus& other) const {
    return status_ == other.status_;
  }

  const char* ToString() const;

 private:
  HealthStatus status_;
};

class XdsHealthStatusSet {
 public:
  XdsHealthStatusSet() = default;

  explicit XdsHealthStatusSet(absl::Span<const XdsHealthStatus> statuses) {
    for (XdsHealthStatus status : statuses) {
      Add(status);
    }
  }

  bool operator==(const XdsHealthStatusSet& other) const {
    return status_mask_ == other.status_mask_;
  }

  void Clear() { status_mask_ = 0; }

  void Add(XdsHealthStatus status) { status_mask_ |= (0x1 << status.status()); }

  bool Contains(XdsHealthStatus status) const {
    return status_mask_ & (0x1 << status.status());
  }

 private:
  int status_mask_ = 0;
};

bool operator<(const XdsHealthStatus& hs1, const XdsHealthStatus& hs2);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_XDS_XDS_HEALTH_STATUS_H
