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

#include "src/core/lib/support/any.h"
#include <gtest/gtest.h>
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

TEST(Any, NoOp) { Any<> any; }

TEST(Any, Int) {
  Any<> any(3);
  Any<> any2(4);
  EXPECT_EQ(3, *any.as<int>());
  Any<> other = any;
  EXPECT_EQ(3, *other.as<int>());
  Any<> other2(std::move(other));
  EXPECT_EQ(3, *other2.as<int>());
  other = std::move(any2);
  EXPECT_EQ(4, *other.as<int>());
}

TEST(Any, Big) {
  struct Big {
    void* ignored;
    int i;
  };
  Any<> any(Big{nullptr, 3});
  Any<> any2(Big{nullptr, 4});
  EXPECT_EQ(3, any.as<Big>()->i);
  Any<> other = any;
  EXPECT_EQ(3, other.as<Big>()->i);
  Any<> other2(std::move(other));
  EXPECT_EQ(3, other2.as<Big>()->i);
  other = std::move(any2);
  EXPECT_EQ(4, other.as<Big>()->i);

  Any<> iany(5);
  other = iany;
  EXPECT_EQ(5, *other.as<int>());
}

TEST(Any, WrongType) {
  Any<> a;
  Any<> b(3);
  EXPECT_EQ(nullptr, a.as<int>());
  EXPECT_EQ(nullptr, a.as<float>());
  EXPECT_EQ(nullptr, b.as<float>());
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
