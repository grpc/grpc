//
// Copyright 2020 gRPC authors.
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

#include "src/core/lib/gprpp/dual_ref_counted.h"

#include <set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class Foo : public DualRefCounted<Foo> {
 public:
  Foo() = default;
  ~Foo() override { GPR_ASSERT(shutting_down_); }

  void Orphan() override { shutting_down_ = true; }

 private:
  bool shutting_down_ = false;
};

TEST(DualRefCounted, Basic) {
  Foo* foo = new Foo();
  foo->Unref();
}

TEST(DualRefCounted, ExtraRef) {
  Foo* foo = new Foo();
  foo->Ref().release();
  foo->Unref();
  foo->Unref();
}

TEST(DualRefCounted, ExtraWeakRef) {
  Foo* foo = new Foo();
  foo->WeakRef().release();
  foo->Unref();
  foo->WeakUnref();
}

TEST(DualRefCounted, RefIfNonZero) {
  Foo* foo = new Foo();
  foo->WeakRef().release();
  {
    RefCountedPtr<Foo> foop = foo->RefIfNonZero();
    EXPECT_NE(foop.get(), nullptr);
  }
  foo->Unref();
  {
    RefCountedPtr<Foo> foop = foo->RefIfNonZero();
    EXPECT_EQ(foop.get(), nullptr);
  }
  foo->WeakUnref();
}

class FooWithTracing : public DualRefCounted<FooWithTracing> {
 public:
  FooWithTracing() : DualRefCounted("FooWithTracing") {}
  ~FooWithTracing() override { GPR_ASSERT(shutting_down_); }

  void Orphan() override { shutting_down_ = true; }

 private:
  bool shutting_down_ = false;
};

TEST(DualRefCountedWithTracing, Basic) {
  FooWithTracing* foo = new FooWithTracing();
  foo->Ref(DEBUG_LOCATION, "extra_ref").release();
  foo->Unref(DEBUG_LOCATION, "extra_ref");
  foo->WeakRef(DEBUG_LOCATION, "extra_ref").release();
  foo->WeakUnref(DEBUG_LOCATION, "extra_ref");
  // Can use the no-argument methods, too.
  foo->Ref().release();
  foo->Unref();
  foo->WeakRef().release();
  foo->WeakUnref();
  foo->Unref(DEBUG_LOCATION, "original_ref");
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
