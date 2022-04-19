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
#include "absl/types/optional.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_core {

struct OutlierDetectionConfig {
  Duration interval = Duration::Infinity();
  Duration base_ejection_time = Duration::Milliseconds(30000);
  Duration max_ejection_time = Duration::Milliseconds(30000);
  uint32_t max_ejection_percent = 10;
  struct SuccessRateEjection {
    uint32_t stdev_factor = 1900;
    uint32_t enforcement_percentage = 0;
    uint32_t minimum_hosts = 5;
    uint32_t request_volume = 100;
  };
  struct FailurePercentageEjection {
    uint32_t threshold = 85;
    uint32_t enforcement_percentage = 0;
    uint32_t minimum_hosts = 5;
    uint32_t request_volume = 50;
  };
  absl::optional<SuccessRateEjection> success_rate_ejection;
  absl::optional<FailurePercentageEjection> failure_percentage_ejection;
};
}  // namespace grpc_core
