// Copyright 2021 gRPC authors.
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

#include "src/core/lib/promise/context.h"

#include <memory>

#include "gtest/gtest.h"

namespace grpc_core {

struct TestContext {
  bool done = false;
};

template <>
struct ContextType<TestContext> {};

TEST(Context, WithContext) {
  EXPECT_FALSE(HasContext<TestContext>());
  TestContext test;
  EXPECT_FALSE(HasContext<TestContext>());
  EXPECT_FALSE(test.done);
  WithContext(
      []() {
        EXPECT_TRUE(HasContext<TestContext>());
        GetContext<TestContext>()->done = true;
      },
      &test)();
  EXPECT_FALSE(HasContext<TestContext>());
  EXPECT_TRUE(test.done);
}

class BaseContext {
 public:
  virtual int Answer() = 0;

 protected:
  ~BaseContext() = default;
};

class CorrectContext final : public BaseContext {
 public:
  int Answer() override { return 42; }
};

class IncorrectContext final : public BaseContext {
 public:
  int Answer() override { return 0; }
};

template <>
struct ContextType<BaseContext> {};
template <>
struct ContextSubclass<CorrectContext> {
  using Base = BaseContext;
};
template <>
struct ContextSubclass<IncorrectContext> {
  using Base = BaseContext;
};

TEST(Context, ContextSubclass) {
  CorrectContext correct;
  IncorrectContext incorrect;
  EXPECT_EQ(42,
            WithContext([]() { return GetContext<BaseContext>()->Answer(); },
                        &correct)());
  EXPECT_EQ(0, WithContext([]() { return GetContext<BaseContext>()->Answer(); },
                           &incorrect)());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
