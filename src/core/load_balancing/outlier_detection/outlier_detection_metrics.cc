//
// Copyright 2026 gRPC authors.
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

#include "src/core/load_balancing/outlier_detection/outlier_detection_metrics.h"

#include "src/core/telemetry/instrument.h"

namespace grpc_core {

OutlierDetectionMetricsDomainEnforced::CounterHandle
    OutlierDetectionMetricsDomainEnforced::kEjectionsEnforced =
        OutlierDetectionMetricsDomainEnforced::RegisterCounter(
            "grpc.lb.outlier_detection.ejections_enforced",
            "EXPERIMENTAL.  Enforced outlier ejections by detection method.",
            "{ejection}");

OutlierDetectionMetricsDomainUnenforced::CounterHandle
    OutlierDetectionMetricsDomainUnenforced::kEjectionsUnenforced =
        OutlierDetectionMetricsDomainUnenforced::RegisterCounter(
            "grpc.lb.outlier_detection.ejections_unenforced",
            "EXPERIMENTAL.  Unenforced outlier ejections, by detection method "
            "and the reason the ejection was not enforced.",
            "{ejection}");

}  // namespace grpc_core
