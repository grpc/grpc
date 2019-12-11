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
#include <grpc/support/string_util.h>

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

class ChannelzRegistryTest : public ::testing::Test {
 protected:
  // ensure we always have a fresh registry for tests.
  void SetUp() override { ChannelzRegistry::Init(); }

  void TearDown() override { ChannelzRegistry::Shutdown(); }
};

static RefCountedPtr<BaseNode> CreateTestNode() {
  return MakeRefCounted<ListenSocketNode>("test", "test");
}

TEST_F(ChannelzRegistryTest, UuidStartsAboveZeroTest) {
  RefCountedPtr<BaseNode> channelz_channel = CreateTestNode();
  intptr_t uuid = channelz_channel->uuid();
  EXPECT_GT(uuid, 0) << "First uuid chose must be greater than zero. Zero if "
                        "reserved according to "
                        "https://github.com/grpc/proposal/blob/master/"
                        "A14-channelz.md";
}

TEST_F(ChannelzRegistryTest, UuidsAreIncreasing) {
  std::vector<RefCountedPtr<BaseNode>> channelz_channels;
  channelz_channels.reserve(10);
  for (int i = 0; i < 10; ++i) {
    channelz_channels.push_back(CreateTestNode());
  }
  for (size_t i = 1; i < channelz_channels.size(); ++i) {
    EXPECT_LT(channelz_channels[i - 1]->uuid(), channelz_channels[i]->uuid())
        << "Uuids must always be increasing";
  }
}

TEST_F(ChannelzRegistryTest, RegisterGetTest) {
  RefCountedPtr<BaseNode> channelz_channel = CreateTestNode();
  RefCountedPtr<BaseNode> retrieved =
      ChannelzRegistry::Get(channelz_channel->uuid());
  EXPECT_EQ(channelz_channel, retrieved);
}

TEST_F(ChannelzRegistryTest, RegisterManyItems) {
  std::vector<RefCountedPtr<BaseNode>> channelz_channels;
  for (int i = 0; i < 100; i++) {
    channelz_channels.push_back(CreateTestNode());
    RefCountedPtr<BaseNode> retrieved =
        ChannelzRegistry::Get(channelz_channels[i]->uuid());
    EXPECT_EQ(channelz_channels[i], retrieved);
  }
}

TEST_F(ChannelzRegistryTest, NullIfNotPresentTest) {
  RefCountedPtr<BaseNode> channelz_channel = CreateTestNode();
  // try to pull out a uuid that does not exist.
  RefCountedPtr<BaseNode> nonexistant =
      ChannelzRegistry::Get(channelz_channel->uuid() + 1);
  EXPECT_EQ(nonexistant, nullptr);
  RefCountedPtr<BaseNode> retrieved =
      ChannelzRegistry::Get(channelz_channel->uuid());
  EXPECT_EQ(channelz_channel, retrieved);
}

TEST_F(ChannelzRegistryTest, TestUnregistration) {
  const int kLoopIterations = 100;
  // These channels will stay in the registry for the duration of the test.
  std::vector<RefCountedPtr<BaseNode>> even_channels;
  even_channels.reserve(kLoopIterations);
  std::vector<intptr_t> odd_uuids;
  odd_uuids.reserve(kLoopIterations);
  {
    // These channels will unregister themselves at the end of this block.
    std::vector<RefCountedPtr<BaseNode>> odd_channels;
    odd_channels.reserve(kLoopIterations);
    for (int i = 0; i < kLoopIterations; i++) {
      even_channels.push_back(CreateTestNode());
      odd_channels.push_back(CreateTestNode());
      odd_uuids.push_back(odd_channels[i]->uuid());
    }
  }
  // Check that the even channels are present and the odd channels are not.
  for (int i = 0; i < kLoopIterations; i++) {
    RefCountedPtr<BaseNode> retrieved =
        ChannelzRegistry::Get(even_channels[i]->uuid());
    EXPECT_EQ(even_channels[i], retrieved);
    retrieved = ChannelzRegistry::Get(odd_uuids[i]);
    EXPECT_EQ(retrieved, nullptr);
  }
  // Add more channels and verify that they get added correctly, to make
  // sure that the unregistration didn't leave the registry in a weird state.
  std::vector<RefCountedPtr<BaseNode>> more_channels;
  more_channels.reserve(kLoopIterations);
  for (int i = 0; i < kLoopIterations; i++) {
    more_channels.push_back(CreateTestNode());
    RefCountedPtr<BaseNode> retrieved =
        ChannelzRegistry::Get(more_channels[i]->uuid());
    EXPECT_EQ(more_channels[i], retrieved);
  }
}

}  // namespace testing
}  // namespace channelz
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
