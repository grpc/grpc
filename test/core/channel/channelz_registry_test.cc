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

#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"

#include "test/core/util/test_config.h"
#include "test/cpp/util/channel_trace_proto_helper.h"

#include <stdlib.h>
#include <string.h>

namespace grpc_core {
namespace testing {

// Tests basic ChannelTrace functionality like construction, adding trace, and
// lookups by uuid.
TEST(ChannelzRegistryTest, UuidStartsAboveZeroTest) {
  int object_to_register;
  intptr_t uuid = ChannelzRegistry::Default()->Register(&object_to_register);
  ASSERT_GT(uuid, 0) << "First uuid chose must be greater than zero. Zero if "
                        "reserved according to "
                        "https://github.com/grpc/proposal/blob/master/"
                        "A14-channelz.md";
  ChannelzRegistry::Default()->Unregister(uuid);
}

TEST(ChannelzRegistryTest, UuidsAreIncreasing) {
  int object_to_register;
  std::vector<intptr_t> uuids;
  for (int i = 0; i < 10; ++i) {
    // reregister the same object. It's ok since we are just testing uuids
    uuids.push_back(ChannelzRegistry::Default()->Register(&object_to_register));
  }
  for (size_t i = 1; i < uuids.size(); ++i) {
    ASSERT_LT(uuids[i - 1], uuids[i]) << "Uuids must always be increasing";
  }
}

TEST(ChannelzRegistryTest, RegisterGetTest) {
  int object_to_register = 42;
  intptr_t uuid = ChannelzRegistry::Default()->Register(&object_to_register);
  int* retrieved = ChannelzRegistry::Default()->Get<int>(uuid);
  ASSERT_EQ(object_to_register, *retrieved);
  ASSERT_EQ(&object_to_register, retrieved);
}

TEST(ChannelzRegistryTest, MultipleTypeTest) {
  int int_to_register = 42;
  intptr_t int_uuid = ChannelzRegistry::Default()->Register(&int_to_register);
  std::string str_to_register = "hello world";
  intptr_t str_uuid = ChannelzRegistry::Default()->Register(&str_to_register);
  int* retrieved_int = ChannelzRegistry::Default()->Get<int>(int_uuid);
  std::string* retrieved_str =
      ChannelzRegistry::Default()->Get<std::string>(str_uuid);
  ASSERT_EQ(int_to_register, *retrieved_int);
  ASSERT_EQ(&int_to_register, retrieved_int);
  ASSERT_STREQ(str_to_register.c_str(), (*retrieved_str).c_str());
  ASSERT_EQ(&str_to_register, retrieved_str);
}

namespace {
class Foo {
 public:
  int bar;
};
}  // namespace

TEST(ChannelzRegistryTest, CustomObjectTest) {
  Foo* foo = New<Foo>();
  foo->bar = 1024;
  intptr_t uuid = ChannelzRegistry::Default()->Register(foo);
  Foo* retrieved = ChannelzRegistry::Default()->Get<Foo>(uuid);
  ASSERT_EQ(foo, retrieved);
  ASSERT_EQ(foo->bar, retrieved->bar);
}

TEST(ChannelzRegistryTest, NullIfNotPresentTest) {
  int object_to_register = 42;
  intptr_t uuid = ChannelzRegistry::Default()->Register(&object_to_register);
  // try to pull out a uuid that does not exist.
  int* nonexistant = ChannelzRegistry::Default()->Get<int>(1234);
  ASSERT_EQ(nonexistant, nullptr);
  int* retrieved = ChannelzRegistry::Default()->Get<int>(uuid);
  ASSERT_EQ(object_to_register, *retrieved);
  ASSERT_EQ(&object_to_register, retrieved);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
