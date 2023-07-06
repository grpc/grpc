// Copyright 2019 The Abseil Authors.
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
//
// -----------------------------------------------------------------------------
// conformance_testing.h
// -----------------------------------------------------------------------------
//

#ifndef ABSL_TYPES_INTERNAL_CONFORMANCE_TESTING_H_
#define ABSL_TYPES_INTERNAL_CONFORMANCE_TESTING_H_

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Many templates in this file take a `T` and a `Prof` type as explicit       //
// template arguments. These are a type to be checked and a                   //
// "Regularity Profile" that describes what operations that type `T` is       //
// expected to support. See "regularity_profiles.h" for more details          //
// regarding Regularity Profiles.                                             //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cstddef>
#include <set>
#include <tuple>
#include <type_traits>
#include <utility>

#include "gtest/gtest.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/internal/conformance_aliases.h"
#include "absl/types/internal/conformance_archetype.h"
#include "absl/types/internal/conformance_profile.h"
#include "absl/types/internal/conformance_testing_helpers.h"
#include "absl/types/internal/parentheses.h"
#include "absl/types/internal/transform_args.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace types_internal {

// Returns true if the compiler incorrectly greedily instantiates constexpr
// templates in any unevaluated context.
constexpr bool constexpr_instantiation_when_unevaluated() {
#if defined(__apple_build_version__)  // TODO(calabrese) Make more specific
  return true;
#elif defined(__clang__)
  return __clang_major__ < 4;
#elif defined(__GNUC__)
  // TODO(calabrese) Figure out why gcc 7 fails (seems like a different bug)
  return __GNUC__ < 5 || (__GNUC__ == 5 && __GNUC_MINOR__ < 2) || __GNUC__ >= 7;
#else
  return false;
#endif
}

// Returns true if the standard library being used incorrectly produces an error
// when instantiating the definition of a poisoned std::hash specialization.
constexpr bool poisoned_hash_fails_instantiation() {
#if defined(_MSC_VER) && !defined(_LIBCPP_VERSION)
  return _MSC_VER < 1914;
#else
  return false;
#endif
}

template <class Fun>
struct GeneratorType {
  decltype(std::declval<const Fun&>()()) operator()() const
      noexcept(noexcept(std::declval<const Fun&>()())) {
    return fun();
  }

  Fun fun;
  const char* description;
};

// A "make" function for the GeneratorType template that deduces the function
// object type.
template <class Fun,
          absl::enable_if_t<IsNullaryCallable<Fun>::value>** = nullptr>
GeneratorType<Fun> Generator(Fun fun, const char* description) {
  return GeneratorType<Fun>{absl::move(fun), description};
}

// A type that contains a set of nullary function objects that each return an
// instance of the same type and value (though possibly different
// representations, such as +0 and -0 or two vectors with the same elements but
// with different capacities).
template <class... Funs>
struct EquivalenceClassType {
  std::tuple<GeneratorType<Funs>...> generators;
};

// A "make" function for the EquivalenceClassType template that deduces the
// function object types and is constrained such that a user can only pass in
// function objects that all have the same return type.
template <class... Funs, absl::enable_if_t<AreGeneratorsWithTheSameReturnType<
                             Funs...>::value>** = nullptr>
EquivalenceClassType<Funs...> EquivalenceClass(GeneratorType<Funs>... funs) {
  return {std::make_tuple(absl::move(funs)...)};
}

// A type that contains an ordered series of EquivalenceClassTypes, from
// smallest value to largest value.
template <class... EqClasses>
struct OrderedEquivalenceClasses {
  std::tuple<EqClasses...> eq_classes;
};

// An object containing the parts of a given (name, initialization expression),
// and is capable of generating a string that describes the given.
struct GivenDeclaration {
  std::string outputDeclaration(std::size_t width) const {
    const std::size_t indent_size = 2;
    std::string result = absl::StrCat("  ", name);

    if (!expression.empty()) {
      // Indent
      result.resize(indent_size + width, ' ');
      absl::StrAppend(&result, " = ", expression, ";\n");
    } else {
      absl::StrAppend(&result, ";\n");
    }

    return result;
  }

  std::string name;
  std::string expression;
};

// Produce a string that contains all of the givens of an error report.
template <class... Decls>
std::string PrepareGivenContext(const Decls&... decls) {
  const std::size_t width = (std::max)({decls.name.size()...});
  return absl::StrCat("Given:\n", decls.outputDeclaration(width)..., "\n");
}

////////////////////////////////////////////////////////////////////////////////
// Function objects that perform a check for each comparison operator         //
////////////////////////////////////////////////////////////////////////////////

#define ABSL_INTERNAL_EXPECT_OP(name, op)                                   \
  struct Expect##name {                                                     \
    template <class T>                                                      \
    void operator()(absl::string_view test_name, absl::string_view context, \
                    const T& lhs, const T& rhs, absl::string_view lhs_name, \
                    absl::string_view rhs_name) const {                     \
      if (!static_cast<bool>(lhs op rhs)) {                                 \
        errors->addTestFailure(                                             \
            test_name, absl::StrCat(context,                                \
                                    "**Unexpected comparison result**\n"    \
                                    "\n"                                    \
                                    "Expression:\n"                         \
                                    "  ",                                   \
                                    lhs_name, " " #op " ", rhs_name,        \
                                    "\n"                                    \
                                    "\n"                                    \
                                    "Expected: true\n"                      \
                                    "  Actual: false"));                    \
      } else {                                                              \
        errors->addTestSuccess(test_name);                                  \
      }                                                                     \
    }                                                                       \
                                                                            \
    ConformanceErrors* errors;                                              \
  };                                                                        \
                                                                            \
  struct ExpectNot##name {                                                  \
    template <class T>                                                      \
    void operator()(absl::string_view test_name, absl::string_view context, \
                    const T& lhs, const T& rhs, absl::string_view lhs_name, \
                    absl::string_view rhs_name) const {                     \
      if (lhs op rhs) {                                                     \
        errors->addTestFailure(                                             \
            test_name, absl::StrCat(context,                                \
                                    "**Unexpected comparison result**\n"    \
                                    "\n"                                    \
                                    "Expression:\n"                         \
                                    "  ",                                   \
                                    lhs_name, " " #op " ", rhs_name,        \
                                    "\n"                                    \
                                    "\n"                                    \
                                    "Expected: false\n"                     \
                                    "  Actual: true"));                     \
      } else {                                                              \
        errors->addTestSuccess(test_name);                                  \
      }                                                                     \
    }                                                                       \
                                                                            \
    ConformanceErrors* errors;                                              \
  }

ABSL_INTERNAL_EXPECT_OP(Eq, ==);
ABSL_INTERNAL_EXPECT_OP(Ne, !=);
ABSL_INTERNAL_EXPECT_OP(Lt, <);
ABSL_INTERNAL_EXPECT_OP(Le, <=);
ABSL_INTERNAL_EXPECT_OP(Ge, >=);
ABSL_INTERNAL_EXPECT_OP(Gt, >);

#undef ABSL_INTERNAL_EXPECT_OP

// A function object that verifies that two objects hash to the same value by
// way of the std::hash specialization.
struct ExpectSameHash {
  template <class T>
  void operator()(absl::string_view test_name, absl::string_view context,
                  const T& lhs, const T& rhs, absl::string_view lhs_name,
                  absl::string_view rhs_name) const {
    if (std::hash<T>()(lhs) != std::hash<T>()(rhs)) {
      errors->addTestFailure(
          test_name, absl::StrCat(context,
                                  "**Unexpected hash result**\n"
                                  "\n"
                                  "Expression:\n"
                                  "  std::hash<T>()(",
                                  lhs_name, ") == std::hash<T>()(", rhs_name,
                                  ")\n"
                                  "\n"
                                  "Expected: true\n"
                                  "  Actual: false"));
    } else {
      errors->addTestSuccess(test_name);
    }
  }

  ConformanceErrors* errors;
};

// A function template that takes two objects and verifies that each comparison
// operator behaves in a way that is consistent with equality. It has "OneWay"
// in the name because the first argument will always be the left-hand operand
// of the corresponding comparison operator and the second argument will
// always be the right-hand operand. It will never switch that order.
// At a higher level in the test suite, the one-way form is called once for each
// of the two possible orders whenever lhs and rhs are not the same initializer.
template <class T, class Prof>
void ExpectOneWayEquality(ConformanceErrors* errors,
                          absl::string_view test_name,
                          absl::string_view context, const T& lhs, const T& rhs,
                          absl::string_view lhs_name,
                          absl::string_view rhs_name) {
  If<PropertiesOfT<Prof>::is_equality_comparable>::Invoke(
      ExpectEq{errors}, test_name, context, lhs, rhs, lhs_name, rhs_name);

  If<PropertiesOfT<Prof>::is_inequality_comparable>::Invoke(
      ExpectNotNe{errors}, test_name, context, lhs, rhs, lhs_name, rhs_name);

  If<PropertiesOfT<Prof>::is_less_than_comparable>::Invoke(
      ExpectNotLt{errors}, test_name, context, lhs, rhs, lhs_name, rhs_name);

  If<PropertiesOfT<Prof>::is_less_equal_comparable>::Invoke(
      ExpectLe{errors}, test_name, context, lhs, rhs, lhs_name, rhs_name);

  If<PropertiesOfT<Prof>::is_greater_equal_comparable>::Invoke(
      ExpectGe{errors}, test_name, context, lhs, rhs, lhs_name, rhs_name);

  If<PropertiesOfT<Prof>::is_greater_than_comparable>::Invoke(
      ExpectNotGt{errors}, test_name, context, lhs, rhs, lhs_name, rhs_name);

  If<PropertiesOfT<Prof>::is_hashable>::Invoke(
      ExpectSameHash{errors}, test_name, context, lhs, rhs, lhs_name, rhs_name);
}

// A function template that takes two objects and verifies that each comparison
// operator behaves in a way that is consistent with equality. This function
// differs from ExpectOneWayEquality in that this will do checks with argument
// order reversed in addition to in-order.
template <class T, class Prof>
void ExpectEquality(ConformanceErrors* errors, absl::string_view test_name,
                    absl::string_view context, const T& lhs, const T& rhs,
                    absl::string_view lhs_name, absl::string_view rhs_name) {
  (ExpectOneWayEquality<T, Prof>)(errors, test_name, context, lhs, rhs,
                                  lhs_name, rhs_name);
  (ExpectOneWayEquality<T, Prof>)(errors, test_name, context, rhs, lhs,
                                  rhs_name, lhs_name);
}

// Given a generator, makes sure that a generated value and a moved-from
// generated value are equal.
template <class T, class Prof>
struct ExpectMoveConstructOneGenerator {
  template <class Fun>
  void operator()(const Fun& generator) const {
    const T object = generator();
    const T moved_object = absl::move(generator());  // Force no elision.

    (ExpectEquality<T, Prof>)(errors, "Move construction",
                              PrepareGivenContext(
                                  GivenDeclaration{"const _T object",
                                                   generator.description},
                                  GivenDeclaration{"const _T moved_object",
                                                   std::string("std::move(") +
                                                       generator.description +
                                                       ")"}),
                              object, moved_object, "object", "moved_object");
  }

  ConformanceErrors* errors;
};

// Given a generator, makes sure that a generated value and a copied-from
// generated value are equal.
template <class T, class Prof>
struct ExpectCopyConstructOneGenerator {
  template <class Fun>
  void operator()(const Fun& generator) const {
    const T object = generator();
    const T copied_object = static_cast<const T&>(generator());

    (ExpectEquality<T, Prof>)(errors, "Copy construction",
                              PrepareGivenContext(
                                  GivenDeclaration{"const _T object",
                                                   generator.description},
                                  GivenDeclaration{
                                      "const _T copied_object",
                                      std::string("static_cast<const _T&>(") +
                                          generator.description + ")"}),
                              object, copied_object, "object", "copied_object");
  }

  ConformanceErrors* errors;
};

// Default-construct and do nothing before destruction.
//
// This is useful in exercising the codepath of default construction followed by
// destruction, but does not explicitly test anything. An example of where this
// might fail is a default destructor that default-initializes a scalar and a
// destructor reads the value of that member. Sanitizers can catch this as long
// as our test attempts to execute such a case.
template <class T>
struct ExpectDefaultConstructWithDestruct {
  void operator()() const {
    // Scoped so that destructor gets called before reporting success.
    {
      T object;
      static_cast<void>(object);
    }

    errors->addTestSuccess("Default construction");
  }

  ConformanceErrors* errors;
};

// Check move-assign into a default-constructed object.
template <class T, class Prof>
struct ExpectDefaultConstructWithMoveAssign {
  template <class Fun>
  void operator()(const Fun& generator) const {
    const T source_of_truth = generator();
    T object;
    object = generator();

    (ExpectEquality<T, Prof>)(errors, "Move assignment",
                              PrepareGivenContext(
                                  GivenDeclaration{"const _T object",
                                                   generator.description},
                                  GivenDeclaration{"_T object", ""},
                                  GivenDeclaration{"object",
                                                   generator.description}),
                              object, source_of_truth, "std::as_const(object)",
                              "source_of_truth");
  }

  ConformanceErrors* errors;
};

// Check copy-assign into a default-constructed object.
template <class T, class Prof>
struct ExpectDefaultConstructWithCopyAssign {
  template <class Fun>
  void operator()(const Fun& generator) const {
    const T source_of_truth = generator();
    T object;
    object = static_cast<const T&>(generator());

    (ExpectEquality<T, Prof>)(errors, "Copy assignment",
                              PrepareGivenContext(
                                  GivenDeclaration{"const _T source_of_truth",
                                                   generator.description},
                                  GivenDeclaration{"_T object", ""},
                                  GivenDeclaration{
                                      "object",
                                      std::string("static_cast<const _T&>(") +
                                          generator.description + ")"}),
                              object, source_of_truth, "std::as_const(object)",
                              "source_of_truth");
  }

  ConformanceErrors* errors;
};

// Perform a self move-assign.
template <class T, class Prof>
struct ExpectSelfMoveAssign {
  template <class Fun>
  void operator()(const Fun& generator) const {
    T object = generator();
    object = absl::move(object);

    // NOTE: Self move-assign results in a valid-but-unspecified state.

    (ExpectEquality<T, Prof>)(errors, "Move assignment",
                              PrepareGivenContext(
                                  GivenDeclaration{"_T object",
                                                   generator.description},
                                  GivenDeclaration{"object",
                                                   "std::move(object)"}),
                              object, object, "object", "object");
  }

  ConformanceErrors* errors;
};

// Perform a self copy-assign.
template <class T, class Prof>
struct ExpectSelfCopyAssign {
  template <class Fun>
  void operator()(const Fun& generator) const {
    const T source_of_truth = generator();
    T object = generator();
    const T& const_object = object;
    object = const_object;

    (ExpectEquality<T, Prof>)(errors, "Copy assignment",
                              PrepareGivenContext(
                                  GivenDeclaration{"const _T source_of_truth",
                                                   generator.description},
                                  GivenDeclaration{"_T object",
                                                   generator.description},
                                  GivenDeclaration{"object",
                                                   "std::as_const(object)"}),
                              const_object, source_of_truth,
                              "std::as_const(object)", "source_of_truth");
  }

  ConformanceErrors* errors;
};

// Perform a self-swap.
template <class T, class Prof>
struct ExpectSelfSwap {
  template <class Fun>
  void operator()(const Fun& generator) const {
    const T source_of_truth = generator();
    T object = generator();

    type_traits_internal::Swap(object, object);

    std::string preliminary_info = absl::StrCat(
        PrepareGivenContext(
            GivenDeclaration{"const _T source_of_truth", generator.description},
            GivenDeclaration{"_T object", generator.description}),
        "After performing a self-swap:\n"
        "  using std::swap;\n"
        "  swap(object, object);\n"
        "\n");

    (ExpectEquality<T, Prof>)(errors, "Swap", std::move(preliminary_info),
                              object, source_of_truth, "std::as_const(object)",
                              "source_of_truth");
  }

  ConformanceErrors* errors;
};

// Perform each of the single-generator checks when necessary operations are
// supported.
template <class T, class Prof>
struct ExpectSelfComparison {
  template <class Fun>
  void operator()(const Fun& generator) const {
    const T object = generator();
    (ExpectOneWayEquality<T, Prof>)(errors, "Comparison",
                                    PrepareGivenContext(GivenDeclaration{
                                        "const _T object",
                                        generator.description}),
                                    object, object, "object", "object");
  }

  ConformanceErrors* errors;
};

// Perform each of the single-generator checks when necessary operations are
// supported.
template <class T, class Prof>
struct ExpectConsistency {
  template <class Fun>
  void operator()(const Fun& generator) const {
    If<PropertiesOfT<Prof>::is_move_constructible>::Invoke(
        ExpectMoveConstructOneGenerator<T, Prof>{errors}, generator);

    If<PropertiesOfT<Prof>::is_copy_constructible>::Invoke(
        ExpectCopyConstructOneGenerator<T, Prof>{errors}, generator);

    If<PropertiesOfT<Prof>::is_default_constructible &&
       PropertiesOfT<Prof>::is_move_assignable>::
        Invoke(ExpectDefaultConstructWithMoveAssign<T, Prof>{errors},
               generator);

    If<PropertiesOfT<Prof>::is_default_constructible &&
       PropertiesOfT<Prof>::is_copy_assignable>::
        Invoke(ExpectDefaultConstructWithCopyAssign<T, Prof>{errors},
               generator);

    If<PropertiesOfT<Prof>::is_move_assignable>::Invoke(
        ExpectSelfMoveAssign<T, Prof>{errors}, generator);

    If<PropertiesOfT<Prof>::is_copy_assignable>::Invoke(
        ExpectSelfCopyAssign<T, Prof>{errors}, generator);

    If<PropertiesOfT<Prof>::is_swappable>::Invoke(
        ExpectSelfSwap<T, Prof>{errors}, generator);
  }

  ConformanceErrors* errors;
};

// Check move-assign with two different values.
template <class T, class Prof>
struct ExpectMoveAssign {
  template <class Fun0, class Fun1>
  void operator()(const Fun0& generator0, const Fun1& generator1) const {
    const T source_of_truth1 = generator1();
    T object = generator0();
    object = generator1();

    (ExpectEquality<T, Prof>)(errors, "Move assignment",
                              PrepareGivenContext(
                                  GivenDeclaration{"const _T source_of_truth1",
                                                   generator1.description},
                                  GivenDeclaration{"_T object",
                                                   generator0.description},
                                  GivenDeclaration{"object",
                                                   generator1.description}),
                              object, source_of_truth1, "std::as_const(object)",
                              "source_of_truth1");
  }

  ConformanceErrors* errors;
};

// Check copy-assign with two different values.
template <class T, class Prof>
struct ExpectCopyAssign {
  template <class Fun0, class Fun1>
  void operator()(const Fun0& generator0, const Fun1& generator1) const {
    const T source_of_truth1 = generator1();
    T object = generator0();
    object = static_cast<const T&>(generator1());

    (ExpectEquality<T, Prof>)(errors, "Copy assignment",
                              PrepareGivenContext(
                                  GivenDeclaration{"const _T source_of_truth1",
                                                   generator1.description},
                                  GivenDeclaration{"_T object",
                                                   generator0.description},
                                  GivenDeclaration{
                                      "object",
                                      std::string("static_cast<const _T&>(") +
                                          generator1.description + ")"}),
                              object, source_of_truth1, "std::as_const(object)",
                              "source_of_truth1");
  }

  ConformanceErrors* errors;
};

// Check swap with two different values.
template <class T, class Prof>
struct ExpectSwap {
  template <class Fun0, class Fun1>
  void operator()(const Fun0& generator0, const Fun1& generator1) const {
    const T source_of_truth0 = generator0();
    const T source_of_truth1 = generator1();
    T object0 = generator0();
    T object1 = generator1();

    type_traits_internal::Swap(object0, object1);

    const std::string context =
        PrepareGivenContext(
            GivenDeclaration{"const _T source_of_truth0",
                             generator0.description},
            GivenDeclaration{"const _T source_of_truth1",
                             generator1.description},
            GivenDeclaration{"_T object0", generator0.description},
            GivenDeclaration{"_T object1", generator1.description}) +
        "After performing a swap:\n"
        "  using std::swap;\n"
        "  swap(object0, object1);\n"
        "\n";

    (ExpectEquality<T, Prof>)(errors, "Swap", context, object0,
                              source_of_truth1, "std::as_const(object0)",
                              "source_of_truth1");
    (ExpectEquality<T, Prof>)(errors, "Swap", context, object1,
                              source_of_truth0, "std::as_const(object1)",
                              "source_of_truth0");
  }

  ConformanceErrors* errors;
};

// Validate that `generator0` and `generator1` produce values that are equal.
template <class T, class Prof>
struct ExpectEquivalenceClassComparison {
  template <class Fun0, class Fun1>
  void operator()(const Fun0& generator0, const Fun1& generator1) const {
    const T object0 = generator0();
    const T object1 = generator1();

    (ExpectEquality<T, Prof>)(errors, "Comparison",
                              PrepareGivenContext(
                                  GivenDeclaration{"const _T object0",
                                                   generator0.description},
                                  GivenDeclaration{"const _T object1",
                                                   generator1.description}),
                              object0, object1, "object0", "object1");
  }

  ConformanceErrors* errors;
};

// Validate that all objects in the same equivalence-class have the same value.
template <class T, class Prof>
struct ExpectEquivalenceClassConsistency {
  template <class Fun0, class Fun1>
  void operator()(const Fun0& generator0, const Fun1& generator1) const {
    If<PropertiesOfT<Prof>::is_move_assignable>::Invoke(
        ExpectMoveAssign<T, Prof>{errors}, generator0, generator1);

    If<PropertiesOfT<Prof>::is_copy_assignable>::Invoke(
        ExpectCopyAssign<T, Prof>{errors}, generator0, generator1);

    If<PropertiesOfT<Prof>::is_swappable>::Invoke(ExpectSwap<T, Prof>{errors},
                                                  generator0, generator1);
  }

  ConformanceErrors* errors;
};

// Given a "lesser" object and a "greater" object, perform every combination of
// comparison operators supported for the type, expecting consistent results.
template <class T, class Prof>
void ExpectOrdered(ConformanceErrors* errors, absl::string_view context,
                   const T& small, const T& big, absl::string_view small_name,
                   absl::string_view big_name) {
  const absl::string_view test_name = "Comparison";

  If<PropertiesOfT<Prof>::is_equality_comparable>::Invoke(
      ExpectNotEq{errors}, test_name, context, small, big, small_name,
      big_name);
  If<PropertiesOfT<Prof>::is_equality_comparable>::Invoke(
      ExpectNotEq{errors}, test_name, context, big, small, big_name,
      small_name);

  If<PropertiesOfT<Prof>::is_inequality_comparable>::Invoke(
      ExpectNe{errors}, test_name, context, small, big, small_name, big_name);
  If<PropertiesOfT<Prof>::is_inequality_comparable>::Invoke(
      ExpectNe{errors}, test_name, context, big, small, big_name, small_name);

  If<PropertiesOfT<Prof>::is_less_than_comparable>::Invoke(
      ExpectLt{errors}, test_name, context, small, big, small_name, big_name);
  If<PropertiesOfT<Prof>::is_less_than_comparable>::Invoke(
      ExpectNotLt{errors}, test_name, context, big, small, big_name,
      small_name);

  If<PropertiesOfT<Prof>::is_less_equal_comparable>::Invoke(
      ExpectLe{errors}, test_name, context, small, big, small_name, big_name);
  If<PropertiesOfT<Prof>::is_less_equal_comparable>::Invoke(
      ExpectNotLe{errors}, test_name, context, big, small, big_name,
      small_name);

  If<PropertiesOfT<Prof>::is_greater_equal_comparable>::Invoke(
      ExpectNotGe{errors}, test_name, context, small, big, small_name,
      big_name);
  If<PropertiesOfT<Prof>::is_greater_equal_comparable>::Invoke(
      ExpectGe{errors}, test_name, context, big, small, big_name, small_name);

  If<PropertiesOfT<Prof>::is_greater_than_comparable>::Invoke(
      ExpectNotGt{errors}, test_name, context, small, big, small_name,
      big_name);
  If<PropertiesOfT<Prof>::is_greater_than_comparable>::Invoke(
      ExpectGt{errors}, test_name, context, big, small, big_name, small_name);
}

// For every two elements of an equivalence class, makes sure that those two
// elements compare equal, including checks with the same argument passed as
// both operands.
template <class T, class Prof>
struct ExpectEquivalenceClassComparisons {
  template <class... Funs>
  void operator()(EquivalenceClassType<Funs...> eq_class) const {
    (ForEachTupleElement)(ExpectSelfComparison<T, Prof>{errors},
                          eq_class.generators);

    (ForEveryTwo)(ExpectEquivalenceClassComparison<T, Prof>{errors},
                  eq_class.generators);
  }

  ConformanceErrors* errors;
};

// For every element of an equivalence class, makes sure that the element is
// self-consistent (in other words, if any of move/copy/swap are defined,
// perform those operations and make such that results and operands still
// compare equal to known values whenever it is required for that operation.
template <class T, class Prof>
struct ExpectEquivalenceClass {
  template <class... Funs>
  void operator()(EquivalenceClassType<Funs...> eq_class) const {
    (ForEachTupleElement)(ExpectConsistency<T, Prof>{errors},
                          eq_class.generators);

    (ForEveryTwo)(ExpectEquivalenceClassConsistency<T, Prof>{errors},
                  eq_class.generators);
  }

  ConformanceErrors* errors;
};

// Validate that the passed-in argument is a generator of a greater value than
// the one produced by the "small_gen" datamember with respect to all of the
// comparison operators that Prof requires, with both argument orders to test.
template <class T, class Prof, class SmallGenerator>
struct ExpectBiggerGeneratorThanComparisons {
  template <class BigGenerator>
  void operator()(BigGenerator big_gen) const {
    const T small = small_gen();
    const T big = big_gen();

    (ExpectOrdered<T, Prof>)(errors,
                             PrepareGivenContext(
                                 GivenDeclaration{"const _T small",
                                                  small_gen.description},
                                 GivenDeclaration{"const _T big",
                                                  big_gen.description}),
                             small, big, "small", "big");
  }

  SmallGenerator small_gen;
  ConformanceErrors* errors;
};

// Perform all of the move, copy, and swap checks on the value generated by
// `small_gen` and the value generated by `big_gen`.
template <class T, class Prof, class SmallGenerator>
struct ExpectBiggerGeneratorThan {
  template <class BigGenerator>
  void operator()(BigGenerator big_gen) const {
    If<PropertiesOfT<Prof>::is_move_assignable>::Invoke(
        ExpectMoveAssign<T, Prof>{errors}, small_gen, big_gen);
    If<PropertiesOfT<Prof>::is_move_assignable>::Invoke(
        ExpectMoveAssign<T, Prof>{errors}, big_gen, small_gen);

    If<PropertiesOfT<Prof>::is_copy_assignable>::Invoke(
        ExpectCopyAssign<T, Prof>{errors}, small_gen, big_gen);
    If<PropertiesOfT<Prof>::is_copy_assignable>::Invoke(
        ExpectCopyAssign<T, Prof>{errors}, big_gen, small_gen);

    If<PropertiesOfT<Prof>::is_swappable>::Invoke(ExpectSwap<T, Prof>{errors},
                                                  small_gen, big_gen);
  }

  SmallGenerator small_gen;
  ConformanceErrors* errors;
};

// Validate that the result of a generator is greater than the results of all
// generators in an equivalence class with respect to comparisons.
template <class T, class Prof, class SmallGenerator>
struct ExpectBiggerGeneratorThanEqClassesComparisons {
  template <class BigEqClass>
  void operator()(BigEqClass big_eq_class) const {
    (ForEachTupleElement)(
        ExpectBiggerGeneratorThanComparisons<T, Prof, SmallGenerator>{small_gen,
                                                                      errors},
        big_eq_class.generators);
  }

  SmallGenerator small_gen;
  ConformanceErrors* errors;
};

// Validate that the non-comparison binary operations required by Prof are
// correct for the result of each generator of big_eq_class and a generator of
// the logically smaller value returned by small_gen.
template <class T, class Prof, class SmallGenerator>
struct ExpectBiggerGeneratorThanEqClasses {
  template <class BigEqClass>
  void operator()(BigEqClass big_eq_class) const {
    (ForEachTupleElement)(
        ExpectBiggerGeneratorThan<T, Prof, SmallGenerator>{small_gen, errors},
        big_eq_class.generators);
  }

  SmallGenerator small_gen;
  ConformanceErrors* errors;
};

// Validate that each equivalence class that is passed is logically less than
// the equivalence classes that comes later on in the argument list.
template <class T, class Prof>
struct ExpectOrderedEquivalenceClassesComparisons {
  template <class... BigEqClasses>
  struct Impl {
    // Validate that the value produced by `small_gen` is less than all of the
    // values generated by those of the logically larger equivalence classes.
    template <class SmallGenerator>
    void operator()(SmallGenerator small_gen) const {
      (ForEachTupleElement)(ExpectBiggerGeneratorThanEqClassesComparisons<
                                T, Prof, SmallGenerator>{small_gen, errors},
                            big_eq_classes);
    }

    std::tuple<BigEqClasses...> big_eq_classes;
    ConformanceErrors* errors;
  };

  // When given no equivalence classes, no validation is necessary.
  void operator()() const {}

  template <class SmallEqClass, class... BigEqClasses>
  void operator()(SmallEqClass small_eq_class,
                  BigEqClasses... big_eq_classes) const {
    // For each generator in the first equivalence class, make sure that it is
    // less than each of those in the logically greater equivalence classes.
    (ForEachTupleElement)(
        Impl<BigEqClasses...>{std::make_tuple(absl::move(big_eq_classes)...),
                              errors},
        small_eq_class.generators);

    // Recurse so that all equivalence class combinations are checked.
    (*this)(absl::move(big_eq_classes)...);
  }

  ConformanceErrors* errors;
};

// Validate that the non-comparison binary operations required by Prof are
// correct for the result of each generator of big_eq_classes and a generator of
// the logically smaller value returned by small_gen.
template <class T, class Prof>
struct ExpectOrderedEquivalenceClasses {
  template <class... BigEqClasses>
  struct Impl {
    template <class SmallGenerator>
    void operator()(SmallGenerator small_gen) const {
      (ForEachTupleElement)(
          ExpectBiggerGeneratorThanEqClasses<T, Prof, SmallGenerator>{small_gen,
                                                                      errors},
          big_eq_classes);
    }

    std::tuple<BigEqClasses...> big_eq_classes;
    ConformanceErrors* errors;
  };

  // Check that small_eq_class is logically consistent and also is logically
  // less than all values in big_eq_classes.
  template <class SmallEqClass, class... BigEqClasses>
  void operator()(SmallEqClass small_eq_class,
                  BigEqClasses... big_eq_classes) const {
    (ForEachTupleElement)(
        Impl<BigEqClasses...>{std::make_tuple(absl::move(big_eq_classes)...),
                              errors},
        small_eq_class.generators);

    (*this)(absl::move(big_eq_classes)...);
  }

  // Terminating case of operator().
  void operator()() const {}

  ConformanceErrors* errors;
};

// Validate that a type meets the syntactic requirements of std::hash if the
// range of profiles requires it.
template <class T, class MinProf, class MaxProf>
struct ExpectHashable {
  void operator()() const {
    ExpectModelOfHashable<T, MinProf, MaxProf>(errors);
  }

  ConformanceErrors* errors;
};

// Validate that the type `T` meets all of the requirements associated with
// `MinProf` and without going beyond the syntactic properties of `MaxProf`.
template <class T, class MinProf, class MaxProf>
struct ExpectModels {
  void operator()(ConformanceErrors* errors) const {
    ExpectModelOfDefaultConstructible<T, MinProf, MaxProf>(errors);
    ExpectModelOfMoveConstructible<T, MinProf, MaxProf>(errors);
    ExpectModelOfCopyConstructible<T, MinProf, MaxProf>(errors);
    ExpectModelOfMoveAssignable<T, MinProf, MaxProf>(errors);
    ExpectModelOfCopyAssignable<T, MinProf, MaxProf>(errors);
    ExpectModelOfDestructible<T, MinProf, MaxProf>(errors);
    ExpectModelOfEqualityComparable<T, MinProf, MaxProf>(errors);
    ExpectModelOfInequalityComparable<T, MinProf, MaxProf>(errors);
    ExpectModelOfLessThanComparable<T, MinProf, MaxProf>(errors);
    ExpectModelOfLessEqualComparable<T, MinProf, MaxProf>(errors);
    ExpectModelOfGreaterEqualComparable<T, MinProf, MaxProf>(errors);
    ExpectModelOfGreaterThanComparable<T, MinProf, MaxProf>(errors);
    ExpectModelOfSwappable<T, MinProf, MaxProf>(errors);

    // Only check hashability on compilers that have a compliant default-hash.
    If<!poisoned_hash_fails_instantiation()>::Invoke(
        ExpectHashable<T, MinProf, MaxProf>{errors});
  }
};

// A metafunction that yields a Profile matching the set of properties that are
// safe to be checked (lack-of-hashability is only checked on standard library
// implementations that are standards compliant in that they provide a std::hash
// primary template that is SFINAE-friendly)
template <class LogicalProf, class T>
struct MinimalCheckableProfile {
  using type =
      MinimalProfiles<PropertiesOfT<LogicalProf>,
                      PropertiesOfT<SyntacticConformanceProfileOf<
                          T, !PropertiesOfT<LogicalProf>::is_hashable &&
                                     poisoned_hash_fails_instantiation()
                                 ? CheckHashability::no
                                 : CheckHashability::yes>>>;
};

// An identity metafunction
template <class T>
struct Always {
  using type = T;
};

// Validate the T meets all of the necessary requirements of LogicalProf, with
// syntactic requirements defined by the profile range [MinProf, MaxProf].
template <class T, class LogicalProf, class MinProf, class MaxProf,
          class... EqClasses>
ConformanceErrors ExpectRegularityImpl(
    OrderedEquivalenceClasses<EqClasses...> vals) {
  ConformanceErrors errors((NameOf<T>()));

  If<!constexpr_instantiation_when_unevaluated()>::Invoke(
      ExpectModels<T, MinProf, MaxProf>(), &errors);

  using minimal_profile = typename absl::conditional_t<
      constexpr_instantiation_when_unevaluated(), Always<LogicalProf>,
      MinimalCheckableProfile<LogicalProf, T>>::type;

  If<PropertiesOfT<minimal_profile>::is_default_constructible>::Invoke(
      ExpectDefaultConstructWithDestruct<T>{&errors});

  //////////////////////////////////////////////////////////////////////////////
  // Perform all comparison checks first, since later checks depend on their
  // correctness.
  //
  // Check all of the comparisons for all values in the same equivalence
  // class (equal with respect to comparison operators and hash the same).
  (ForEachTupleElement)(
      ExpectEquivalenceClassComparisons<T, minimal_profile>{&errors},
      vals.eq_classes);

  // Check all of the comparisons for each combination of values that are in
  // different equivalence classes (not equal with respect to comparison
  // operators).
  absl::apply(
      ExpectOrderedEquivalenceClassesComparisons<T, minimal_profile>{&errors},
      vals.eq_classes);
  //
  //////////////////////////////////////////////////////////////////////////////

  // Perform remaining checks, relying on comparisons.
  // TODO(calabrese) short circuit if any comparisons above failed.
  (ForEachTupleElement)(ExpectEquivalenceClass<T, minimal_profile>{&errors},
                        vals.eq_classes);

  absl::apply(ExpectOrderedEquivalenceClasses<T, minimal_profile>{&errors},
              vals.eq_classes);

  return errors;
}

// A type that represents a range of profiles that are acceptable to be matched.
//
// `MinProf` is the minimum set of syntactic requirements that must be met.
//
// `MaxProf` is the maximum set of syntactic requirements that must be met.
// This maximum is particularly useful for certain "strictness" checking. Some
// examples for when this is useful:
//
// * Making sure that a type is move-only (rather than simply movable)
//
// * Making sure that a member function is *not* noexcept in cases where it
//   cannot be noexcept, such as if a dependent datamember has certain
//   operations that are not noexcept.
//
// * Making sure that a type tightly matches a spec, such as the standard.
//
// `LogicalProf` is the Profile for which run-time testing is to take place.
//
// Note: The reason for `LogicalProf` is because it is often the case, when
// dealing with templates, that a declaration of a given operation is specified,
// but whose body would fail to instantiate. Examples include the
// copy-constructor of a standard container when the element-type is move-only,
// or the comparison operators of a standard container when the element-type
// does not have the necessary comparison operations defined. The `LogicalProf`
// parameter allows us to capture the intent of what should be tested at
// run-time, even in the cases where syntactically it might otherwise appear as
// though the type undergoing testing supports more than it actually does.
template <class LogicalProf, class MinProf = LogicalProf,
          class MaxProf = MinProf>
struct ProfileRange {
  using logical_profile = LogicalProf;
  using min_profile = MinProf;
  using max_profile = MaxProf;
};

// Similar to ProfileRange except that it creates a profile range that is
// coupled with a Domain and is used when testing that a type matches exactly
// the "minimum" requirements of LogicalProf.
template <class StrictnessDomain, class LogicalProf,
          class MinProf = LogicalProf, class MaxProf = MinProf>
struct StrictProfileRange {
  // We do not yet support extension.
  static_assert(
      std::is_same<StrictnessDomain, RegularityDomain>::value,
      "Currently, the only valid StrictnessDomain is RegularityDomain.");
  using strictness_domain = StrictnessDomain;
  using logical_profile = LogicalProf;
  using min_profile = MinProf;
  using max_profile = MaxProf;
};

////////////////////////////////////////////////////////////////////////////////
//
// A metafunction that creates a StrictProfileRange from a Domain and either a
// Profile or ProfileRange.
template <class StrictnessDomain, class ProfOrRange>
struct MakeStrictProfileRange;

template <class StrictnessDomain, class LogicalProf>
struct MakeStrictProfileRange {
  using type = StrictProfileRange<StrictnessDomain, LogicalProf>;
};

template <class StrictnessDomain, class LogicalProf, class MinProf,
          class MaxProf>
struct MakeStrictProfileRange<StrictnessDomain,
                              ProfileRange<LogicalProf, MinProf, MaxProf>> {
  using type =
      StrictProfileRange<StrictnessDomain, LogicalProf, MinProf, MaxProf>;
};

template <class StrictnessDomain, class ProfOrRange>
using MakeStrictProfileRangeT =
    typename MakeStrictProfileRange<StrictnessDomain, ProfOrRange>::type;
//
////////////////////////////////////////////////////////////////////////////////

// A profile in the RegularityDomain with the strongest possible requirements.
using MostStrictProfile =
    CombineProfiles<TriviallyCompleteProfile, NothrowComparableProfile>;

// Forms a ProfileRange that treats the Profile as the bare minimum requirements
// of a type.
template <class LogicalProf, class MinProf = LogicalProf>
using LooseProfileRange = StrictProfileRange<RegularityDomain, LogicalProf,
                                             MinProf, MostStrictProfile>;

template <class Prof>
using MakeLooseProfileRangeT = Prof;

////////////////////////////////////////////////////////////////////////////////
//
// The following classes implement the metafunction ProfileRangeOfT<T> that
// takes either a Profile or ProfileRange and yields the ProfileRange to be
// used during testing.
//
template <class T, class /*Enabler*/ = void>
struct ProfileRangeOfImpl;

template <class T>
struct ProfileRangeOfImpl<T, absl::void_t<PropertiesOfT<T>>> {
  using type = LooseProfileRange<T>;
};

template <class T>
struct ProfileRangeOf : ProfileRangeOfImpl<T> {};

template <class StrictnessDomain, class LogicalProf, class MinProf,
          class MaxProf>
struct ProfileRangeOf<
    StrictProfileRange<StrictnessDomain, LogicalProf, MinProf, MaxProf>> {
  using type =
      StrictProfileRange<StrictnessDomain, LogicalProf, MinProf, MaxProf>;
};

template <class T>
using ProfileRangeOfT = typename ProfileRangeOf<T>::type;
//
////////////////////////////////////////////////////////////////////////////////

// Extract the logical profile of a range (what will be runtime tested).
template <class T>
using LogicalProfileOfT = typename ProfileRangeOfT<T>::logical_profile;

// Extract the minimal syntactic profile of a range (error if not at least).
template <class T>
using MinProfileOfT = typename ProfileRangeOfT<T>::min_profile;

// Extract the maximum syntactic profile of a range (error if more than).
template <class T>
using MaxProfileOfT = typename ProfileRangeOfT<T>::max_profile;

////////////////////////////////////////////////////////////////////////////////
//
template <class T>
struct IsProfileOrProfileRange : IsProfile<T>::type {};

template <class StrictnessDomain, class LogicalProf, class MinProf,
          class MaxProf>
struct IsProfileOrProfileRange<
    StrictProfileRange<StrictnessDomain, LogicalProf, MinProf, MaxProf>>
    : std::true_type {};
//
////////////////////////////////////////////////////////////////////////////////

// TODO(calabrese): Consider naming the functions in this class the same as
// the macros (defined later on) so that auto-complete leads to the correct name
// and so that a user cannot accidentally call a function rather than the macro
// form.
template <bool ExpectSuccess, class T, class... EqClasses>
struct ExpectConformanceOf {
  // Add a value to be tested. Subsequent calls to this function on the same
  // object must specify logically "larger" values with respect to the
  // comparison operators of the type, if any.
  //
  // NOTE: This function should not be called directly. A stateless lambda is
  // implicitly formed and passed when using the INITIALIZER macro at the bottom
  // of this file.
  template <class Fun,
            absl::enable_if_t<std::is_same<
                ResultOfGeneratorT<GeneratorType<Fun>>, T>::value>** = nullptr>
  ABSL_MUST_USE_RESULT ExpectConformanceOf<ExpectSuccess, T, EqClasses...,
                                           EquivalenceClassType<Fun>>
  initializer(GeneratorType<Fun> fun) && {
    return {
        {std::tuple_cat(absl::move(ordered_vals.eq_classes),
                        std::make_tuple((EquivalenceClass)(absl::move(fun))))},
        std::move(expected_failed_tests)};
  }

  template <class... TestNames,
            absl::enable_if_t<!ExpectSuccess && sizeof...(EqClasses) == 0 &&
                              absl::conjunction<std::is_convertible<
                                  TestNames, absl::string_view>...>::value>** =
                nullptr>
  ABSL_MUST_USE_RESULT ExpectConformanceOf<ExpectSuccess, T, EqClasses...>
  due_to(TestNames&&... test_names) && {
    (InsertEach)(&expected_failed_tests,
                 absl::AsciiStrToLower(absl::string_view(test_names))...);

    return {absl::move(ordered_vals), std::move(expected_failed_tests)};
  }

  template <class... TestNames, int = 0,  // MSVC disambiguator
            absl::enable_if_t<ExpectSuccess && sizeof...(EqClasses) == 0 &&
                              absl::conjunction<std::is_convertible<
                                  TestNames, absl::string_view>...>::value>** =
                nullptr>
  ABSL_MUST_USE_RESULT ExpectConformanceOf<ExpectSuccess, T, EqClasses...>
  due_to(TestNames&&... test_names) && {
    // TODO(calabrese) Instead have DUE_TO only exist via a CRTP base.
    // This would produce better errors messages than the static_assert.
    static_assert(!ExpectSuccess,
                  "DUE_TO cannot be called when conformance is expected -- did "
                  "you mean to use ASSERT_NONCONFORMANCE_OF?");
  }

  // Add a value to be tested. Subsequent calls to this function on the same
  // object must specify logically "larger" values with respect to the
  // comparison operators of the type, if any.
  //
  // NOTE: This function should not be called directly. A stateful lambda is
  // implicitly formed and passed when using the INITIALIZER macro at the bottom
  // of this file.
  template <class Fun,
            absl::enable_if_t<std::is_same<
                ResultOfGeneratorT<GeneratorType<Fun>>, T>::value>** = nullptr>
  ABSL_MUST_USE_RESULT ExpectConformanceOf<ExpectSuccess, T, EqClasses...,
                                           EquivalenceClassType<Fun>>
  dont_class_directly_stateful_initializer(GeneratorType<Fun> fun) && {
    return {
        {std::tuple_cat(absl::move(ordered_vals.eq_classes),
                        std::make_tuple((EquivalenceClass)(absl::move(fun))))},
        std::move(expected_failed_tests)};
  }

  // Add a set of value to be tested, where each value is equal with respect to
  // the comparison operators and std::hash specialization, if defined.
  template <
      class... Funs,
      absl::void_t<absl::enable_if_t<std::is_same<
          ResultOfGeneratorT<GeneratorType<Funs>>, T>::value>...>** = nullptr>
  ABSL_MUST_USE_RESULT ExpectConformanceOf<ExpectSuccess, T, EqClasses...,
                                           EquivalenceClassType<Funs...>>
  equivalence_class(GeneratorType<Funs>... funs) && {
    return {{std::tuple_cat(
                absl::move(ordered_vals.eq_classes),
                std::make_tuple((EquivalenceClass)(absl::move(funs)...)))},
            std::move(expected_failed_tests)};
  }

  // Execute the tests for the captured set of values, strictly matching a range
  // of expected profiles in a given domain.
  template <
      class ProfRange,
      absl::enable_if_t<IsProfileOrProfileRange<ProfRange>::value>** = nullptr>
  ABSL_MUST_USE_RESULT ::testing::AssertionResult with_strict_profile(
      ProfRange /*profile*/) {
    ConformanceErrors test_result =
        (ExpectRegularityImpl<
            T, LogicalProfileOfT<ProfRange>, MinProfileOfT<ProfRange>,
            MaxProfileOfT<ProfRange>>)(absl::move(ordered_vals));

    return ExpectSuccess ? test_result.assertionResult()
                         : test_result.expectFailedTests(expected_failed_tests);
  }

  // Execute the tests for the captured set of values, loosely matching a range
  // of expected profiles (loose in that an interface is allowed to be more
  // refined that a profile suggests, such as a type having a noexcept copy
  // constructor when all that is required is that the copy constructor exists).
  template <class Prof, absl::enable_if_t<IsProfile<Prof>::value>** = nullptr>
  ABSL_MUST_USE_RESULT ::testing::AssertionResult with_loose_profile(
      Prof /*profile*/) {
    ConformanceErrors test_result =
        (ExpectRegularityImpl<
            T, Prof, Prof,
            CombineProfiles<TriviallyCompleteProfile,
                            NothrowComparableProfile>>)(absl::
                                                            move(ordered_vals));

    return ExpectSuccess ? test_result.assertionResult()
                         : test_result.expectFailedTests(expected_failed_tests);
  }

  OrderedEquivalenceClasses<EqClasses...> ordered_vals;
  std::set<std::string> expected_failed_tests;
};

template <class T>
using ExpectConformanceOfType = ExpectConformanceOf</*ExpectSuccess=*/true, T>;

template <class T>
using ExpectNonconformanceOfType =
    ExpectConformanceOf</*ExpectSuccess=*/false, T>;

struct EquivalenceClassMaker {
  // TODO(calabrese) Constrain to callable
  template <class Fun>
  static GeneratorType<Fun> initializer(GeneratorType<Fun> fun) {
    return fun;
  }
};

// A top-level macro that begins the builder pattern.
//
// The argument here takes the datatype to be tested.
#define ABSL_INTERNAL_ASSERT_CONFORMANCE_OF(...)                            \
  GTEST_AMBIGUOUS_ELSE_BLOCKER_                                             \
  if ABSL_INTERNAL_LPAREN                                                   \
  const ::testing::AssertionResult gtest_ar =                               \
      ABSL_INTERNAL_LPAREN ::absl::types_internal::ExpectConformanceOfType< \
          __VA_ARGS__>()

// Akin to ASSERT_CONFORMANCE_OF except that it expects failure and tries to
// match text.
#define ABSL_INTERNAL_ASSERT_NONCONFORMANCE_OF(...)                            \
  GTEST_AMBIGUOUS_ELSE_BLOCKER_                                                \
  if ABSL_INTERNAL_LPAREN                                                      \
  const ::testing::AssertionResult gtest_ar =                                  \
      ABSL_INTERNAL_LPAREN ::absl::types_internal::ExpectNonconformanceOfType< \
          __VA_ARGS__>()

////////////////////////////////////////////////////////////////////////////////
// NOTE: The following macros look like they are recursive, but are not (macros
// cannot recurse). These actually refer to member functions of the same name.
// This is done intentionally so that a user cannot accidentally invoke a
// member function of the conformance-testing suite without going through the
// macro.
////////////////////////////////////////////////////////////////////////////////

// Specify expected test failures as comma-separated strings.
#define DUE_TO(...) due_to(__VA_ARGS__)

// Specify a value to be tested.
//
// Note: Internally, this takes an expression and turns it into the return value
// of lambda that captures no data. The expression is stringized during
// preprocessing so that it can be used in error reports.
#define INITIALIZER(...)                         \
  initializer(::absl::types_internal::Generator( \
      [] { return __VA_ARGS__; }, ABSL_INTERNAL_STRINGIZE(__VA_ARGS__)))

// Specify a value to be tested.
//
// Note: Internally, this takes an expression and turns it into the return value
// of lambda that captures data by reference. The expression is stringized
// during preprocessing so that it can be used in error reports.
#define STATEFUL_INITIALIZER(...)                         \
  stateful_initializer(::absl::types_internal::Generator( \
      [&] { return __VA_ARGS__; }, ABSL_INTERNAL_STRINGIZE(__VA_ARGS__)))

// Used in the builder-pattern.
//
// Takes a series of INITIALIZER and/or STATEFUL_INITIALIZER invocations and
// forwards them along to be tested, grouping them such that the testing suite
// knows that they are supposed to represent the same logical value (the values
// compare the same, hash the same, etc.).
#define EQUIVALENCE_CLASS(...)                    \
  equivalence_class(ABSL_INTERNAL_TRANSFORM_ARGS( \
      ABSL_INTERNAL_PREPEND_EQ_MAKER, __VA_ARGS__))

// An invocation of this or WITH_STRICT_PROFILE must end the builder-pattern.
// It takes a Profile as its argument.
//
// This executes the tests and allows types that are "more referined" than the
// profile specifies, but not less. For instance, if the Profile specifies
// noexcept copy-constructiblity, the test will fail if the copy-constructor is
// not noexcept, however, it will succeed if the copy constructor is trivial.
//
// This is useful for testing that a type meets some minimum set of
// requirements.
#define WITH_LOOSE_PROFILE(...)                                      \
  with_loose_profile(                                                \
      ::absl::types_internal::MakeLooseProfileRangeT<__VA_ARGS__>()) \
      ABSL_INTERNAL_RPAREN ABSL_INTERNAL_RPAREN;                     \
  else GTEST_FATAL_FAILURE_(gtest_ar.failure_message())  // NOLINT

// An invocation of this or WITH_STRICT_PROFILE must end the builder-pattern.
// It takes a Domain and a Profile as its arguments.
//
// This executes the tests and disallows types that differ at all from the
// properties of the Profile. For instance, if the Profile specifies noexcept
// copy-constructiblity, the test will fail if the copy constructor is trivial.
//
// This is useful for testing that a type does not do anything more than a
// specification requires, such as to minimize things like Hyrum's Law, or more
// commonly, to prevent a type from being "accidentally" copy-constructible in
// a way that may produce incorrect results, simply because the user forget to
// delete that operation.
#define WITH_STRICT_PROFILE(...)                                      \
  with_strict_profile(                                                \
      ::absl::types_internal::MakeStrictProfileRangeT<__VA_ARGS__>()) \
      ABSL_INTERNAL_RPAREN ABSL_INTERNAL_RPAREN;                      \
  else GTEST_FATAL_FAILURE_(gtest_ar.failure_message())  // NOLINT

// Internal macro that is used in the internals of the EDSL when forming
// equivalence classes.
#define ABSL_INTERNAL_PREPEND_EQ_MAKER(arg) \
  ::absl::types_internal::EquivalenceClassMaker().arg

}  // namespace types_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TYPES_INTERNAL_CONFORMANCE_TESTING_H_
