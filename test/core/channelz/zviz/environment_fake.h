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

#ifndef GRPC_TEST_CORE_CHANNELZ_ZVIZ_ENVIRONMENT_FAKE_H
#define GRPC_TEST_CORE_CHANNELZ_ZVIZ_ENVIRONMENT_FAKE_H

#include "absl/container/flat_hash_map.h"
#include "src/core/channelz/zviz/environment.h"

namespace grpc_zviz {

class EnvironmentFake final : public Environment {
 public:
  explicit EnvironmentFake(
      absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities)
      : entities_(std::move(entities)) {}

  std::string EntityLinkTarget(int64_t entity_id) override {
    return absl::StrCat("http://example.com/", entity_id);
  }
  absl::StatusOr<grpc::channelz::v2::Entity> GetEntity(
      int64_t entity_id) override {
    auto it = entities_.find(entity_id);
    if (it == entities_.end()) {
      return absl::NotFoundError(absl::StrCat("entity ", entity_id));
    }
    return it->second;
  }

 private:
  absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities_;
};

}  // namespace grpc_zviz

#endif  // GRPC_TEST_CORE_CHANNELZ_ZVIZ_ENVIRONMENT_FAKE_H
