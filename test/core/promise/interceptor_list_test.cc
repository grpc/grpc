// Copyright 2022 gRPC authors.
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

#include "src/core/lib/promise/interceptor_list.h"

#include "gtest/gtest.h"

#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/promise/test_context.h"

namespace grpc_core {
namespace {

class InterceptorListTest : public ::testing::Test {
 protected:
  MemoryAllocator memory_allocator_ = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
  ScopedArenaPtr arena_ = MakeScopedArena(1024, &memory_allocator_);
  TestContext<Arena> arena_ctx_{arena_.get()};
};

TEST_F(InterceptorListTest, NoOp) { InterceptorList<std::string>(); }

TEST_F(InterceptorListTest, CanRunOne) {
  InterceptorList<std::string> list;
  list.AppendMap([](std::string s) { return s + "a"; });
  EXPECT_EQ(list.Run("hello")(), Poll<absl::optional<std::string>>("helloa"));
}

TEST_F(InterceptorListTest, CanRunTwo) {
  InterceptorList<std::string> list;
  list.AppendMap([](std::string s) { return s + "a"; });
  list.AppendMap([](std::string s) { return s + "b"; });
  EXPECT_EQ(list.Run("hello")(), Poll<absl::optional<std::string>>("helloab"));
}

TEST_F(InterceptorListTest, CanRunTwoTwice) {
  InterceptorList<std::string> list;
  list.AppendMap([](std::string s) { return s + s; });
  list.AppendMap([](std::string s) { return s + s + s; });
  EXPECT_EQ(absl::get<kPollReadyIdx>(list.Run(std::string(10, 'a'))()).value(),
            std::string(60, 'a'));
  EXPECT_EQ(absl::get<kPollReadyIdx>(list.Run(std::string(100, 'b'))()).value(),
            std::string(600, 'b'));
}

TEST_F(InterceptorListTest, CanRunManyWithCaptures) {
  InterceptorList<std::string> list;
  for (size_t i = 0; i < 26 * 1000; i++) {
    list.AppendMap([i = std::make_shared<size_t>(i)](std::string s) {
      return s + static_cast<char>((*i % 26) + 'a');
    });
  }
  std::string expected;
  for (size_t i = 0; i < 1000; i++) {
    expected += "abcdefghijklmnopqrstuvwxyz";
  }
  EXPECT_EQ(absl::get<kPollReadyIdx>(list.Run("")()).value(), expected);
}

TEST_F(InterceptorListTest, CanRunManyWithCapturesThatDelay) {
  InterceptorList<std::string> list;
  for (size_t i = 0; i < 26 * 1000; i++) {
    list.AppendMap([i = std::make_shared<size_t>(i)](std::string s) {
      return [x = false, i, s]() mutable -> Poll<absl::optional<std::string>> {
        if (!x) {
          x = true;
          return Pending{};
        }
        return s + static_cast<char>((*i % 26) + 'a');
      };
    });
  }
  auto promise = list.Run("");
  for (size_t i = 0; i < 26 * 1000; i++) {
    EXPECT_TRUE(absl::holds_alternative<Pending>(promise())) << i;
  }
  std::string expected;
  for (size_t i = 0; i < 1000; i++) {
    expected += "abcdefghijklmnopqrstuvwxyz";
  }
  EXPECT_EQ(absl::get<kPollReadyIdx>(promise()).value(), expected);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
