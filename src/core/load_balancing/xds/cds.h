//
// Copyright 2019 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LOAD_BALANCING_XDS_CDS_H
#define GRPC_SRC_CORE_LOAD_BALANCING_XDS_CDS_H

#include <vector>

#include "src/core/resolver/xds/xds_config.h"

namespace grpc_core {

// State used to retain child policy names for the priority policy.
// Exposed for testing purposes only.
class CdsChildNameState {
 public:
  // Each index in the vector is a priority.  The value for a given
  // priority is the child number for that priority.
  const std::vector<size_t /*child_number*/>& priority_child_numbers() const {
    return priority_child_numbers_;
  }

  size_t next_available_child_number() const {
    return next_available_child_number_;
  }

  // Updates child numbers for endpoint_config, reusing child numbers
  // from old_cluster and current state in an intelligent way to avoid
  // unnecessary churn.
  void Update(const XdsConfig::ClusterConfig* old_cluster,
              const XdsConfig::ClusterConfig::EndpointConfig& endpoint_config);

  void Reset() {
    priority_child_numbers_.clear();
    next_available_child_number_ = 0;
  }

 private:
  std::vector<size_t /*child_number*/> priority_child_numbers_;
  size_t next_available_child_number_ = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_XDS_CDS_H
