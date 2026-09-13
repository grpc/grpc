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

#ifndef GRPC_SRC_CORE_LOAD_BALANCING_OUTLIER_DETECTION_OUTLIER_DETECTION_METRICS_H
#define GRPC_SRC_CORE_LOAD_BALANCING_OUTLIER_DETECTION_OUTLIER_DETECTION_METRICS_H

#include "src/core/telemetry/instrument.h"

namespace grpc_core {

// InstrumentDomain for the outlier_detection.ejections_enforced counter
// (gRFC A91).  detection_method is baked into the storage key so that a
// separate storage instance exists per detection method.
class OutlierDetectionMetricsDomainEnforced final
    : public InstrumentDomain<OutlierDetectionMetricsDomainEnforced> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "outlier_detection";
  GRPC_INSTRUMENT_DOMAIN_LABELS(
      "grpc.target", "grpc.lb.backend_service", "grpc.lb.locality",
      "grpc.lb.outlier_detection.detection_method");

  static CounterHandle kEjectionsEnforced;
};

// InstrumentDomain for the outlier_detection.ejections_unenforced counter
// (gRFC A91).  Both detection_method and unenforced_reason are baked into
// the storage key.
class OutlierDetectionMetricsDomainUnenforced final
    : public InstrumentDomain<OutlierDetectionMetricsDomainUnenforced> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "outlier_detection";
  GRPC_INSTRUMENT_DOMAIN_LABELS(
      "grpc.target", "grpc.lb.backend_service", "grpc.lb.locality",
      "grpc.lb.outlier_detection.detection_method",
      "grpc.lb.outlier_detection.unenforced_reason");

  static CounterHandle kEjectionsUnenforced;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_OUTLIER_DETECTION_OUTLIER_DETECTION_METRICS_H
