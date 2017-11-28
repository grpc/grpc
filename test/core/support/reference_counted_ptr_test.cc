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

#include "src/core/lib/support/reference_counted_ptr.h"

#include <gtest/gtest.h>

#include <grpc/support/log.h>

#include "src/core/lib/support/memory.h"
#include "src/core/lib/support/reference_counted.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

class Foo : public ReferenceCounted {
 public:
  Foo() : ReferenceCounted(nullptr) {}

  void log() { 
    gpr_log(GPR_INFO, "==> log()");
  }
};

TEST(ReferenceCountedPtr, DefaultConstructor) {
  ReferenceCountedPtr<Foo> foo;
}

TEST(ReferenceCountedPtr, ExplicitConstructorEmpty) {
  ReferenceCountedPtr<Foo> foo(nullptr);
}

TEST(ReferenceCountedPtr, ExplicitConstructor) {
  ReferenceCountedPtr<Foo> foo(New<Foo>());
}

TEST(ReferenceCountedPtr, MoveConstructor) {
  ReferenceCountedPtr<Foo> foo(New<Foo>());
  ReferenceCountedPtr<Foo> foo2(std::move(foo));
  EXPECT_EQ(nullptr, foo.get());
  EXPECT_NE(nullptr, foo2.get());
}

TEST(ReferenceCountedPtr, MoveAssignment) {
  ReferenceCountedPtr<Foo> foo(New<Foo>());
  ReferenceCountedPtr<Foo> foo2 = std::move(foo);
  EXPECT_EQ(nullptr, foo.get());
  EXPECT_NE(nullptr, foo2.get());
}

TEST(ReferenceCountedPtr, CopyConstructor) {
  ReferenceCountedPtr<Foo> foo(New<Foo>());
  ReferenceCountedPtr<Foo> foo2(foo);
  EXPECT_NE(nullptr, foo.get());
  EXPECT_EQ(foo.get(), foo2.get());
}

TEST(ReferenceCountedPtr, CopyAssignment) {
  ReferenceCountedPtr<Foo> foo(New<Foo>());
  ReferenceCountedPtr<Foo> foo2 = foo;
  EXPECT_NE(nullptr, foo.get());
  EXPECT_EQ(foo.get(), foo2.get());
}

TEST(ReferenceCountedPtr, EnclosedScope) {
  ReferenceCountedPtr<Foo> foo(New<Foo>());
  {
    ReferenceCountedPtr<Foo> foo2(std::move(foo));
    EXPECT_EQ(nullptr, foo.get());
    EXPECT_NE(nullptr, foo2.get());
  }
  EXPECT_EQ(nullptr, foo.get());
}

TEST(ReferenceCountedPtr, ResetFromNullToNonNull) {
  ReferenceCountedPtr<Foo> foo;
  EXPECT_EQ(nullptr, foo.get());
  foo.reset(New<Foo>());
  EXPECT_NE(nullptr, foo.get());
}

TEST(ReferenceCountedPtr, ResetFromNonNullToNonNull) {
  ReferenceCountedPtr<Foo> foo(New<Foo>());
  EXPECT_NE(nullptr, foo.get());
  Foo* original = foo.get();
  foo.reset(New<Foo>());
  EXPECT_NE(nullptr, foo.get());
  EXPECT_NE(original, foo.get());
}

TEST(ReferenceCountedPtr, ResetFromNonNullToNull) {
  ReferenceCountedPtr<Foo> foo(New<Foo>());
  EXPECT_NE(nullptr, foo.get());
  foo.reset();
  EXPECT_EQ(nullptr, foo.get());
}

TEST(ReferenceCountedPtr, ResetFromNullToNull) {
  ReferenceCountedPtr<Foo> foo;
  EXPECT_EQ(nullptr, foo.get());
  foo.reset(nullptr);
  EXPECT_EQ(nullptr, foo.get());
}

TEST(ReferenceCountedPtr, DerefernceOperators) {
  ReferenceCountedPtr<Foo> foo(New<Foo>());
  foo->log();
  Foo& foo_ref = *foo;
  foo_ref.log();
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
