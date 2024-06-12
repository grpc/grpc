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

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_LB_METADATA_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_LB_METADATA_H

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/load_balancing/lb_policy.h"

namespace grpc_core {

class LbMetadata : public LoadBalancingPolicy::MetadataInterface {
 public:
  explicit LbMetadata(grpc_metadata_batch* batch) : batch_(batch) {}

  void Add(absl::string_view key, absl::string_view value) override;

  std::vector<std::pair<std::string, std::string>> TestOnlyCopyToVector()
      override;

  absl::optional<absl::string_view> Lookup(absl::string_view key,
                                           std::string* buffer) const override;

 private:
  grpc_metadata_batch* batch_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CLIENT_CHANNEL_LB_METADATA_H
