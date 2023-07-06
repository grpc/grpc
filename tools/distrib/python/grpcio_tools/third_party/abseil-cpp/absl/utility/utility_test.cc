// Copyright 2022 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/utility/utility.h"

#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"

namespace {

#ifdef _MSC_VER
// Warnings for unused variables in this test are false positives.  On other
// platforms, they are suppressed by ABSL_ATTRIBUTE_UNUSED, but that doesn't
// work on MSVC.
// Both the unused variables and the name length warnings are due to calls
// to absl::make_index_sequence with very large values, creating very long type
// names. The resulting warnings are so long they make build output unreadable.
#pragma warning(push)
#pragma warning(disable : 4503)  // decorated name length exceeded
#pragma warning(disable : 4101)  // unreferenced local variable
#endif                           // _MSC_VER

using ::testing::ElementsAre;
using ::testing::Pointee;
using ::testing::StaticAssertTypeEq;

TEST(IntegerSequenceTest, ValueType) {
  StaticAssertTypeEq<int, absl::integer_sequence<int>::value_type>();
  StaticAssertTypeEq<char, absl::integer_sequence<char>::value_type>();
}

TEST(IntegerSequenceTest, Size) {
  EXPECT_EQ(0, (absl::integer_sequence<int>::size()));
  EXPECT_EQ(1, (absl::integer_sequence<int, 0>::size()));
  EXPECT_EQ(1, (absl::integer_sequence<int, 1>::size()));
  EXPECT_EQ(2, (absl::integer_sequence<int, 1, 2>::size()));
  EXPECT_EQ(3, (absl::integer_sequence<int, 0, 1, 2>::size()));
  EXPECT_EQ(3, (absl::integer_sequence<int, -123, 123, 456>::size()));
  constexpr size_t sz = absl::integer_sequence<int, 0, 1>::size();
  EXPECT_EQ(2, sz);
}

TEST(IntegerSequenceTest, MakeIndexSequence) {
  StaticAssertTypeEq<absl::index_sequence<>, absl::make_index_sequence<0>>();
  StaticAssertTypeEq<absl::index_sequence<0>, absl::make_index_sequence<1>>();
  StaticAssertTypeEq<absl::index_sequence<0, 1>,
                     absl::make_index_sequence<2>>();
  StaticAssertTypeEq<absl::index_sequence<0, 1, 2>,
                     absl::make_index_sequence<3>>();
}

TEST(IntegerSequenceTest, MakeIntegerSequence) {
  StaticAssertTypeEq<absl::integer_sequence<int>,
                     absl::make_integer_sequence<int, 0>>();
  StaticAssertTypeEq<absl::integer_sequence<int, 0>,
                     absl::make_integer_sequence<int, 1>>();
  StaticAssertTypeEq<absl::integer_sequence<int, 0, 1>,
                     absl::make_integer_sequence<int, 2>>();
  StaticAssertTypeEq<absl::integer_sequence<int, 0, 1, 2>,
                     absl::make_integer_sequence<int, 3>>();
}

template <typename... Ts>
class Counter {};

template <size_t... Is>
void CountAll(absl::index_sequence<Is...>) {
  // We only need an alias here, but instantiate a variable to silence warnings
  // for unused typedefs in some compilers.
  ABSL_ATTRIBUTE_UNUSED Counter<absl::make_index_sequence<Is>...> seq;
}

// This test verifies that absl::make_index_sequence can handle large arguments
// without blowing up template instantiation stack, going OOM or taking forever
// to compile (there is hard 15 minutes limit imposed by forge).
TEST(IntegerSequenceTest, MakeIndexSequencePerformance) {
  // O(log N) template instantiations.
  // We only need an alias here, but instantiate a variable to silence warnings
  // for unused typedefs in some compilers.
  ABSL_ATTRIBUTE_UNUSED absl::make_index_sequence<(1 << 16) - 1> seq;
  // O(N) template instantiations.
  CountAll(absl::make_index_sequence<(1 << 8) - 1>());
}

template <typename F, typename Tup, size_t... Is>
auto ApplyFromTupleImpl(F f, const Tup& tup, absl::index_sequence<Is...>)
    -> decltype(f(std::get<Is>(tup)...)) {
  return f(std::get<Is>(tup)...);
}

template <typename Tup>
using TupIdxSeq = absl::make_index_sequence<std::tuple_size<Tup>::value>;

template <typename F, typename Tup>
auto ApplyFromTuple(F f, const Tup& tup)
    -> decltype(ApplyFromTupleImpl(f, tup, TupIdxSeq<Tup>{})) {
  return ApplyFromTupleImpl(f, tup, TupIdxSeq<Tup>{});
}

template <typename T>
std::string Fmt(const T& x) {
  std::ostringstream os;
  os << x;
  return os.str();
}

struct PoorStrCat {
  template <typename... Args>
  std::string operator()(const Args&... args) const {
    std::string r;
    for (const auto& e : {Fmt(args)...}) r += e;
    return r;
  }
};

template <typename Tup, size_t... Is>
std::vector<std::string> TupStringVecImpl(const Tup& tup,
                                          absl::index_sequence<Is...>) {
  return {Fmt(std::get<Is>(tup))...};
}

template <typename... Ts>
std::vector<std::string> TupStringVec(const std::tuple<Ts...>& tup) {
  return TupStringVecImpl(tup, absl::index_sequence_for<Ts...>());
}

TEST(MakeIndexSequenceTest, ApplyFromTupleExample) {
  PoorStrCat f{};
  EXPECT_EQ("12abc3.14", f(12, "abc", 3.14));
  EXPECT_EQ("12abc3.14", ApplyFromTuple(f, std::make_tuple(12, "abc", 3.14)));
}

TEST(IndexSequenceForTest, Basic) {
  StaticAssertTypeEq<absl::index_sequence<>, absl::index_sequence_for<>>();
  StaticAssertTypeEq<absl::index_sequence<0>, absl::index_sequence_for<int>>();
  StaticAssertTypeEq<absl::index_sequence<0, 1, 2, 3>,
                     absl::index_sequence_for<int, void, char, int>>();
}

TEST(IndexSequenceForTest, Example) {
  EXPECT_THAT(TupStringVec(std::make_tuple(12, "abc", 3.14)),
              ElementsAre("12", "abc", "3.14"));
}

int Function(int a, int b) { return a - b; }

int Sink(std::unique_ptr<int> p) { return *p; }

std::unique_ptr<int> Factory(int n) { return absl::make_unique<int>(n); }

void NoOp() {}

struct ConstFunctor {
  int operator()(int a, int b) const { return a - b; }
};

struct MutableFunctor {
  int operator()(int a, int b) { return a - b; }
};

struct EphemeralFunctor {
  EphemeralFunctor() {}
  EphemeralFunctor(const EphemeralFunctor&) {}
  EphemeralFunctor(EphemeralFunctor&&) {}
  int operator()(int a, int b) && { return a - b; }
};

struct OverloadedFunctor {
  OverloadedFunctor() {}
  OverloadedFunctor(const OverloadedFunctor&) {}
  OverloadedFunctor(OverloadedFunctor&&) {}
  template <typename... Args>
  std::string operator()(const Args&... args) & {
    return absl::StrCat("&", args...);
  }
  template <typename... Args>
  std::string operator()(const Args&... args) const& {
    return absl::StrCat("const&", args...);
  }
  template <typename... Args>
  std::string operator()(const Args&... args) && {
    return absl::StrCat("&&", args...);
  }
};

struct Class {
  int Method(int a, int b) { return a - b; }
  int ConstMethod(int a, int b) const { return a - b; }

  int member;
};

struct FlipFlop {
  int ConstMethod() const { return member; }
  FlipFlop operator*() const { return {-member}; }

  int member;
};

TEST(ApplyTest, Function) {
  EXPECT_EQ(1, absl::apply(Function, std::make_tuple(3, 2)));
  EXPECT_EQ(1, absl::apply(&Function, std::make_tuple(3, 2)));
}

TEST(ApplyTest, NonCopyableArgument) {
  EXPECT_EQ(42, absl::apply(Sink, std::make_tuple(absl::make_unique<int>(42))));
}

TEST(ApplyTest, NonCopyableResult) {
  EXPECT_THAT(absl::apply(Factory, std::make_tuple(42)), Pointee(42));
}

TEST(ApplyTest, VoidResult) { absl::apply(NoOp, std::tuple<>()); }

TEST(ApplyTest, ConstFunctor) {
  EXPECT_EQ(1, absl::apply(ConstFunctor(), std::make_tuple(3, 2)));
}

TEST(ApplyTest, MutableFunctor) {
  MutableFunctor f;
  EXPECT_EQ(1, absl::apply(f, std::make_tuple(3, 2)));
  EXPECT_EQ(1, absl::apply(MutableFunctor(), std::make_tuple(3, 2)));
}
TEST(ApplyTest, EphemeralFunctor) {
  EphemeralFunctor f;
  EXPECT_EQ(1, absl::apply(std::move(f), std::make_tuple(3, 2)));
  EXPECT_EQ(1, absl::apply(EphemeralFunctor(), std::make_tuple(3, 2)));
}
TEST(ApplyTest, OverloadedFunctor) {
  OverloadedFunctor f;
  const OverloadedFunctor& cf = f;

  EXPECT_EQ("&", absl::apply(f, std::tuple<>{}));
  EXPECT_EQ("& 42", absl::apply(f, std::make_tuple(" 42")));

  EXPECT_EQ("const&", absl::apply(cf, std::tuple<>{}));
  EXPECT_EQ("const& 42", absl::apply(cf, std::make_tuple(" 42")));

  EXPECT_EQ("&&", absl::apply(std::move(f), std::tuple<>{}));
  OverloadedFunctor f2;
  EXPECT_EQ("&& 42", absl::apply(std::move(f2), std::make_tuple(" 42")));
}

TEST(ApplyTest, ReferenceWrapper) {
  ConstFunctor cf;
  MutableFunctor mf;
  EXPECT_EQ(1, absl::apply(std::cref(cf), std::make_tuple(3, 2)));
  EXPECT_EQ(1, absl::apply(std::ref(cf), std::make_tuple(3, 2)));
  EXPECT_EQ(1, absl::apply(std::ref(mf), std::make_tuple(3, 2)));
}

TEST(ApplyTest, MemberFunction) {
  std::unique_ptr<Class> p(new Class);
  std::unique_ptr<const Class> cp(new Class);
  EXPECT_EQ(
      1, absl::apply(&Class::Method,
                     std::tuple<std::unique_ptr<Class>&, int, int>(p, 3, 2)));
  EXPECT_EQ(1, absl::apply(&Class::Method,
                           std::tuple<Class*, int, int>(p.get(), 3, 2)));
  EXPECT_EQ(
      1, absl::apply(&Class::Method, std::tuple<Class&, int, int>(*p, 3, 2)));

  EXPECT_EQ(
      1, absl::apply(&Class::ConstMethod,
                     std::tuple<std::unique_ptr<Class>&, int, int>(p, 3, 2)));
  EXPECT_EQ(1, absl::apply(&Class::ConstMethod,
                           std::tuple<Class*, int, int>(p.get(), 3, 2)));
  EXPECT_EQ(1, absl::apply(&Class::ConstMethod,
                           std::tuple<Class&, int, int>(*p, 3, 2)));

  EXPECT_EQ(1, absl::apply(&Class::ConstMethod,
                           std::tuple<std::unique_ptr<const Class>&, int, int>(
                               cp, 3, 2)));
  EXPECT_EQ(1, absl::apply(&Class::ConstMethod,
                           std::tuple<const Class*, int, int>(cp.get(), 3, 2)));
  EXPECT_EQ(1, absl::apply(&Class::ConstMethod,
                           std::tuple<const Class&, int, int>(*cp, 3, 2)));

  EXPECT_EQ(1, absl::apply(&Class::Method,
                           std::make_tuple(absl::make_unique<Class>(), 3, 2)));
  EXPECT_EQ(1, absl::apply(&Class::ConstMethod,
                           std::make_tuple(absl::make_unique<Class>(), 3, 2)));
  EXPECT_EQ(
      1, absl::apply(&Class::ConstMethod,
                     std::make_tuple(absl::make_unique<const Class>(), 3, 2)));
}

TEST(ApplyTest, DataMember) {
  std::unique_ptr<Class> p(new Class{42});
  std::unique_ptr<const Class> cp(new Class{42});
  EXPECT_EQ(
      42, absl::apply(&Class::member, std::tuple<std::unique_ptr<Class>&>(p)));
  EXPECT_EQ(42, absl::apply(&Class::member, std::tuple<Class&>(*p)));
  EXPECT_EQ(42, absl::apply(&Class::member, std::tuple<Class*>(p.get())));

  absl::apply(&Class::member, std::tuple<std::unique_ptr<Class>&>(p)) = 42;
  absl::apply(&Class::member, std::tuple<Class*>(p.get())) = 42;
  absl::apply(&Class::member, std::tuple<Class&>(*p)) = 42;

  EXPECT_EQ(42, absl::apply(&Class::member,
                            std::tuple<std::unique_ptr<const Class>&>(cp)));
  EXPECT_EQ(42, absl::apply(&Class::member, std::tuple<const Class&>(*cp)));
  EXPECT_EQ(42,
            absl::apply(&Class::member, std::tuple<const Class*>(cp.get())));
}

TEST(ApplyTest, FlipFlop) {
  FlipFlop obj = {42};
  // This call could resolve to (obj.*&FlipFlop::ConstMethod)() or
  // ((*obj).*&FlipFlop::ConstMethod)(). We verify that it's the former.
  EXPECT_EQ(42, absl::apply(&FlipFlop::ConstMethod, std::make_tuple(obj)));
  EXPECT_EQ(42, absl::apply(&FlipFlop::member, std::make_tuple(obj)));
}

TEST(ExchangeTest, MoveOnly) {
  auto a = Factory(1);
  EXPECT_EQ(1, *a);
  auto b = absl::exchange(a, Factory(2));
  EXPECT_EQ(2, *a);
  EXPECT_EQ(1, *b);
}

TEST(MakeFromTupleTest, String) {
  EXPECT_EQ(
      absl::make_from_tuple<std::string>(std::make_tuple("hello world", 5)),
      "hello");
}

TEST(MakeFromTupleTest, MoveOnlyParameter) {
  struct S {
    S(std::unique_ptr<int> n, std::unique_ptr<int> m) : value(*n + *m) {}
    int value = 0;
  };
  auto tup =
      std::make_tuple(absl::make_unique<int>(3), absl::make_unique<int>(4));
  auto s = absl::make_from_tuple<S>(std::move(tup));
  EXPECT_EQ(s.value, 7);
}

TEST(MakeFromTupleTest, NoParameters) {
  struct S {
    S() : value(1) {}
    int value = 2;
  };
  EXPECT_EQ(absl::make_from_tuple<S>(std::make_tuple()).value, 1);
}

TEST(MakeFromTupleTest, Pair) {
  EXPECT_EQ(
      (absl::make_from_tuple<std::pair<bool, int>>(std::make_tuple(true, 17))),
      std::make_pair(true, 17));
}

}  // namespace
