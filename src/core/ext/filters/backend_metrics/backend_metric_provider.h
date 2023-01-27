/*
 *
 * Copyright 2023 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_BACKEND_METRICS_BACKEND_METRIC_DATA_H
#define GRPC_CORE_EXT_FILTERS_BACKEND_METRICS_BACKEND_METRIC_DATA_H

#include "absl/types/optional.h"

namespace grpc_core {

struct BackendMetricData;
class BackendMetricProvider {
 public:
  virtual ~BackendMetricProvider() = default;
  // Returns nullopt when empty.
  virtual BackendMetricData GetBackendMetricData() = 0;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_BACKEND_METRICS_BACKEND_METRIC_DATA_H
