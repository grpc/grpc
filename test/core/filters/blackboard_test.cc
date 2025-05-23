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

#include "src/core/filter/blackboard.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

class FooEntry : public Blackboard::Entry {
 public:
  static UniqueTypeName Type() {
    static UniqueTypeName::Factory kFactory("FooEntry");
    return kFactory.Create();
  }
};

class BarEntry : public Blackboard::Entry {
 public:
  static UniqueTypeName Type() {
    static UniqueTypeName::Factory kFactory("BarEntry");
    return kFactory.Create();
  }
};

TEST(Blackboard, Basic) {
  auto blackboard = MakeRefCounted<Blackboard>();
  // No entry for type FooEntry key "foo".
  EXPECT_EQ(blackboard->Get<FooEntry>("a"), nullptr);
  // Set entry for type FooEntry key "foo".
  auto foo_entry = MakeRefCounted<FooEntry>();
  auto foo_entry_actual = blackboard->Set("a", foo_entry);
  EXPECT_EQ(foo_entry_actual, foo_entry);
  // Get the entry we just added.
  EXPECT_EQ(blackboard->Get<FooEntry>("a"), foo_entry);
  // Re-add the entry, which should return the original entry.
  EXPECT_EQ(blackboard->Set("a", MakeRefCounted<FooEntry>()), foo_entry);
  // A different key for the same type is still unset.
  EXPECT_EQ(blackboard->Get<FooEntry>("b"), nullptr);
  // The same key for a different type is still unset.
  EXPECT_EQ(blackboard->Get<BarEntry>("a"), nullptr);
  // Set entry for type BarEntry key "foo".
  auto bar_entry = MakeRefCounted<BarEntry>();
  auto bar_entry_actual = blackboard->Set("a", bar_entry);
  EXPECT_EQ(bar_entry_actual, bar_entry);
  EXPECT_EQ(blackboard->Get<BarEntry>("a"), bar_entry);
  // This should not have replaced the same key for FooEntry.
  EXPECT_EQ(blackboard->Get<FooEntry>("a"), foo_entry);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
