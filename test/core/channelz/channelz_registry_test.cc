//
//
// Copyright 2017 gRPC authors.
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
//

#include "src/core/channelz/channelz_registry.h"

#include <stdlib.h>

#include <algorithm>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "src/core/channelz/channelz.h"
#include "src/core/config/config_vars.h"
#include "src/core/util/notification.h"
#include "src/core/util/shared_bit_gen.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace channelz {
namespace testing {

class ChannelzRegistryTest : public ::testing::TestWithParam<int> {
 protected:
  // ensure we always have a fresh registry for tests.
  void SetUp() override {
    ChannelzRegistry::TestOnlyReset();
    ConfigVars::Reset();
    ConfigVars::Overrides overrides;
    overrides.channelz_max_orphaned_nodes = GetParam();
    ConfigVars::SetOverrides(overrides);
    ChannelzRegistry::TestOnlyReset();
  }
};

INSTANTIATE_TEST_SUITE_P(ChannelRegistrySuite, ChannelzRegistryTest,
                         ::testing::Values(0, 1, 32, 1024, 1000000),
                         [](const ::testing::TestParamInfo<int>& info) {
                           return absl::StrCat("max_orphaned_nodes_",
                                               info.param);
                         });

static RefCountedPtr<BaseNode> CreateTestNode() {
  return MakeRefCounted<ListenSocketNode>("test", "test");
}

TEST_P(ChannelzRegistryTest, UuidStartsAboveZeroTest) {
  RefCountedPtr<BaseNode> channelz_channel = CreateTestNode();
  intptr_t uuid = channelz_channel->uuid();
  EXPECT_GT(uuid, 0) << "First uuid chose must be greater than zero. Zero if "
                        "reserved according to "
                        "https://github.com/grpc/proposal/blob/master/"
                        "A14-channelz.md";
}

TEST_P(ChannelzRegistryTest, UuidsAreIncreasing) {
  std::vector<RefCountedPtr<BaseNode>> channelz_channels;
  channelz_channels.reserve(10);
  for (int i = 0; i < 10; ++i) {
    channelz_channels.push_back(CreateTestNode());
  }
  for (size_t i = 1; i < channelz_channels.size(); ++i) {
    const intptr_t prev_uuid = channelz_channels[i - 1]->uuid();
    const intptr_t curr_uuid = channelz_channels[i]->uuid();
    EXPECT_LT(prev_uuid, curr_uuid) << "Uuids must always be increasing";
  }
}

TEST_P(ChannelzRegistryTest, RegisterGetTest) {
  RefCountedPtr<BaseNode> channelz_channel = CreateTestNode();
  WeakRefCountedPtr<BaseNode> retrieved =
      ChannelzRegistry::Get(channelz_channel->uuid());
  EXPECT_EQ(channelz_channel.get(), retrieved.get());
}

TEST_P(ChannelzRegistryTest, RegisterManyItems) {
  std::vector<RefCountedPtr<BaseNode>> channelz_channels;
  for (int i = 0; i < 100; i++) {
    channelz_channels.push_back(CreateTestNode());
    WeakRefCountedPtr<BaseNode> retrieved =
        ChannelzRegistry::Get(channelz_channels[i]->uuid());
    EXPECT_EQ(channelz_channels[i].get(), retrieved.get());
  }
}

TEST_P(ChannelzRegistryTest, NullIfNotPresentTest) {
  RefCountedPtr<BaseNode> channelz_channel = CreateTestNode();
  // try to pull out a uuid that does not exist.
  WeakRefCountedPtr<BaseNode> nonexistent =
      ChannelzRegistry::Get(channelz_channel->uuid() + 1);
  EXPECT_EQ(nonexistent.get(), nullptr);
  WeakRefCountedPtr<BaseNode> retrieved =
      ChannelzRegistry::Get(channelz_channel->uuid());
  EXPECT_EQ(channelz_channel.get(), retrieved.get());
}

TEST_P(ChannelzRegistryTest, TestUnregistration) {
  if (GetParam() != 0) {
    GTEST_SKIP()
        << "Unregistration not possible to test with orphaning like this: we "
           "don't know the shard that things are orphaned on, so we can't "
           "predict how to flush it from the orphan list.";
  }
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
    WeakRefCountedPtr<BaseNode> retrieved =
        ChannelzRegistry::Get(even_channels[i]->uuid());
    EXPECT_EQ(even_channels[i].get(), retrieved.get());
    retrieved = ChannelzRegistry::Get(odd_uuids[i]);
    EXPECT_EQ(retrieved.get(), nullptr);
  }
  // Add more channels and verify that they get added correctly, to make
  // sure that the unregistration didn't leave the registry in a weird state.
  std::vector<RefCountedPtr<BaseNode>> more_channels;
  more_channels.reserve(kLoopIterations);
  for (int i = 0; i < kLoopIterations; i++) {
    more_channels.push_back(CreateTestNode());
    WeakRefCountedPtr<BaseNode> retrieved =
        ChannelzRegistry::Get(more_channels[i]->uuid());
    EXPECT_EQ(more_channels[i].get(), retrieved.get());
  }
}

TEST_P(ChannelzRegistryTest, ThreadStressTest) {
  std::vector<std::thread> threads;
  threads.reserve(30);
  Notification done;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back(std::thread([&done]() {
      while (!done.HasBeenNotified()) {
        auto a = MakeRefCounted<ChannelNode>("x", 1, false);
        auto b = MakeRefCounted<ChannelNode>("x", 1, false);
        auto c = MakeRefCounted<ChannelNode>("x", 1, false);
        auto d = MakeRefCounted<ChannelNode>("x", 1, false);
      }
    }));
  }
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back(std::thread([&done]() {
      intptr_t last_uuid = 0;
      while (!done.HasBeenNotified()) {
        intptr_t uuid = MakeRefCounted<ChannelNode>("x", 1, false)->uuid();
        EXPECT_GT(uuid, last_uuid);
        last_uuid = uuid;
      }
    }));
  }
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&done]() {
      while (!done.HasBeenNotified()) {
        ChannelzRegistry::GetAllEntities();
      }
    });
  }
  absl::SleepFor(absl::Seconds(10));
  done.Notify();
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_P(ChannelzRegistryTest, HugeNodeCount) {
  std::vector<RefCountedPtr<BaseNode>> nodes;
  nodes.reserve(200000);
  for (int i = 0; i < 100000; ++i) {
    nodes.push_back(MakeRefCounted<ChannelNode>("x", 1, false));
    nodes.push_back(MakeRefCounted<SocketNode>("x", "y", "z", nullptr));
  }
  auto [channels1, end] = ChannelzRegistry::GetTopChannels(0);
  EXPECT_FALSE(end);
  auto [channels2, end2] =
      ChannelzRegistry::GetTopChannels(channels1.back()->uuid());
  EXPECT_FALSE(end2);
  std::shuffle(nodes.begin(), nodes.end(), SharedBitGen());
}

TEST_P(ChannelzRegistryTest, HugeNodeCountWithParents) {
  std::vector<RefCountedPtr<BaseNode>> nodes;
  for (int i = 0; i < 100; ++i) {
    nodes.push_back(MakeRefCounted<ChannelNode>("x", 1, false));
    auto parent = nodes.back();
    for (int j = 0; j < 1000; ++j) {
      nodes.push_back(MakeRefCounted<SubchannelNode>("x", 1));
      nodes.back()->AddParent(parent.get());
    }
  }
  auto [channels1, end] = ChannelzRegistry::GetTopChannels(0);
  EXPECT_TRUE(end);
  std::shuffle(nodes.begin(), nodes.end(), SharedBitGen());
}

TEST_P(ChannelzRegistryTest, ServerWithChildren) {
  auto server = MakeRefCounted<ServerNode>(1);
  std::vector<RefCountedPtr<BaseNode>> sockets;
  sockets.reserve(200000);
  for (int i = 0; i < 200000; ++i) {
    sockets.push_back(MakeRefCounted<SocketNode>("x", "y", "z", nullptr));
    sockets.back()->AddParent(server.get());
  }
  std::shuffle(sockets.begin(), sockets.end(), SharedBitGen());
  sockets.resize(100000);
  auto child_sockets = server->child_sockets();
  server.reset();
  sockets.clear();
}

}  // namespace testing
}  // namespace channelz
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
