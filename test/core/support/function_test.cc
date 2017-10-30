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

#include "src/core/lib/support/function.h"
#include <gtest/gtest.h>
#include <stdio.h>
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

struct FunctionFixture {
  template <class T,
            size_t kInplaceStorage = function_impl::kDefaultInplaceStorage>
  using Func = Function<T, kInplaceStorage>;

  static constexpr const bool kAllowsLargeFunctions = true;
  static constexpr const bool kAllowsNonTrivialFunctions = true;
};

struct InplaceFunctionFixture {
  template <class T,
            size_t kInplaceStorage = function_impl::kDefaultInplaceStorage>
  using Func = InplaceFunction<T, kInplaceStorage>;

  static constexpr const bool kAllowsLargeFunctions = false;
  static constexpr const bool kAllowsNonTrivialFunctions = true;
};

struct TrivialInplaceFunctionFixture {
  template <class T,
            size_t kInplaceStorage = function_impl::kDefaultInplaceStorage>
  using Func = TrivialInplaceFunction<T, kInplaceStorage>;

  static constexpr const bool kAllowsLargeFunctions = false;
  static constexpr const bool kAllowsNonTrivialFunctions = false;
};

template <class FuncFixture>
class FuncTest : public ::testing::Test {};

typedef ::testing::Types<FunctionFixture, InplaceFunctionFixture,
                         TrivialInplaceFunctionFixture>
    FixtureTypes;
TYPED_TEST_CASE(FuncTest, FixtureTypes);

template <template <class, size_t> class Func, size_t kInplaceStorage>
int CopyThenCall(Func<int(int), kInplaceStorage> copy, int n) {
  return copy(n);
}

static int CAnswer() { return 42; }

static int CIdent(int x) { return x; }

TYPED_TEST(FuncTest, CFunction) {
  typename TypeParam::template Func<int()> answer = &CAnswer;
  EXPECT_EQ(42, answer());
  typename TypeParam::template Func<int(int)> id = &CIdent;
  EXPECT_EQ(123, id(123));
  EXPECT_EQ(123, CopyThenCall(id, 123));
}

TYPED_TEST(FuncTest, FreeLambda) {
  typename TypeParam::template Func<int()> answer = []() { return 42; };
  EXPECT_EQ(42, answer());
  typename TypeParam::template Func<int(int)> id = [](int i) { return i; };
  EXPECT_EQ(123, id(123));
  EXPECT_EQ(123, CopyThenCall(id, 123));
}

TYPED_TEST(FuncTest, MemberFunctionLambda) {
  class Foo {
   public:
    int Answer() { return 42; }
    int Ident(int x) { return x; }
  };
  Foo* foo = new Foo;
  typename TypeParam::template Func<int()> answer = [foo]() {
    return foo->Answer();
  };
  EXPECT_EQ(42, answer());
  typename TypeParam::template Func<int(int)> id = [foo](int i) {
    return foo->Ident(i);
  };
  EXPECT_EQ(123, id(123));
  EXPECT_EQ(123, CopyThenCall(id, 123));
  delete foo;
}

TYPED_TEST(FuncTest, MemberValueLambda) {
  class Foo {
   public:
    int Answer() const { return 42; }
    int Ident(int x) const { return x; }
  };
  Foo foo;
  typename TypeParam::template Func<int()> answer = [foo]() {
    return foo.Answer();
  };
  EXPECT_EQ(42, answer());
  typename TypeParam::template Func<int(int)> id = [foo](int i) {
    return foo.Ident(i);
  };
  EXPECT_EQ(123, id(123));
  EXPECT_EQ(123, CopyThenCall(id, 123));
}

template <class TypeParam, bool kHandlesNonTrivial>
class NonTrivialTests;

template <class TypeParam>
class NonTrivialTests<TypeParam, false> {
 public:
  static void ComplexLambda() { printf("Skipped: %s\n", __PRETTY_FUNCTION__); }
};

template <class TypeParam>
class NonTrivialTests<TypeParam, true> {
 public:
  static void ComplexLambda() {
    class Foo {
     public:
      Foo() : x_(123) {}
      Foo(const Foo& other) : x_(other.x_ + 1) {}
      ~Foo() {}
      int Answer() const { return 42; }
      int Ident(int x) const { return x; }

     private:
      int x_;
    };
    Foo foo;
    typename TypeParam::template Func<int()> answer = [foo]() {
      return foo.Answer();
    };
    EXPECT_EQ(42, answer());
    typename TypeParam::template Func<int(int)> id = [foo](int i) {
      return foo.Ident(i);
    };
    EXPECT_EQ(123, id(123));
    EXPECT_EQ(123, CopyThenCall(id, 123));
  }
};

TYPED_TEST(FuncTest, ComplexLambda) {
  NonTrivialTests<TypeParam,
                  TypeParam::kAllowsNonTrivialFunctions>::ComplexLambda();
}

template <class TypeParam, bool kHandlesNonTrivial>
class LargeFunctionTests;

template <class TypeParam>
class LargeFunctionTests<TypeParam, false> {
 public:
  static void LargeLambda() { printf("Skipped: %s\n", __PRETTY_FUNCTION__); }
};

template <class TypeParam>
class LargeFunctionTests<TypeParam, true> {
 public:
  static void LargeLambda() {
    class Foo {
     public:
      int Answer() const { return 42; }
      int Ident(int x) const { return x; }

      char very_big[1024];
    };
    Foo foo;
    typename TypeParam::template Func<int()> answer = [foo]() {
      return foo.Answer();
    };
    EXPECT_EQ(42, answer());
    typename TypeParam::template Func<int(int)> id = [foo](int i) {
      return foo.Ident(i);
    };
    EXPECT_EQ(123, id(123));
    EXPECT_EQ(123, CopyThenCall(id, 123));
  }
};

TYPED_TEST(FuncTest, LargeLambda) {
  LargeFunctionTests<TypeParam,
                     TypeParam::kAllowsLargeFunctions>::LargeLambda();
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
