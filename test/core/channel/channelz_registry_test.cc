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

TEST(ChannelzRegistryTest, UuidStartsAboveZeroTest) {
  BaseNode* channelz_channel = nullptr;
  intptr_t uuid = ChannelzRegistry::Register(channelz_channel);
  EXPECT_GT(uuid, 0) << "First uuid chose must be greater than zero. Zero if "
                        "reserved according to "
                        "https://github.com/grpc/proposal/blob/master/"
                        "A14-channelz.md";
  ChannelzRegistry::Unregister(uuid);
}

TEST(ChannelzRegistryTest, UuidsAreIncreasing) {
  BaseNode* channelz_channel = nullptr;
  std::vector<intptr_t> uuids;
  uuids.reserve(10);
  for (int i = 0; i < 10; ++i) {
    // reregister the same object. It's ok since we are just testing uuids
    uuids.push_back(ChannelzRegistry::Register(channelz_channel));
  }
  for (size_t i = 1; i < uuids.size(); ++i) {
    EXPECT_LT(uuids[i - 1], uuids[i]) << "Uuids must always be increasing";
  }
}

TEST(ChannelzRegistryTest, RegisterGetTest) {
  // we hackily jam an intptr_t into this pointer to check for equality later
  BaseNode* channelz_channel = (BaseNode*)42;
  intptr_t uuid = ChannelzRegistry::Register(channelz_channel);
  BaseNode* retrieved = ChannelzRegistry::Get(uuid);
  EXPECT_EQ(channelz_channel, retrieved);
}

TEST(ChannelzRegistryTest, RegisterManyItems) {
  // we hackily jam an intptr_t into this pointer to check for equality later
  BaseNode* channelz_channel = (BaseNode*)42;
  for (int i = 0; i < 100; i++) {
    intptr_t uuid = ChannelzRegistry::Register(channelz_channel);
    BaseNode* retrieved = ChannelzRegistry::Get(uuid);
    EXPECT_EQ(channelz_channel, retrieved);
  }
}

TEST(ChannelzRegistryTest, NullIfNotPresentTest) {
  // we hackily jam an intptr_t into this pointer to check for equality later
  BaseNode* channelz_channel = (BaseNode*)42;
  intptr_t uuid = ChannelzRegistry::Register(channelz_channel);
  // try to pull out a uuid that does not exist.
  BaseNode* nonexistant = ChannelzRegistry::Get(uuid + 1);
  EXPECT_EQ(nonexistant, nullptr);
  BaseNode* retrieved = ChannelzRegistry::Get(uuid);
  EXPECT_EQ(channelz_channel, retrieved);
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
