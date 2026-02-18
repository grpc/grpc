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

#ifndef GRPC_SRC_CORE_CHANNELZ_V2TOV1_CONVERT_H
#define GRPC_SRC_CORE_CHANNELZ_V2TOV1_CONVERT_H

#include <string>
#include <vector>

#include "absl/status/statusor.h"

namespace grpc_core {
namespace channelz {
namespace v2tov1 {

class EntityFetcher {
 public:
  virtual ~EntityFetcher() = default;
  // returns serialized grpc::channelz::v2::Entity
  virtual absl::StatusOr<std::string> GetEntity(int64_t id) = 0;
  // returns list of serialized grpc::channelz::v2::Entity
  virtual absl::StatusOr<std::vector<std::string>> GetEntitiesWithParent(
      int64_t parent_id) = 0;
};

// Converts a v2 entity to a v1 entity.
// `serialized_entity` is the serialized v2 entity.
// `fetcher` is used to fetch child entities.
// `json` is true if the output should be a json string, false if it should be
// a serialized proto. The serialized proto will be of the appropriate channelz
// v1 proto type.
absl::StatusOr<std::string> ConvertServer(const std::string& serialized_entity,
                                          EntityFetcher& fetcher, bool json);
absl::StatusOr<std::string> ConvertSocket(const std::string& serialized_entity,
                                          EntityFetcher& fetcher, bool json);
absl::StatusOr<std::string> ConvertChannel(const std::string& serialized_entity,
                                           EntityFetcher& fetcher, bool json);
absl::StatusOr<std::string> ConvertSubchannel(
    const std::string& serialized_entity, EntityFetcher& fetcher, bool json);
absl::StatusOr<std::string> ConvertListenSocket(
    const std::string& serialized_entity, EntityFetcher& fetcher, bool json);

}  // namespace v2tov1
}  // namespace channelz
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CHANNELZ_V2TOV1_CONVERT_H
