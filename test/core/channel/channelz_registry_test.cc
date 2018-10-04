/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdlib.h>
#include <string.h>

#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/surface/channel.h"

#include "test/core/util/test_config.h"

#include <stdlib.h>
#include <string.h>

namespace grpc_core {
namespace channelz {
namespace testing {

class ChannelzRegistryPeer {
  const InlinedVector<BaseNode*, 20>* entities() {
    return &ChannelzRegistry::Default()->entities_;
  }
  int num_empty_slots() {
    return ChannelzRegistry::Default()->num_empty_slots_;
  }
};

TEST(ChannelzRegistryTest, UuidStartsAboveZeroTest) {
  UniquePtr<BaseNode> channelz_channel(
      new BaseNode(BaseNode::EntityType::kTopLevelChannel));
  intptr_t uuid = channelz_channel->uuid();
  EXPECT_GT(uuid, 0) << "First uuid chose must be greater than zero. Zero if "
                        "reserved according to "
                        "https://github.com/grpc/proposal/blob/master/"
                        "A14-channelz.md";
}

TEST(ChannelzRegistryTest, UuidsAreIncreasing) {
  std::vector<UniquePtr<BaseNode>> channelz_channels;
  channelz_channels.reserve(10);
  for (int i = 0; i < 10; ++i) {
    channelz_channels.push_back(UniquePtr<BaseNode>(
        new BaseNode(BaseNode::EntityType::kTopLevelChannel)));
  }
  for (size_t i = 1; i < channelz_channels.size(); ++i) {
    EXPECT_LT(channelz_channels[i - 1]->uuid(), channelz_channels[i]->uuid())
        << "Uuids must always be increasing";
  }
}

TEST(ChannelzRegistryTest, RegisterGetTest) {
  UniquePtr<BaseNode> channelz_channel(
      new BaseNode(BaseNode::EntityType::kTopLevelChannel));
  BaseNode* retrieved = ChannelzRegistry::Get(channelz_channel->uuid());
  EXPECT_EQ(channelz_channel.get(), retrieved);
}

TEST(ChannelzRegistryTest, RegisterManyItems) {
  std::vector<UniquePtr<BaseNode>> channelz_channels;
  for (int i = 0; i < 100; i++) {
    channelz_channels.push_back(UniquePtr<BaseNode>(
        new BaseNode(BaseNode::EntityType::kTopLevelChannel)));
    BaseNode* retrieved = ChannelzRegistry::Get(channelz_channels[i]->uuid());
    EXPECT_EQ(channelz_channels[i].get(), retrieved);
  }
}

TEST(ChannelzRegistryTest, NullIfNotPresentTest) {
  UniquePtr<BaseNode> channelz_channel(
      new BaseNode(BaseNode::EntityType::kTopLevelChannel));
  // try to pull out a uuid that does not exist.
  BaseNode* nonexistant = ChannelzRegistry::Get(channelz_channel->uuid() + 1);
  EXPECT_EQ(nonexistant, nullptr);
  BaseNode* retrieved = ChannelzRegistry::Get(channelz_channel->uuid());
  EXPECT_EQ(channelz_channel.get(), retrieved);
}

}  // namespace testing
}  // namespace channelz
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
