//
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
//

#include "test/cpp/util/channelz_util.h"

#include "src/core/channelz/channelz.h"
#include "src/core/channelz/channelz_registry.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/grpc_check.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace testing {

using grpc_core::channelz::BaseNode;
using grpc_core::channelz::ChannelzRegistry;
using grpc_core::channelz::SocketNode;
using grpc_core::channelz::SubchannelNode;

namespace {

grpc::channelz::v2::Entity GetEntityFromNode(BaseNode& node) {
  const absl::Duration kTimeout =
      absl::Seconds(1) * grpc_test_slowdown_factor();
  std::string serialized_entity = node.SerializeEntityToString(kTimeout);
  grpc::channelz::v2::Entity entity;
  GRPC_CHECK(entity.ParseFromString(serialized_entity));
  return entity;
}

}  // namespace

std::vector<grpc::channelz::v2::Entity> ChannelzUtil::GetSubchannelsForAddress(
    absl::string_view address) {
  std::vector<grpc::channelz::v2::Entity> entities;
  while (true) {
    auto [nodes, done] = ChannelzRegistry::GetNodesOfType(
        /*start_node=*/0, BaseNode::EntityType::kSubchannel,
        /*max_results=*/std::numeric_limits<size_t>::max());
    for (auto& node : nodes) {
      auto* subchannel_node = grpc_core::DownCast<SubchannelNode*>(node.get());
      if (subchannel_node->target() == address) {
        entities.push_back(GetEntityFromNode(*node));
      }
    }
    if (done) break;
  }
  return entities;
}

std::vector<grpc::channelz::v2::Entity> ChannelzUtil::GetSubchannelConnections(
    int64_t subchannel_id) {
  auto subchannel_node = ChannelzRegistry::GetSubchannel(subchannel_id);
  GRPC_CHECK(subchannel_node != nullptr);
  std::vector<grpc::channelz::v2::Entity> entities;
  while (true) {
    auto [nodes, done] = ChannelzRegistry::GetChildrenOfType(
        /*start_node=*/0, /*parent=*/subchannel_node.get(),
        BaseNode::EntityType::kSocket,
        /*max_results=*/std::numeric_limits<size_t>::max());
    for (auto& node : nodes) {
      entities.push_back(GetEntityFromNode(*node));
    }
    if (done) break;
  }
  return entities;
}

}  // namespace testing
}  // namespace grpc
