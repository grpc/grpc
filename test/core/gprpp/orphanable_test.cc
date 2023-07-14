//
//
// Copyright 2017 gRPC authors.
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

#include "src/core/lib/gprpp/orphanable.h"

#include "gtest/gtest.h"

#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class Foo : public Orphanable {
 public:
  Foo() : Foo(0) {}
  explicit Foo(int value) : value_(value) {}
  void Orphan() override { delete this; }
  int value() const { return value_; }

 private:
  int value_;
};

TEST(Orphanable, Basic) {
  Foo* foo = new Foo();
  foo->Orphan();
}

TEST(OrphanablePtr, Basic) {
  OrphanablePtr<Foo> foo(new Foo());
  EXPECT_EQ(0, foo->value());
}

TEST(MakeOrphanable, DefaultConstructor) {
  auto foo = MakeOrphanable<Foo>();
  EXPECT_EQ(0, foo->value());
}

TEST(MakeOrphanable, WithParameters) {
  auto foo = MakeOrphanable<Foo>(5);
  EXPECT_EQ(5, foo->value());
}

class Bar : public InternallyRefCounted<Bar> {
 public:
  Bar() : Bar(0) {}
  explicit Bar(int value) : value_(value) {}
  void Orphan() override { Unref(); }
  int value() const { return value_; }

  void StartWork() { self_ref_ = Ref(); }
  void FinishWork() { self_ref_.reset(); }

 private:
  int value_;
  RefCountedPtr<Bar> self_ref_;
};

TEST(OrphanablePtr, InternallyRefCounted) {
  auto bar = MakeOrphanable<Bar>();
  bar->StartWork();
  bar->FinishWork();
}

class Baz : public InternallyRefCounted<Baz> {
 public:
  Baz() : Baz(0) {}
  explicit Baz(int value) : InternallyRefCounted<Baz>("Baz"), value_(value) {}
  void Orphan() override { Unref(); }
  int value() const { return value_; }

  void StartWork() { self_ref_ = Ref(DEBUG_LOCATION, "work"); }
  void FinishWork() {
    // This is a little ugly, but it makes the logged ref and unref match up.
    self_ref_.release();
    Unref(DEBUG_LOCATION, "work");
  }

 private:
  int value_;
  RefCountedPtr<Baz> self_ref_;
};

TEST(OrphanablePtr, InternallyRefCountedWithTracing) {
  auto baz = MakeOrphanable<Baz>();
  baz->StartWork();
  baz->FinishWork();
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
