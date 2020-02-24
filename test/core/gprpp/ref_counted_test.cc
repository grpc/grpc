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

#include "src/core/lib/gprpp/ref_counted.h"

#include <gtest/gtest.h>

#include "src/core/lib/gprpp/memory.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class Foo : public RefCounted<Foo> {
 public:
  Foo() {
    static_assert(std::has_virtual_destructor<Foo>::value,
                  "PolymorphicRefCount doesn't have a virtual dtor");
  }
};

TEST(RefCounted, Basic) {
  Foo* foo = new Foo();
  foo->Unref();
}

TEST(RefCounted, ExtraRef) {
  Foo* foo = new Foo();
  RefCountedPtr<Foo> foop = foo->Ref();
  foop.release();
  foo->Unref();
  foo->Unref();
}

class FooNonPolymorphic
    : public RefCounted<FooNonPolymorphic, NonPolymorphicRefCount> {
 public:
  FooNonPolymorphic() {
    static_assert(!std::has_virtual_destructor<FooNonPolymorphic>::value,
                  "NonPolymorphicRefCount has a virtual dtor");
  }
};

TEST(RefCountedNonPolymorphic, Basic) {
  FooNonPolymorphic* foo = new FooNonPolymorphic();
  foo->Unref();
}

TEST(RefCountedNonPolymorphic, ExtraRef) {
  FooNonPolymorphic* foo = new FooNonPolymorphic();
  RefCountedPtr<FooNonPolymorphic> foop = foo->Ref();
  foop.release();
  foo->Unref();
  foo->Unref();
}

// Note: We use DebugOnlyTraceFlag instead of TraceFlag to ensure that
// things build properly in both debug and non-debug cases.
DebugOnlyTraceFlag foo_tracer(true, "foo");

class FooWithTracing : public RefCounted<FooWithTracing> {
 public:
  FooWithTracing() : RefCounted(&foo_tracer) {}
};

TEST(RefCountedWithTracing, Basic) {
  FooWithTracing* foo = new FooWithTracing();
  RefCountedPtr<FooWithTracing> foop = foo->Ref(DEBUG_LOCATION, "extra_ref");
  foop.release();
  foo->Unref(DEBUG_LOCATION, "extra_ref");
  // Can use the no-argument methods, too.
  foop = foo->Ref();
  foop.release();
  foo->Unref();
  foo->Unref(DEBUG_LOCATION, "original_ref");
}

class FooNonPolymorphicWithTracing
    : public RefCounted<FooNonPolymorphicWithTracing, NonPolymorphicRefCount> {
 public:
  FooNonPolymorphicWithTracing() : RefCounted(&foo_tracer) {}
};

TEST(RefCountedNonPolymorphicWithTracing, Basic) {
  FooNonPolymorphicWithTracing* foo = new FooNonPolymorphicWithTracing();
  RefCountedPtr<FooNonPolymorphicWithTracing> foop =
      foo->Ref(DEBUG_LOCATION, "extra_ref");
  foop.release();
  foo->Unref(DEBUG_LOCATION, "extra_ref");
  // Can use the no-argument methods, too.
  foop = foo->Ref();
  foop.release();
  foo->Unref();
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
