//
// Copyright 2026 gRPC authors.
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

#include "src/core/util/atomic_utils.h"

#include <stdint.h>

#include <atomic>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

namespace grpc_core {
namespace {

TEST(IncrementIfNonzeroTest, DoesNotIncrementZero) {
  std::atomic<int> value{0};
  EXPECT_FALSE(IncrementIfNonzero(&value));
  EXPECT_EQ(value.load(), 0);
}

TEST(IncrementIfNonzeroTest, IncrementsNonzero) {
  std::atomic<int> value{1};
  EXPECT_TRUE(IncrementIfNonzero(&value));
  EXPECT_EQ(value.load(), 2);
  EXPECT_TRUE(IncrementIfNonzero(&value));
  EXPECT_EQ(value.load(), 3);
}

TEST(IncrementIfNonzeroTest, WorksWithOtherIntegerTypes) {
  std::atomic<intptr_t> intptr_value{41};
  EXPECT_TRUE(IncrementIfNonzero(&intptr_value));
  EXPECT_EQ(intptr_value.load(), 42);

  std::atomic<uint64_t> unsigned_value{0};
  EXPECT_FALSE(IncrementIfNonzero(&unsigned_value));
  EXPECT_EQ(unsigned_value.load(), 0u);
}

TEST(IncrementIfNonzeroTest, ConcurrentIncrementsAreAllCounted) {
  constexpr int kThreads = 8;
  constexpr int kIncrementsPerThread = 10000;
  std::atomic<int> value{1};
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&value]() {
      for (int j = 0; j < kIncrementsPerThread; ++j) {
        EXPECT_TRUE(IncrementIfNonzero(&value));
      }
    });
  }
  for (auto& t : threads) t.join();
  EXPECT_EQ(value.load(), 1 + kThreads * kIncrementsPerThread);
}

TEST(IncrementIfNonzeroTest, ZeroStaysZeroUnderContention) {
  constexpr int kThreads = 8;
  constexpr int kAttemptsPerThread = 10000;
  std::atomic<int> value{0};
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&value]() {
      for (int j = 0; j < kAttemptsPerThread; ++j) {
        EXPECT_FALSE(IncrementIfNonzero(&value));
      }
    });
  }
  for (auto& t : threads) t.join();
  EXPECT_EQ(value.load(), 0);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}