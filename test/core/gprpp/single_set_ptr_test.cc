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

#include "src/core/lib/gprpp/single_set_ptr.h"

#include <algorithm>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {

TEST(SingleSetPtrTest, NoOp) { SingleSetPtr<int>(); }

TEST(SingleSetPtrTest, CanSet) {
  SingleSetPtr<int> p;
  EXPECT_FALSE(p.is_set());
  EXPECT_DEATH_IF_SUPPORTED(gpr_log(GPR_ERROR, "%d", *p), "");
  p.Set(new int(42));
  EXPECT_EQ(*p, 42);
}

TEST(SingleSetPtrTest, CanReset) {
  SingleSetPtr<int> p;
  EXPECT_FALSE(p.is_set());
  p.Set(new int(42));
  EXPECT_TRUE(p.is_set());
  p.Set(new int(43));
  EXPECT_EQ(*p, 42);
  p.Reset();
  EXPECT_FALSE(p.is_set());
}

TEST(SingleSetPtrTest, LotsOfSetters) {
  SingleSetPtr<int> p;
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; i++) {
    threads.emplace_back([&p, i]() { p.Set(new int(i)); });
  }
  for (auto& t : threads) {
    t.join();
  }
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
