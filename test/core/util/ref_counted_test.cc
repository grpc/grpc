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

#include "src/core/util/ref_counted.h"

#include <memory>
#include <new>
#include <set>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/core/test_util/test_config.h"

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

TEST(RefCounted, Const) {
  const Foo* foo = new Foo();
  RefCountedPtr<const Foo> foop = foo->Ref();
  foop.release();
  foop = foo->RefIfNonZero();
  foop.release();
  foo->Unref();
  foo->Unref();
  foo->Unref();
}

TEST(RefCounted, SubclassOfRefCountedType) {
  class Bar : public Foo {};
  Bar* bar = new Bar();
  RefCountedPtr<Bar> barp = bar->RefAsSubclass<Bar>();
  barp.release();
  barp = bar->RefAsSubclass<Bar>(DEBUG_LOCATION, "whee");
  barp.release();
  bar->Unref();
  bar->Unref();
  bar->Unref();
}

class Value : public RefCounted<Value, PolymorphicRefCount, UnrefNoDelete> {
 public:
  Value(int value, std::set<std::unique_ptr<Value>>* registry) : value_(value) {
    registry->emplace(this);
  }

  int value() const { return value_; }

 private:
  int value_;
};

void GarbageCollectRegistry(std::set<std::unique_ptr<Value>>* registry) {
  for (auto it = registry->begin(); it != registry->end();) {
    RefCountedPtr<Value> v = (*it)->RefIfNonZero();
    // Check if the object has any refs remaining.
    if (v != nullptr) {
      // It has refs remaining, so we do not delete it.
      ++it;
    } else {
      // No refs remaining, so remove it from the registry.
      it = registry->erase(it);
    }
  }
}

TEST(RefCounted, NoDeleteUponUnref) {
  std::set<std::unique_ptr<Value>> registry;
  // Add two objects to the registry.
  auto v1 = MakeRefCounted<Value>(1, &registry);
  auto v2 = MakeRefCounted<Value>(2, &registry);
  EXPECT_THAT(registry,
              ::testing::UnorderedElementsAre(
                  ::testing::Pointee(::testing::Property(&Value::value, 1)),
                  ::testing::Pointee(::testing::Property(&Value::value, 2))));
  // Running garbage collection should not delete anything, since both
  // entries still have refs.
  GarbageCollectRegistry(&registry);
  EXPECT_THAT(registry,
              ::testing::UnorderedElementsAre(
                  ::testing::Pointee(::testing::Property(&Value::value, 1)),
                  ::testing::Pointee(::testing::Property(&Value::value, 2))));
  // Unref v2 and run GC to remove it.
  v2.reset();
  GarbageCollectRegistry(&registry);
  EXPECT_THAT(registry, ::testing::UnorderedElementsAre(::testing::Pointee(
                            ::testing::Property(&Value::value, 1))));
  // Now unref v1 and run GC again.
  v1.reset();
  GarbageCollectRegistry(&registry);
  EXPECT_THAT(registry, ::testing::UnorderedElementsAre());
}

class ValueInExternalAllocation
    : public RefCounted<ValueInExternalAllocation, PolymorphicRefCount,
                        UnrefCallDtor> {
 public:
  explicit ValueInExternalAllocation(int value) : value_(value) {}

  int value() const { return value_; }

 private:
  int value_;
};

TEST(RefCounted, CallDtorUponUnref) {
  alignas(ValueInExternalAllocation) char
      storage[sizeof(ValueInExternalAllocation)];
  RefCountedPtr<ValueInExternalAllocation> value(
      new (&storage) ValueInExternalAllocation(5));
  EXPECT_EQ(value->value(), 5);
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

class FooWithTracing : public RefCounted<FooWithTracing> {
 public:
  FooWithTracing() : RefCounted("Foo") {}
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
  FooNonPolymorphicWithTracing() : RefCounted("FooNonPolymorphicWithTracing") {}
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
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
