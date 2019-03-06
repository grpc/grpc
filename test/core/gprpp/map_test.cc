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

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/map.h"
#include <gtest/gtest.h>
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
TEST(MapTest, MapAddTest) {
  grpc_core::Map<char, int> map;
  map['a'] = 0;
  map['b'] = 1;
  map['c'] = 2;
  EXPECT_EQ(0, map['a']);
  EXPECT_EQ(1, map['b']);
  EXPECT_EQ(2, map['c']);
}

TEST(MapTest, MapRemoveTest) {
  grpc_core::Map<int, int> map;
  map['a'] = 0;
  map['b'] = 1;
  map['c'] = 2;
  map.erase('c');
  EXPECT_EQ(0, map['a']);
  EXPECT_EQ(1, map['b']);
  EXPECT_EQ(0, map.count('c'));
}

TEST(MapTest, MapEmptyAndSizeTest) {
  grpc_core::Map<int, int> map;
  EXPECT_TRUE(map.empty());
  map['a'] = 0;
  map['b'] = 1;
  map['c'] = 2;
  EXPECT_EQ(3, map.size());
}

TEST(MapTest, MapIterateTest) {
  grpc_core::Map<int, int> map;
  map['a'] = 0;
  map['b'] = 1;
  map['c'] = 2;
  int num_iterations = 0;
  for (auto it = map.begin(); it != map.end(); ++it) {
    ++num_iterations;
  }
  EXPECT_EQ(3, num_iterations);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

