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

#ifndef GRPC_SRC_CORE_LOAD_BALANCING_WEIGHTED_ROUND_ROBIN_WEIGHTED_ROUND_ROBIN_H
#define GRPC_SRC_CORE_LOAD_BALANCING_WEIGHTED_ROUND_ROBIN_WEIGHTED_ROUND_ROBIN_H

#include <grpc/support/port_platform.h>

#include <string>
#include <vector>

#include "src/core/load_balancing/lb_policy.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

bool WrrCustomMetricsEnabled();

class WeightedRoundRobinConfig final : public LoadBalancingPolicy::Config {
 public:
  struct ParsedMetric {
    enum class Type {
      kCpu,
      kMem,
      kApplication,
      kNamedMetric,
      kUtilization,
    };
    Type type;
    std::string name;
  };

  WeightedRoundRobinConfig() = default;

  WeightedRoundRobinConfig(const WeightedRoundRobinConfig&) = delete;
  WeightedRoundRobinConfig& operator=(const WeightedRoundRobinConfig&) = delete;

  WeightedRoundRobinConfig(WeightedRoundRobinConfig&&) = delete;
  WeightedRoundRobinConfig& operator=(WeightedRoundRobinConfig&&) = delete;

  absl::string_view name() const override;

  bool enable_oob_load_report() const { return enable_oob_load_report_; }
  Duration oob_reporting_period() const { return oob_reporting_period_; }
  Duration blackout_period() const { return blackout_period_; }
  Duration weight_update_period() const { return weight_update_period_; }
  Duration weight_expiration_period() const {
    return weight_expiration_period_;
  }
  float error_utilization_penalty() const { return error_utilization_penalty_; }
  const std::vector<ParsedMetric>& parsed_custom_metrics() const {
    return parsed_custom_metrics_;
  }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs& args);

  void JsonPostLoad(const Json& json, const JsonArgs& args,
                    ValidationErrors* errors);

 private:
  bool enable_oob_load_report_ = false;
  Duration oob_reporting_period_ = Duration::Seconds(10);
  Duration blackout_period_ = Duration::Seconds(10);
  Duration weight_update_period_ = Duration::Seconds(1);
  Duration weight_expiration_period_ = Duration::Minutes(3);
  float error_utilization_penalty_ = 1.0;
  std::vector<ParsedMetric> parsed_custom_metrics_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_WEIGHTED_ROUND_ROBIN_WEIGHTED_ROUND_ROBIN_H
