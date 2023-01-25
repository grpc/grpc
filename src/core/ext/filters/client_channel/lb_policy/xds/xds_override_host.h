//
// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_OVERRIDE_HOST_H
#define GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_OVERRIDE_HOST_H

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"

#include "src/core/ext/xds/xds_health_status.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/load_balancing/lb_policy.h"

namespace grpc_core {

// Config for stateful session LB policy.
class XdsOverrideHostLbConfig : public LoadBalancingPolicy::Config {
 public:
  XdsOverrideHostLbConfig() = default;

  XdsOverrideHostLbConfig(const XdsOverrideHostLbConfig&) = delete;
  XdsOverrideHostLbConfig& operator=(const XdsOverrideHostLbConfig&) = delete;

  XdsOverrideHostLbConfig(XdsOverrideHostLbConfig&& other) = delete;
  XdsOverrideHostLbConfig& operator=(XdsOverrideHostLbConfig&& other) = delete;

  static absl::string_view Name() { return "xds_override_host_experimental"; }

  absl::string_view name() const override { return Name(); }

  RefCountedPtr<LoadBalancingPolicy::Config> child_config() const {
    return child_config_;
  }

  XdsHealthStatusSet override_host_status_set() const {
    return override_host_status_set_;
  }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
  void JsonPostLoad(const Json& json, const JsonArgs&,
                    ValidationErrors* errors);

 private:
  RefCountedPtr<LoadBalancingPolicy::Config> child_config_;
  XdsHealthStatusSet override_host_status_set_;
};

}  // namespace grpc_core
#endif  // GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_OVERRIDE_HOST_H
