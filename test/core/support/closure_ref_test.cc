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

#include "src/core/lib/support/closure_ref.h"
#include <gtest/gtest.h>
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

static int counter;

class Incrementer {
 public:
  Incrementer(int* p) : p_(p) {}
  void Inc() { ++*p_; }

 private:
  int* const p_;
};

void IncCounter() { ++counter; }
void IncCounterBy(int n) { counter += n; }

TEST(ClosureRef, SimpleTests) {
  // simple closures around functions, member functions
  counter = 0;
  ClosureRef<> cb1 =
      AcquiresNoLocks::MakeClosureWithArgs<>::FromFreeFunction<IncCounter>();
  ClosureRef<int> cb2 = AcquiresNoLocks::MakeClosureWithArgs<
      int>::FromFreeFunction<IncCounterBy>();
  Incrementer incrementer(&counter);
  ClosureRef<> cb3 =
      AcquiresNoLocks::MakeClosureWithArgs<>::FromNonRefCountedMemberFunction<
          Incrementer, &Incrementer::Inc>(&incrementer);

  cb1.UnsafeRun();
  cb2.UnsafeRun(2);
  cb3.UnsafeRun();

  EXPECT_DEATH_IF_SUPPORTED(cb1.UnsafeRun(), "");
  EXPECT_DEATH_IF_SUPPORTED(cb2.UnsafeRun(7), "");
  EXPECT_DEATH_IF_SUPPORTED(cb3.UnsafeRun(), "");

  EXPECT_EQ(4, counter);

  // empty closure test
  ClosureRef<> empty;
  EXPECT_DEATH_IF_SUPPORTED(empty.UnsafeRun(), "");
}

TEST(ClosureRef, Lambda) {
  counter = 0;
  ClosureRef<> cb =
      AcquiresNoLocks::MakeClosureWithArgs<>::FromFunctor([]() { counter++; });
  EXPECT_EQ(0, counter);
  cb.UnsafeRun();
  EXPECT_EQ(1, counter);
}

TEST(ClosureRef, Move) {
  counter = 0;
  ClosureRef<> cb1 =
      AcquiresNoLocks::MakeClosureWithArgs<>::FromFreeFunction<IncCounter>();
  ClosureRef<> cb2 = std::move(cb1);
  cb2.UnsafeRun();
  EXPECT_DEATH_IF_SUPPORTED(cb1.UnsafeRun(), "");
  EXPECT_DEATH_IF_SUPPORTED(cb2.UnsafeRun(), "");
}

TEST(ClosureRef, Movier) {
  counter = 0;
  ClosureRef<> cb1 =
      AcquiresNoLocks::MakeClosureWithArgs<>::FromFreeFunction<IncCounter>();
  ClosureRef<> cb2 = std::move(cb1);
  cb1 = std::move(cb2);
  cb1.UnsafeRun();
  EXPECT_DEATH_IF_SUPPORTED(cb1.UnsafeRun(), "");
  EXPECT_DEATH_IF_SUPPORTED(cb2.UnsafeRun(), "");
}

TEST(ClosureRef, RefCountedMember) {
  class Foo {
   public:
    int refs = 0;
    int unrefs = 0;
    int executes = 0;
    void Ref() { ++refs; }
    void Unref() { ++unrefs; }
    void Execute() { ++executes; }
  };
  Foo foo;
  ClosureRef<> cb =
      AcquiresNoLocks::MakeClosureWithArgs<>::FromRefCountedMemberFunction<
          Foo, &Foo::Execute>(&foo);
  cb.Schedule();
  EXPECT_EQ(1, foo.refs);
  EXPECT_EQ(1, foo.unrefs);
  EXPECT_EQ(1, foo.executes);
}

TEST(ClosureRef, MustBeScheduled) {
  auto test_body = []() {
    ClosureRef<> cb1 =
        AcquiresNoLocks::MakeClosureWithArgs<>::FromFreeFunction<IncCounter>();
    // should crash here due to reaching destructor without executing cb1
  };
  EXPECT_DEATH_IF_SUPPORTED(test_body(), "");
}

TEST(ClosureRef, BarrierMember) {
  class Foo {
   public:
    int refs = 0;
    int unrefs = 0;
    int executes = 0;
    int barrier = 2;
    void Ref() { ++refs; }
    void Unref() { ++unrefs; }
    void Execute() { ++executes; }
    ClosureRef<> MakeClosure() {
      return AcquiresNoLocks::MakeClosureWithArgs<>::
          FromRefCountedMemberFunctionWithBarrier<Foo, &Foo::Execute, int,
                                                  &Foo::barrier>(this);
    }
  };
  Foo foo;

  foo.MakeClosure().UnsafeRun();
  EXPECT_EQ(1, foo.refs);
  EXPECT_EQ(1, foo.unrefs);
  EXPECT_EQ(0, foo.executes);
  foo.MakeClosure().UnsafeRun();
  EXPECT_EQ(2, foo.refs);
  EXPECT_EQ(2, foo.unrefs);
  EXPECT_EQ(1, foo.executes);
}

TEST(ClosureRef, CopyShouldNotCompile) {
  static_assert(!std::is_copy_constructible<ClosureRef<>>::value,
                "ClosureRef should not be copyable");
  static_assert(!std::is_copy_constructible<ClosureRef<int>>::value,
                "ClosureRef should not be copyable");
  static_assert(!std::is_copy_assignable<ClosureRef<>>::value,
                "ClosureRef should not be copyable");
  static_assert(!std::is_copy_assignable<ClosureRef<int>>::value,
                "ClosureRef should not be copyable");
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
