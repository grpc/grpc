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

#include "src/core/lib/gprpp/ref_counted_ptr.h"

#include <gtest/gtest.h>

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

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

TEST(RefCountedPtr, ExplicitConstructor) { RefCountedPtr<Foo> foo(New<Foo>()); }

TEST(RefCountedPtr, MoveConstructor) {
  RefCountedPtr<Foo> foo(New<Foo>());
  RefCountedPtr<Foo> foo2(std::move(foo));
  EXPECT_EQ(nullptr, foo.get());
  EXPECT_NE(nullptr, foo2.get());
}

TEST(RefCountedPtr, MoveAssignment) {
  RefCountedPtr<Foo> foo(New<Foo>());
  RefCountedPtr<Foo> foo2 = std::move(foo);
  EXPECT_EQ(nullptr, foo.get());
  EXPECT_NE(nullptr, foo2.get());
}

TEST(RefCountedPtr, CopyConstructor) {
  RefCountedPtr<Foo> foo(New<Foo>());
  RefCountedPtr<Foo> foo2(foo);
  EXPECT_NE(nullptr, foo.get());
  EXPECT_EQ(foo.get(), foo2.get());
}

TEST(RefCountedPtr, CopyAssignment) {
  RefCountedPtr<Foo> foo(New<Foo>());
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
  RefCountedPtr<Foo> foo(New<Foo>());
  foo = *&foo;  // The "*&" avoids warnings from LLVM -Wself-assign.
}

TEST(RefCountedPtr, EnclosedScope) {
  RefCountedPtr<Foo> foo(New<Foo>());
  {
    RefCountedPtr<Foo> foo2(std::move(foo));
    EXPECT_EQ(nullptr, foo.get());
    EXPECT_NE(nullptr, foo2.get());
  }
  EXPECT_EQ(nullptr, foo.get());
}

TEST(RefCountedPtr, ResetFromNullToNonNull) {
  RefCountedPtr<Foo> foo;
  EXPECT_EQ(nullptr, foo.get());
  foo.reset(New<Foo>());
  EXPECT_NE(nullptr, foo.get());
}

TEST(RefCountedPtr, ResetFromNonNullToNonNull) {
  RefCountedPtr<Foo> foo(New<Foo>());
  EXPECT_NE(nullptr, foo.get());
  Foo* original = foo.get();
  foo.reset(New<Foo>());
  EXPECT_NE(nullptr, foo.get());
  EXPECT_NE(original, foo.get());
}

TEST(RefCountedPtr, ResetFromNonNullToNull) {
  RefCountedPtr<Foo> foo(New<Foo>());
  EXPECT_NE(nullptr, foo.get());
  foo.reset();
  EXPECT_EQ(nullptr, foo.get());
}

TEST(RefCountedPtr, ResetFromNullToNull) {
  RefCountedPtr<Foo> foo;
  EXPECT_EQ(nullptr, foo.get());
  foo.reset(nullptr);
  EXPECT_EQ(nullptr, foo.get());
}

TEST(RefCountedPtr, DerefernceOperators) {
  RefCountedPtr<Foo> foo(New<Foo>());
  foo->value();
  Foo& foo_ref = *foo;
  foo_ref.value();
}

TEST(RefCountedPtr, EqualityOperators) {
  RefCountedPtr<Foo> foo(New<Foo>());
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

TEST(MakeRefCounted, NoArgs) {
  RefCountedPtr<Foo> foo = MakeRefCounted<Foo>();
  EXPECT_EQ(0, foo->value());
}

TEST(MakeRefCounted, Args) {
  RefCountedPtr<Foo> foo = MakeRefCounted<Foo>(3);
  EXPECT_EQ(3, foo->value());
}

TraceFlag foo_tracer(true, "foo");

class FooWithTracing : public RefCountedWithTracing<FooWithTracing> {
 public:
  FooWithTracing() : RefCountedWithTracing(&foo_tracer) {}
};

TEST(RefCountedPtr, RefCountedWithTracing) {
  RefCountedPtr<FooWithTracing> foo(New<FooWithTracing>());
  RefCountedPtr<FooWithTracing> foo2 = foo->Ref(DEBUG_LOCATION, "foo");
  foo2.release();
  foo->Unref(DEBUG_LOCATION, "foo");
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
