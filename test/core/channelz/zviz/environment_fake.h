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

  absl::StatusOr<GetChildrenResult> GetChildrenPaginated(
      int64_t entity_id, absl::string_view kind, int64_t start,
      size_t max_results) override {
    std::deque<grpc::channelz::v2::Entity> children;
    for (const auto& pair : entities_) {
      if (pair.second.kind() != kind) continue;
      for (int64_t parent_id : pair.second.parents()) {
        if (parent_id == entity_id) {
          children.push_back(pair.second);
          break;
        }
      }
    }
    std::sort(
        children.begin(), children.end(),
        [](const grpc::channelz::v2::Entity& a,
           const grpc::channelz::v2::Entity& b) { return a.id() < b.id(); });
    while (!children.empty() && children[0].id() < start) {
      children.pop_front();
    }
    bool end = children.size() < max_results;
    if (!end) children.resize(max_results);
    GetChildrenResult result;
    for (auto& child : children) {
      result.entities.push_back(std::move(child));
    }
    result.end = end;
    return result;
  }

 private:
  absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities_;
};

}  // namespace grpc_zviz

#endif  // GRPC_TEST_CORE_CHANNELZ_ZVIZ_ENVIRONMENT_FAKE_H
