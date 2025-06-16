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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_ACTION_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_ACTION_H

#include "src/core/xds/grpc/xds_matcher.h"

namespace grpc_core {

// Dummy BUcket action
// Need to add RLQS bucket config action
class BucketingAction : public XdsMatcher::Action {
 public:
  struct BucketConfig {
    absl::flat_hash_map<std::string, std::string> map;
  };

  explicit BucketingAction(BucketConfig config)
      : bucket_config_(std::move(config)) {}

  absl::string_view type_url() const override { return "sampleAction"; }
  absl::string_view GetConfigValue(absl::string_view key) {
    return bucket_config_.map[key];
  }

 private:
  BucketConfig bucket_config_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_ACTION_H
