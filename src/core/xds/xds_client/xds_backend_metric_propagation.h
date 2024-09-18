//
// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_BACKEND_METRIC_PROPAGATION_H
#define GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_BACKEND_METRIC_PROPAGATION_H

#include <string>

#include "absl/container/flat_hash_set.h"

#include "src/core/lib/gprpp/ref_counted.h"

namespace grpc_core {

struct BackendMetricPropagation
    : public RefCountedPtr<BackendMetricPropagation> {
  static constexpr uint8_t kCpuUtilization = 1;
  static constexpr uint8_t kMemUtilization = 2;
  static constexpr uint8_t kApplicationUtilization = 4;
  static constexpr uint8_t kNamedMetricsAll = 8;

  uint8_t propagation_bits = 0;
  absl::flat_hash_set<std::string> named_metric_keys;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_BACKEND_METRIC_PROPAGATION_H