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

#ifndef GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_H
#define GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_H

#include <string>

#include "src/core/load_balancing/lb_policy.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/ref_counted_string.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"
#include "absl/strings/string_view.h"

// TODO(bpawan): Expose Channel Factory types once defined for C++ (gRFC A119).

namespace grpc_core {

class AutoShardingLbConfig final : public LoadBalancingPolicy::Config {
 public:
  AutoShardingLbConfig() = default;

  AutoShardingLbConfig(const AutoShardingLbConfig&) = delete;
  AutoShardingLbConfig& operator=(const AutoShardingLbConfig&) = delete;

  AutoShardingLbConfig(AutoShardingLbConfig&& other) = delete;
  AutoShardingLbConfig& operator=(AutoShardingLbConfig&& other) = delete;

  absl::string_view name() const override;

  const std::string& channel_factory_key() const {
    return channel_factory_key_;
  }
  const std::string& autosharding_target() const {
    return autosharding_target_;
  }
  const RefCountedStringValue& key_header_name() const {
    return key_header_name_;
  }
  bool enable_fallback() const { return enable_fallback_; }
  // TODO(bpawan): Pass this to the autosharding client, which will own the
  // initial assignment timer (gRFC A119).
  Duration initial_assignment_timeout() const {
    return initial_assignment_timeout_;
  }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs& args);

  void JsonPostLoad(const Json& json, const JsonArgs& args,
                    ValidationErrors* errors);

 private:
  std::string channel_factory_key_;
  std::string autosharding_target_;
  RefCountedStringValue key_header_name_;
  bool enable_fallback_ = false;
  Duration initial_assignment_timeout_ = Duration::Seconds(60);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_H
