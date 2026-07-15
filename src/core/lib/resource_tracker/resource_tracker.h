// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_RESOURCE_TRACKER_RESOURCE_TRACKER_H
#define GRPC_SRC_CORE_LIB_RESOURCE_TRACKER_RESOURCE_TRACKER_H

#include <grpc/support/port_platform.h>

#include <string>
#include <vector>

#include "absl/status/statusor.h"

namespace grpc_core {

// Interface for tracking and retrieving resource usage metrics.
class ResourceTracker {
 public:
  virtual ~ResourceTracker() = default;

  static ResourceTracker* Get();
  static void Set(ResourceTracker* tracker);

  // Returns a list of metric names that this tracker can provide.
  virtual std::vector<std::string> GetMetrics() const = 0;

  // Retrieves the current value of a specific metric.
  // Returns NotFoundError if the metric_name is not supported.
  virtual absl::StatusOr<double> GetMetricValue(
      const std::string& metric_name) const = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_RESOURCE_TRACKER_RESOURCE_TRACKER_H
