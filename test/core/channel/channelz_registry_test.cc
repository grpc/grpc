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
 public:
  static int size() {
    return (*ChannelzRegistry::registry_)->node_map_.size();
  }
  static void DoGarbageCollection() {
    MutexLock lock(&(*ChannelzRegistry::registry_)->mu_);
    (*ChannelzRegistry::registry_)->DoGarbageCollectionLocked();
  }
};

class ChannelzRegistryTest : public ::testing::Test {
 protected:
  // ensure we always have a fresh registry for tests.
  void SetUp() override { grpc_init(); }

  void TearDown() override { grpc_shutdown(); }
};

TEST_F(ChannelzRegistryTest, UuidStartsAboveZeroTest) {
  RefCountedPtr<BaseNode> channelz_channel =
      ChannelzRegistry::CreateNode<BaseNode>(
          BaseNode::EntityType::kTopLevelChannel);
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
    channelz_channels.push_back(ChannelzRegistry::CreateNode<BaseNode>(
        BaseNode::EntityType::kTopLevelChannel));
  }
  for (size_t i = 1; i < channelz_channels.size(); ++i) {
    EXPECT_LT(channelz_channels[i - 1]->uuid(), channelz_channels[i]->uuid())
        << "Uuids must always be increasing";
  }
}

TEST_F(ChannelzRegistryTest, RegisterGetTest) {
  RefCountedPtr<BaseNode> channelz_channel =
      ChannelzRegistry::CreateNode<BaseNode>(
          BaseNode::EntityType::kTopLevelChannel);
  RefCountedPtr<BaseNode> retrieved =
      ChannelzRegistry::Get(channelz_channel->uuid());
  EXPECT_EQ(channelz_channel, retrieved);
}

TEST_F(ChannelzRegistryTest, RegisterManyItems) {
  std::vector<RefCountedPtr<BaseNode>> channelz_channels;
  for (int i = 0; i < 100; i++) {
    channelz_channels.push_back(ChannelzRegistry::CreateNode<BaseNode>(
        BaseNode::EntityType::kTopLevelChannel));
    RefCountedPtr<BaseNode> retrieved =
        ChannelzRegistry::Get(channelz_channels[i]->uuid());
    EXPECT_EQ(channelz_channels[i], retrieved);
  }
}

TEST_F(ChannelzRegistryTest, NullIfNotPresentTest) {
  RefCountedPtr<BaseNode> channelz_channel =
      ChannelzRegistry::CreateNode<BaseNode>(
          BaseNode::EntityType::kTopLevelChannel);
  // try to pull out a uuid that does not exist.
  RefCountedPtr<BaseNode> nonexistant =
      ChannelzRegistry::Get(channelz_channel->uuid() + 1);
  EXPECT_EQ(nonexistant, nullptr);
  RefCountedPtr<BaseNode> retrieved =
      ChannelzRegistry::Get(channelz_channel->uuid());
  EXPECT_EQ(channelz_channel, retrieved);
}

TEST_F(ChannelzRegistryTest, GarbageCollection) {
  const int kLoopIterations = 300;
  // These channels will stay in the registry for the duration of the test.
  std::vector<RefCountedPtr<BaseNode>> even_channels;
  even_channels.reserve(kLoopIterations);
  {
    // These channels will be unreffed at the end of this block.
    std::vector<RefCountedPtr<BaseNode>> odd_channels;
    odd_channels.reserve(kLoopIterations);
    for (int i = 0; i < kLoopIterations; i++) {
      even_channels.push_back(ChannelzRegistry::CreateNode<BaseNode>(
          BaseNode::EntityType::kTopLevelChannel));
      odd_channels.push_back(ChannelzRegistry::CreateNode<BaseNode>(
          BaseNode::EntityType::kTopLevelChannel));
    }
    // All channels are still in the registry.
    EXPECT_EQ(2 * kLoopIterations, ChannelzRegistryPeer::size());
  }
  // Run GC.
  ChannelzRegistryPeer::DoGarbageCollection();
  // Half of the channels should have been removed.
  EXPECT_EQ(kLoopIterations, ChannelzRegistryPeer::size());
}

TEST_F(ChannelzRegistryTest, GetAfterUnref) {
  const int kLoopIterations = 1000;
  // Add nodes.
  std::vector<RefCountedPtr<BaseNode>> nodes;
  std::vector<intptr_t> uuids;
  for (int i = 0; i < kLoopIterations; i++) {
    nodes.push_back(ChannelzRegistry::CreateNode<BaseNode>(
        BaseNode::EntityType::kTopLevelChannel));
    uuids.push_back(nodes[i]->uuid());
  }
  EXPECT_EQ(kLoopIterations, ChannelzRegistryPeer::size());
  // Unref nodes.
  // Note: It's possible that GC will run at this point, which will
  // eliminate the value of this test.  However, that's unlikely, since
  // GC runs only once every 5 seconds, and even if it happens, the test
  // would still fail most of the time if Get() was returning nodes that
  // had been unreffed.
  nodes.clear();
  EXPECT_LE(ChannelzRegistryPeer::size(), kLoopIterations);
  // Get() should return null for all of them.
  for (int i = 0; i < kLoopIterations; i++) {
    RefCountedPtr<BaseNode> retrieved = ChannelzRegistry::Get(uuids[i]);
    EXPECT_EQ(retrieved, nullptr);
  }
}

TEST_F(ChannelzRegistryTest, GetTopChannelsAfterUnref) {
  const int kLoopIterations = 1000;
  // Add nodes.
  std::vector<RefCountedPtr<BaseNode>> nodes;
  std::vector<intptr_t> uuids;
  for (int i = 0; i < kLoopIterations; i++) {
    nodes.push_back(ChannelzRegistry::CreateNode<BaseNode>(
        BaseNode::EntityType::kTopLevelChannel));
    uuids.push_back(nodes[i]->uuid());
  }
  EXPECT_EQ(kLoopIterations, ChannelzRegistryPeer::size());
  // Unref nodes.
  // Note: It's possible that GC will run at this point, which will
  // eliminate the value of this test.  However, that's unlikely, since
  // GC runs only once every 5 seconds, and even if it happens, the test
  // would still fail most of the time if GetTopChannels() was returning
  // nodes that had been unreffed.
  nodes.clear();
  EXPECT_LE(ChannelzRegistryPeer::size(), kLoopIterations);
  // Get() should return null for all of them.
  for (int i = 0; i < kLoopIterations; i++) {
    char* top_channels_json = ChannelzRegistry::GetTopChannels(0);
    EXPECT_STREQ("{\"end\":true}", top_channels_json);
    gpr_free(top_channels_json);
  }
}

TEST_F(ChannelzRegistryTest, GetServersAfterUnref) {
  const int kLoopIterations = 1000;
  // Add nodes.
  std::vector<RefCountedPtr<BaseNode>> nodes;
  std::vector<intptr_t> uuids;
  for (int i = 0; i < kLoopIterations; i++) {
    nodes.push_back(ChannelzRegistry::CreateNode<BaseNode>(
        BaseNode::EntityType::kServer));
    uuids.push_back(nodes[i]->uuid());
  }
  EXPECT_EQ(kLoopIterations, ChannelzRegistryPeer::size());
  // Unref nodes.
  // Note: It's possible that GC will run at this point, which will
  // eliminate the value of this test.  However, that's unlikely, since
  // GC runs only once every 5 seconds, and even if it happens, the test
  // would still fail most of the time if GetServers() was returning nodes
  // that had been unreffed.
  nodes.clear();
  EXPECT_LE(ChannelzRegistryPeer::size(), kLoopIterations);
  // Get() should return null for all of them.
  for (int i = 0; i < kLoopIterations; i++) {
    char* servers_json = ChannelzRegistry::GetServers(0);
    EXPECT_STREQ("{\"end\":true}", servers_json);
    gpr_free(servers_json);
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
