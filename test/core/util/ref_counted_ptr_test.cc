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

#include "src/core/util/ref_counted_ptr.h"

#include <memory>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "gtest/gtest.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/ref_counted.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

//
// RefCountedPtr<> tests
//

class Foo : public RefCounted<Foo> {
 public:
  Foo() : value_(0) {}

  explicit Foo(int value) : value_(value) {}

  int value() const { return value_; }

 private:
  int value_;
};

TEST(RefCountedPtr, DefaultConstructor) { RefCountedPtr<Foo> foo; }

TEST(RefCountedPtr, ExplicitConstructorEmpty) {
  RefCountedPtr<Foo> foo(nullptr);
}

TEST(RefCountedPtr, ExplicitConstructor) { RefCountedPtr<Foo> foo(new Foo()); }

TEST(RefCountedPtr, MoveConstructor) {
  RefCountedPtr<Foo> foo(new Foo());
  RefCountedPtr<Foo> foo2(std::move(foo));
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_EQ(nullptr, foo.get());
  EXPECT_NE(nullptr, foo2.get());
}

TEST(RefCountedPtr, MoveAssignment) {
  RefCountedPtr<Foo> foo(new Foo());
  RefCountedPtr<Foo> foo2 = std::move(foo);
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_EQ(nullptr, foo.get());
  EXPECT_NE(nullptr, foo2.get());
}

TEST(RefCountedPtr, CopyConstructor) {
  RefCountedPtr<Foo> foo(new Foo());
  RefCountedPtr<Foo> foo2(foo);
  EXPECT_NE(nullptr, foo.get());
  EXPECT_EQ(foo.get(), foo2.get());
}

TEST(RefCountedPtr, CopyAssignment) {
  RefCountedPtr<Foo> foo(new Foo());
  RefCountedPtr<Foo> foo2 = foo;
  EXPECT_NE(nullptr, foo.get());
  EXPECT_EQ(foo.get(), foo2.get());
}

TEST(RefCountedPtr, CopyAssignmentWhenEmpty) {
  RefCountedPtr<Foo> foo;
  RefCountedPtr<Foo> foo2;
  foo2 = foo;
  EXPECT_EQ(nullptr, foo.get());
  EXPECT_EQ(nullptr, foo2.get());
}

TEST(RefCountedPtr, CopyAssignmentToSelf) {
  RefCountedPtr<Foo> foo(new Foo());
  foo = *&foo;  // The "*&" avoids warnings from LLVM -Wself-assign.
}

TEST(RefCountedPtr, EnclosedScope) {
  RefCountedPtr<Foo> foo(new Foo());
  {
    RefCountedPtr<Foo> foo2(std::move(foo));
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_EQ(nullptr, foo.get());
    EXPECT_NE(nullptr, foo2.get());
  }
  EXPECT_EQ(nullptr, foo.get());
}

TEST(RefCountedPtr, ResetFromNullToNonNull) {
  RefCountedPtr<Foo> foo;
  EXPECT_EQ(nullptr, foo.get());
  foo.reset(new Foo());
  EXPECT_NE(nullptr, foo.get());
}

TEST(RefCountedPtr, ResetFromNonNullToNonNull) {
  RefCountedPtr<Foo> foo(new Foo());
  EXPECT_NE(nullptr, foo.get());
  Foo* original = foo.get();
  foo.reset(new Foo());
  EXPECT_NE(nullptr, foo.get());
  EXPECT_NE(original, foo.get());
}

TEST(RefCountedPtr, ResetFromNonNullToNull) {
  RefCountedPtr<Foo> foo(new Foo());
  EXPECT_NE(nullptr, foo.get());
  foo.reset();
  EXPECT_EQ(nullptr, foo.get());
}

TEST(RefCountedPtr, ResetFromNullToNull) {
  RefCountedPtr<Foo> foo;
  EXPECT_EQ(nullptr, foo.get());
  foo.reset();
  EXPECT_EQ(nullptr, foo.get());
}

TEST(RefCountedPtr, DereferenceOperators) {
  RefCountedPtr<Foo> foo(new Foo());
  foo->value();
  Foo& foo_ref = *foo;
  foo_ref.value();
}

TEST(RefCountedPtr, EqualityOperators) {
  RefCountedPtr<Foo> foo(new Foo());
  RefCountedPtr<Foo> bar = foo;
  RefCountedPtr<Foo> empty;
  // Test equality between RefCountedPtrs.
  EXPECT_EQ(foo, bar);
  EXPECT_NE(foo, empty);
  // Test equality with bare pointers.
  EXPECT_EQ(foo, foo.get());
  EXPECT_EQ(empty, nullptr);
  EXPECT_NE(foo, nullptr);
}

TEST(RefCountedPtr, Swap) {
  Foo* foo = new Foo();
  Foo* bar = new Foo();
  RefCountedPtr<Foo> ptr1(foo);
  RefCountedPtr<Foo> ptr2(bar);
  ptr1.swap(ptr2);
  EXPECT_EQ(foo, ptr2.get());
  EXPECT_EQ(bar, ptr1.get());
  RefCountedPtr<Foo> ptr3;
  ptr3.swap(ptr2);
  EXPECT_EQ(nullptr, ptr2.get());
  EXPECT_EQ(foo, ptr3.get());
}

TEST(MakeRefCounted, NoArgs) {
  RefCountedPtr<Foo> foo = MakeRefCounted<Foo>();
  EXPECT_EQ(0, foo->value());
}

TEST(MakeRefCounted, Args) {
  RefCountedPtr<Foo> foo = MakeRefCounted<Foo>(3);
  EXPECT_EQ(3, foo->value());
}

class FooWithTracing : public RefCounted<FooWithTracing> {
 public:
  FooWithTracing() : RefCounted("FooWithTracing") {}
};

TEST(RefCountedPtr, RefCountedWithTracing) {
  RefCountedPtr<FooWithTracing> foo(new FooWithTracing());
  RefCountedPtr<FooWithTracing> foo2 = foo->Ref(DEBUG_LOCATION, "foo");
  foo2.release();
  RefCountedPtr<FooWithTracing> foo3 = foo.Ref(DEBUG_LOCATION, "foo");
  foo3.release();
  foo->Unref(DEBUG_LOCATION, "foo");
  foo->Unref(DEBUG_LOCATION, "foo");
}

class BaseClass : public RefCounted<BaseClass> {
 public:
  BaseClass() {}
};

class Subclass : public BaseClass {
 public:
  Subclass() {}
};

TEST(RefCountedPtr, ConstructFromSubclass) {
  RefCountedPtr<BaseClass> p(new Subclass());
}

TEST(RefCountedPtr, CopyAssignFromSubclass) {
  RefCountedPtr<BaseClass> b;
  EXPECT_EQ(nullptr, b.get());
  RefCountedPtr<Subclass> s = MakeRefCounted<Subclass>();
  b = s;
  EXPECT_NE(nullptr, b.get());
}

TEST(RefCountedPtr, MoveAssignFromSubclass) {
  RefCountedPtr<BaseClass> b;
  EXPECT_EQ(nullptr, b.get());
  RefCountedPtr<Subclass> s = MakeRefCounted<Subclass>();
  b = std::move(s);
  EXPECT_NE(nullptr, b.get());
}

TEST(RefCountedPtr, ResetFromSubclass) {
  RefCountedPtr<BaseClass> b;
  EXPECT_EQ(nullptr, b.get());
  b.reset(new Subclass());
  EXPECT_NE(nullptr, b.get());
}

TEST(RefCountedPtr, EqualityWithSubclass) {
  Subclass* s = new Subclass();
  RefCountedPtr<BaseClass> b(s);
  EXPECT_EQ(b, s);
}

void FunctionTakingBaseClass(RefCountedPtr<BaseClass>) {}

TEST(RefCountedPtr, CanPassSubclassToFunctionExpectingBaseClass) {
  RefCountedPtr<Subclass> p = MakeRefCounted<Subclass>();
  FunctionTakingBaseClass(p);
}

void FunctionTakingSubclass(RefCountedPtr<Subclass>) {}

TEST(RefCountedPtr, CanPassSubclassToFunctionExpectingSubclass) {
  RefCountedPtr<Subclass> p = MakeRefCounted<Subclass>();
  FunctionTakingSubclass(p);
}

TEST(RefCountedPtr, TakeAsSubclass) {
  RefCountedPtr<BaseClass> p = MakeRefCounted<Subclass>();
  auto s = p.TakeAsSubclass<Subclass>();
  EXPECT_EQ(p.get(), nullptr);
  EXPECT_NE(s.get(), nullptr);
}

//
// WeakRefCountedPtr<> tests
//

class Bar : public DualRefCounted<Bar> {
 public:
  Bar() : value_(0) {}

  explicit Bar(int value) : value_(value) {}

  ~Bar() override { CHECK(shutting_down_); }

  void Orphaned() override { shutting_down_ = true; }

  int value() const { return value_; }

 private:
  int value_;
  bool shutting_down_ = false;
};

TEST(WeakRefCountedPtr, DefaultConstructor) { WeakRefCountedPtr<Bar> bar; }

TEST(WeakRefCountedPtr, ExplicitConstructorEmpty) {
  WeakRefCountedPtr<Bar> bar(nullptr);
}

TEST(WeakRefCountedPtr, ExplicitConstructor) {
  RefCountedPtr<Bar> bar_strong(new Bar());
  bar_strong->WeakRef().release();
  WeakRefCountedPtr<Bar> bar(bar_strong.get());
}

TEST(WeakRefCountedPtr, MoveConstructor) {
  RefCountedPtr<Bar> bar_strong(new Bar());
  WeakRefCountedPtr<Bar> bar = bar_strong->WeakRef();
  WeakRefCountedPtr<Bar> bar2(std::move(bar));
  EXPECT_EQ(nullptr, bar.get());  // NOLINT
  EXPECT_NE(nullptr, bar2.get());
}

TEST(WeakRefCountedPtr, MoveAssignment) {
  RefCountedPtr<Bar> bar_strong(new Bar());
  WeakRefCountedPtr<Bar> bar = bar_strong->WeakRef();
  WeakRefCountedPtr<Bar> bar2 = std::move(bar);
  EXPECT_EQ(nullptr, bar.get());  // NOLINT
  EXPECT_NE(nullptr, bar2.get());
}

TEST(WeakRefCountedPtr, CopyConstructor) {
  RefCountedPtr<Bar> bar_strong(new Bar());
  WeakRefCountedPtr<Bar> bar = bar_strong->WeakRef();
  WeakRefCountedPtr<Bar> bar2(bar);
  EXPECT_NE(nullptr, bar.get());
  EXPECT_EQ(bar.get(), bar2.get());
}

TEST(WeakRefCountedPtr, CopyAssignment) {
  RefCountedPtr<Bar> bar_strong(new Bar());
  WeakRefCountedPtr<Bar> bar = bar_strong->WeakRef();
  WeakRefCountedPtr<Bar> bar2 = bar;
  EXPECT_NE(nullptr, bar.get());
  EXPECT_EQ(bar.get(), bar2.get());
}

TEST(WeakRefCountedPtr, CopyAssignmentWhenEmpty) {
  WeakRefCountedPtr<Bar> bar;
  WeakRefCountedPtr<Bar> bar2;
  bar2 = bar;
  EXPECT_EQ(nullptr, bar.get());
  EXPECT_EQ(nullptr, bar2.get());
}

TEST(WeakRefCountedPtr, CopyAssignmentToSelf) {
  RefCountedPtr<Bar> bar_strong(new Bar());
  WeakRefCountedPtr<Bar> bar = bar_strong->WeakRef();
  bar = *&bar;  // The "*&" avoids warnings from LLVM -Wself-assign.
}

TEST(WeakRefCountedPtr, EnclosedScope) {
  RefCountedPtr<Bar> bar_strong(new Bar());
  WeakRefCountedPtr<Bar> bar = bar_strong->WeakRef();
  {
    WeakRefCountedPtr<Bar> bar2(std::move(bar));
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_EQ(nullptr, bar.get());
    EXPECT_NE(nullptr, bar2.get());
  }
  EXPECT_EQ(nullptr, bar.get());
}

TEST(WeakRefCountedPtr, ResetFromNullToNonNull) {
  RefCountedPtr<Bar> bar_strong(new Bar());
  WeakRefCountedPtr<Bar> bar;
  EXPECT_EQ(nullptr, bar.get());
  bar_strong->WeakRef().release();
  bar.reset(bar_strong.get());
  EXPECT_NE(nullptr, bar.get());
}

TEST(WeakRefCountedPtr, ResetFromNonNullToNonNull) {
  RefCountedPtr<Bar> bar_strong(new Bar());
  RefCountedPtr<Bar> bar2_strong(new Bar());
  WeakRefCountedPtr<Bar> bar = bar_strong->WeakRef();
  EXPECT_NE(nullptr, bar.get());
  bar2_strong->WeakRef().release();
  bar.reset(bar2_strong.get());
  EXPECT_NE(nullptr, bar.get());
  EXPECT_NE(bar_strong.get(), bar.get());
}

TEST(WeakRefCountedPtr, ResetFromNonNullToNull) {
  RefCountedPtr<Bar> bar_strong(new Bar());
  WeakRefCountedPtr<Bar> bar = bar_strong->WeakRef();
  EXPECT_NE(nullptr, bar.get());
  bar.reset();
  EXPECT_EQ(nullptr, bar.get());
}

TEST(WeakRefCountedPtr, ResetFromNullToNull) {
  WeakRefCountedPtr<Bar> bar;
  EXPECT_EQ(nullptr, bar.get());
  bar.reset();
  EXPECT_EQ(nullptr, bar.get());
}

TEST(WeakRefCountedPtr, DereferenceOperators) {
  RefCountedPtr<Bar> bar_strong(new Bar());
  WeakRefCountedPtr<Bar> bar = bar_strong->WeakRef();
  bar->value();
  Bar& bar_ref = *bar;
  bar_ref.value();
}

TEST(WeakRefCountedPtr, EqualityOperators) {
  RefCountedPtr<Bar> bar_strong(new Bar());
  WeakRefCountedPtr<Bar> bar = bar_strong->WeakRef();
  WeakRefCountedPtr<Bar> bar2 = bar;
  WeakRefCountedPtr<Bar> empty;
  // Test equality between RefCountedPtrs.
  EXPECT_EQ(bar, bar2);
  EXPECT_NE(bar, empty);
  // Test equality with bare pointers.
  EXPECT_EQ(bar, bar.get());
  EXPECT_EQ(empty, nullptr);
  EXPECT_NE(bar, nullptr);
}

TEST(WeakRefCountedPtr, Swap) {
  RefCountedPtr<Bar> bar_strong(new Bar());
  RefCountedPtr<Bar> bar2_strong(new Bar());
  WeakRefCountedPtr<Bar> bar = bar_strong->WeakRef();
  WeakRefCountedPtr<Bar> bar2 = bar2_strong->WeakRef();
  bar.swap(bar2);
  EXPECT_EQ(bar_strong.get(), bar2.get());
  EXPECT_EQ(bar2_strong.get(), bar.get());
  WeakRefCountedPtr<Bar> bar3;
  bar3.swap(bar2);
  EXPECT_EQ(nullptr, bar2.get());
  EXPECT_EQ(bar_strong.get(), bar3.get());
}

class BarWithTracing : public DualRefCounted<BarWithTracing> {
 public:
  BarWithTracing() : DualRefCounted("BarWithTracing") {}

  ~BarWithTracing() override { CHECK(shutting_down_); }

  void Orphaned() override { shutting_down_ = true; }

 private:
  bool shutting_down_ = false;
};

TEST(WeakRefCountedPtr, RefCountedWithTracing) {
  RefCountedPtr<BarWithTracing> bar_strong(new BarWithTracing());
  WeakRefCountedPtr<BarWithTracing> bar = bar_strong->WeakRef();
  WeakRefCountedPtr<BarWithTracing> bar2 = bar->WeakRef(DEBUG_LOCATION, "bar");
  bar2.release();
  WeakRefCountedPtr<BarWithTracing> bar3 = bar.WeakRef(DEBUG_LOCATION, "bar");
  bar3.release();
  bar->WeakUnref(DEBUG_LOCATION, "bar");
  bar->WeakUnref(DEBUG_LOCATION, "bar");
}

class WeakBaseClass : public DualRefCounted<WeakBaseClass> {
 public:
  WeakBaseClass() {}

  ~WeakBaseClass() override { CHECK(shutting_down_); }

  void Orphaned() override { shutting_down_ = true; }

 private:
  bool shutting_down_ = false;
};

class WeakSubclass : public WeakBaseClass {
 public:
  WeakSubclass() {}
};

TEST(WeakRefCountedPtr, ConstructFromWeakSubclass) {
  RefCountedPtr<WeakSubclass> strong(new WeakSubclass());
  WeakRefCountedPtr<WeakBaseClass> p(strong->WeakRef().release());
}

TEST(WeakRefCountedPtr, CopyAssignFromWeakSubclass) {
  RefCountedPtr<WeakSubclass> strong(new WeakSubclass());
  WeakRefCountedPtr<WeakBaseClass> b;
  EXPECT_EQ(nullptr, b.get());
  WeakRefCountedPtr<WeakSubclass> s = strong->WeakRefAsSubclass<WeakSubclass>();
  b = s;
  EXPECT_NE(nullptr, b.get());
}

TEST(WeakRefCountedPtr, MoveAssignFromWeakSubclass) {
  RefCountedPtr<WeakSubclass> strong(new WeakSubclass());
  WeakRefCountedPtr<WeakBaseClass> b;
  EXPECT_EQ(nullptr, b.get());
  WeakRefCountedPtr<WeakSubclass> s = strong->WeakRefAsSubclass<WeakSubclass>();
  b = std::move(s);
  EXPECT_NE(nullptr, b.get());
}

TEST(WeakRefCountedPtr, ResetFromWeakSubclass) {
  RefCountedPtr<WeakSubclass> strong(new WeakSubclass());
  WeakRefCountedPtr<WeakBaseClass> b;
  EXPECT_EQ(nullptr, b.get());
  b.reset(strong->WeakRefAsSubclass<WeakSubclass>().release());
  EXPECT_NE(nullptr, b.get());
}

TEST(WeakRefCountedPtr, EqualityWithWeakSubclass) {
  RefCountedPtr<WeakSubclass> strong(new WeakSubclass());
  WeakRefCountedPtr<WeakBaseClass> b = strong->WeakRef();
  EXPECT_EQ(b, strong.get());
}

void FunctionTakingWeakBaseClass(WeakRefCountedPtr<WeakBaseClass>) {}

TEST(WeakRefCountedPtr, CanPassWeakSubclassToFunctionExpectingWeakBaseClass) {
  RefCountedPtr<WeakSubclass> strong(new WeakSubclass());
  WeakRefCountedPtr<WeakSubclass> p = strong->WeakRefAsSubclass<WeakSubclass>();
  FunctionTakingWeakBaseClass(p);
}

void FunctionTakingWeakSubclass(WeakRefCountedPtr<WeakSubclass>) {}

TEST(WeakRefCountedPtr, CanPassWeakSubclassToFunctionExpectingWeakSubclass) {
  RefCountedPtr<WeakSubclass> strong(new WeakSubclass());
  WeakRefCountedPtr<WeakSubclass> p = strong->WeakRefAsSubclass<WeakSubclass>();
  FunctionTakingWeakSubclass(p);
}

TEST(WeakRefCountedPtr, TakeAsSubclass) {
  RefCountedPtr<WeakBaseClass> strong = MakeRefCounted<WeakSubclass>();
  WeakRefCountedPtr<WeakBaseClass> p = strong->WeakRef();
  WeakRefCountedPtr<WeakSubclass> s = p.TakeAsSubclass<WeakSubclass>();
  EXPECT_EQ(p.get(), nullptr);
  EXPECT_NE(s.get(), nullptr);
}

//
// tests for absl hash integration
//

TEST(AbslHashIntegration, RefCountedPtr) {
  absl::flat_hash_set<RefCountedPtr<Foo>> set;
  auto p = MakeRefCounted<Foo>(5);
  set.insert(p);
  auto it = set.find(p);
  ASSERT_NE(it, set.end());
  EXPECT_EQ(*it, p);
}

TEST(AbslHashIntegration, WeakRefCountedPtr) {
  absl::flat_hash_set<WeakRefCountedPtr<Bar>> set;
  auto p = MakeRefCounted<Bar>(5);
  auto q = p->WeakRef();
  set.insert(q);
  auto it = set.find(q);
  ASSERT_NE(it, set.end());
  EXPECT_EQ(*it, q);
}

TEST(AbslHashIntegration, RefCountedPtrHeterogenousLookup) {
  absl::flat_hash_set<RefCountedPtr<Bar>, RefCountedPtrHash<Bar>,
                      RefCountedPtrEq<Bar>>
      set;
  auto p = MakeRefCounted<Bar>(5);
  set.insert(p);
  auto it = set.find(p);
  ASSERT_NE(it, set.end());
  EXPECT_EQ(*it, p);
  auto q = p->WeakRef();
  it = set.find(q);
  ASSERT_NE(it, set.end());
  EXPECT_EQ(*it, p);
  it = set.find(p.get());
  ASSERT_NE(it, set.end());
  EXPECT_EQ(*it, p);
}

TEST(AbslHashIntegration, WeakRefCountedPtrHeterogenousLookup) {
  absl::flat_hash_set<WeakRefCountedPtr<Bar>, RefCountedPtrHash<Bar>,
                      RefCountedPtrEq<Bar>>
      set;
  auto p = MakeRefCounted<Bar>(5);
  auto q = p->WeakRef();
  set.insert(q);
  auto it = set.find(q);
  ASSERT_NE(it, set.end());
  EXPECT_EQ(*it, q);
  it = set.find(p);
  ASSERT_NE(it, set.end());
  EXPECT_EQ(*it, q);
  it = set.find(p.get());
  ASSERT_NE(it, set.end());
  EXPECT_EQ(*it, q);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
