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

#include "src/core/lib/security/context/connection_auth_context.h"

#include <gtest/gtest.h>

#include <cstdint>

#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

class Foo {
 public:
  explicit Foo(const char* value) : value_(value) {}
  const char* value() const { return value_; }

 private:
  const char* value_;
};

struct FooAuthProperty {
  using ValueType = Foo;
  static const char* name() { return name_; }
  static uint16_t id() { return id_; }
  static void Destroy(Foo* value) { delete value; }
  static Foo* Construct(const char* value) { return new Foo(value); }

 private:
  static uint16_t id_;
  static constexpr const char* name_ = "grpc.foo.auth.property";
};

class Bar {
 public:
  explicit Bar(int value) : value_(value) {}
  int value() const { return value_; }

 private:
  int value_;
};

struct BarAuthProperty {
  using ValueType = Bar;
  static const char* name() { return name_; }
  static uint16_t id() { return id_; }
  static void Destroy(Bar* value) { delete value; }
  static Bar* Construct(int value) { return new Bar(value); }

 private:
  static uint16_t id_;
  static constexpr const char* name_ = "grpc.bar.auth.property";
};

uint16_t FooAuthProperty::id_ =
    auth_context_detail::BaseAuthPropertiesTraits::AllocateId<
        FooAuthProperty>();

uint16_t BarAuthProperty::id_ =
    auth_context_detail::BaseAuthPropertiesTraits::AllocateId<
        BarAuthProperty>();

TEST(ConnectionAuthContextTest, SimpleStaticPropertyAdditionContext) {
  OrphanablePtr<ConnectionAuthContext> map = ConnectionAuthContext::Create();
  EXPECT_TRUE(map->SetIfUnset(FooAuthProperty(), "foo"));
  EXPECT_EQ(map->Get<FooAuthProperty>()->value(), "foo");
  EXPECT_FALSE(map->SetIfUnset(FooAuthProperty(), "bar"));
  EXPECT_EQ(map->Get<FooAuthProperty>()->value(), "foo");
  map->Update(FooAuthProperty(), "bar");
  EXPECT_EQ(map->Get<FooAuthProperty>()->value(), "bar");

  EXPECT_TRUE(map->SetIfUnset(BarAuthProperty(), 1));
  EXPECT_EQ(map->Get<BarAuthProperty>()->value(), 1);
  EXPECT_FALSE(map->SetIfUnset(BarAuthProperty(), 2));
  EXPECT_EQ(map->Get<BarAuthProperty>()->value(), 1);
  map->Update(BarAuthProperty(), 1234);
  EXPECT_EQ(map->Get<BarAuthProperty>()->value(), 1234);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
