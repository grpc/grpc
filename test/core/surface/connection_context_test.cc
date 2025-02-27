//
//
// Copyright 2024 gRPC authors.
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

#include "src/core/lib/surface/connection_context.h"

#include "gtest/gtest.h"
#include "src/core/util/orphanable.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

class Foo {
 public:
  explicit Foo(double value) : value_(value) {}
  double value() const { return value_; }

 private:
  double value_;
};

class Bar {
 public:
  explicit Bar(int value) : value_(value) {}
  int value() const { return value_; }

 private:
  int value_;
};

}  // namespace

template <>
struct ConnectionContextProperty<Foo> {};

template <>
struct ConnectionContextProperty<Bar> {};

TEST(ConnectionAuthContextTest, SimpleStaticPropertyAdditionContext) {
  OrphanablePtr<ConnectionContext> map = ConnectionContext::Create();
  EXPECT_TRUE(map->EmplaceIfUnset<Foo>(3.0));
  EXPECT_EQ(map->Get<Foo>()->value(), 3.0);
  EXPECT_FALSE(map->EmplaceIfUnset<Foo>(1.0));
  EXPECT_EQ(map->Get<Foo>()->value(), 3.0);
  map->Update<Foo>(2.0);
  EXPECT_EQ(map->Get<Foo>()->value(), 2.0);

  EXPECT_TRUE(map->EmplaceIfUnset<Bar>(1));
  EXPECT_EQ(map->Get<Bar>()->value(), 1);
  EXPECT_FALSE(map->EmplaceIfUnset<Bar>(2));
  EXPECT_EQ(map->Get<Bar>()->value(), 1);
  map->Update<Bar>(1234);
  EXPECT_EQ(map->Get<Bar>()->value(), 1234);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
