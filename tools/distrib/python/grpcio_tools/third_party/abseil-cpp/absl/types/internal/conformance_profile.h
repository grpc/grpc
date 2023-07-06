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
// conformance_profiles.h
// -----------------------------------------------------------------------------
//
// This file contains templates for representing "Regularity Profiles" and
// concisely-named versions of commonly used Regularity Profiles.
//
// A Regularity Profile is a compile-time description of the types of operations
// that a given type supports, along with properties of those operations when
// they do exist. For instance, a Regularity Profile may describe a type that
// has a move-constructor that is noexcept and a copy constructor that is not
// noexcept. This description can then be examined and passed around to other
// templates for the purposes of asserting expectations on user-defined types
// via a series trait checks, or for determining what kinds of run-time tests
// are able to be performed.
//
// Regularity Profiles are also used when creating "archetypes," which are
// minimum-conforming types that meet all of the requirements of a given
// Regularity Profile. For more information regarding archetypes, see
// "conformance_archetypes.h".

#ifndef ABSL_TYPES_INTERNAL_CONFORMANCE_PROFILE_H_
#define ABSL_TYPES_INTERNAL_CONFORMANCE_PROFILE_H_

#include <set>
#include <type_traits>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/algorithm/container.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/internal/conformance_testing_helpers.h"
#include "absl/utility/utility.h"

// TODO(calabrese) Add support for extending profiles.

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace types_internal {

// Converts an enum to its underlying integral value.
template <typename Enum>
constexpr absl::underlying_type_t<Enum> UnderlyingValue(Enum value) {
  return static_cast<absl::underlying_type_t<Enum>>(value);
}

// A tag type used in place of a matcher when checking that an assertion result
// does not actually contain any errors.
struct NoError {};

// -----------------------------------------------------------------------------
// ConformanceErrors
// -----------------------------------------------------------------------------
class ConformanceErrors {
 public:
  // Setup the error reporting mechanism by seeding it with the name of the type
  // that is being tested.
  explicit ConformanceErrors(std::string type_name)
      : assertion_result_(false), type_name_(std::move(type_name)) {
    assertion_result_ << "\n\n"
                         "Assuming the following type alias:\n"
                         "\n"
                         "  using _T = "
                      << type_name_ << ";\n\n";
    outputDivider();
  }

  // Adds the test name to the list of successfully run tests iff it was not
  // previously reported as failing. This behavior is useful for tests that
  // have multiple parts, where failures and successes are reported individually
  // with the same test name.
  void addTestSuccess(absl::string_view test_name) {
    auto normalized_test_name = absl::AsciiStrToLower(test_name);

    // If the test is already reported as failing, do not add it to the list of
    // successes.
    if (test_failures_.find(normalized_test_name) == test_failures_.end()) {
      test_successes_.insert(std::move(normalized_test_name));
    }
  }

  // Streams a single error description into the internal buffer (a visual
  // divider is automatically inserted after the error so that multiple errors
  // are visibly distinct).
  //
  // This function increases the error count by 1.
  //
  // TODO(calabrese) Determine desired behavior when if this function throws.
  template <class... P>
  void addTestFailure(absl::string_view test_name, const P&... args) {
    // Output a message related to the test failure.
    assertion_result_ << "\n\n"
                         "Failed test: "
                      << test_name << "\n\n";
    addTestFailureImpl(args...);
    assertion_result_ << "\n\n";
    outputDivider();

    auto normalized_test_name = absl::AsciiStrToLower(test_name);

    // If previous parts of this test succeeded, remove it from that set.
    test_successes_.erase(normalized_test_name);

    // Add the test name to the list of failed tests.
    test_failures_.insert(std::move(normalized_test_name));

    has_error_ = true;
  }

  // Convert this object into a testing::AssertionResult instance such that it
  // can be used with gtest.
  ::testing::AssertionResult assertionResult() const {
    return has_error_ ? assertion_result_ : ::testing::AssertionSuccess();
  }

  // Convert this object into a testing::AssertionResult instance such that it
  // can be used with gtest. This overload expects errors, using the specified
  // matcher.
  ::testing::AssertionResult expectFailedTests(
      const std::set<std::string>& test_names) const {
    // Since we are expecting nonconformance, output an error message when the
    // type actually conformed to the specified profile.
    if (!has_error_) {
      return ::testing::AssertionFailure()
             << "Unexpected conformance of type:\n"
                "    "
             << type_name_ << "\n\n";
    }

    // Get a list of all expected failures that did not actually fail
    // (or that were not run).
    std::vector<std::string> nonfailing_tests;
    absl::c_set_difference(test_names, test_failures_,
                           std::back_inserter(nonfailing_tests));

    // Get a list of all "expected failures" that were never actually run.
    std::vector<std::string> unrun_tests;
    absl::c_set_difference(nonfailing_tests, test_successes_,
                           std::back_inserter(unrun_tests));

    // Report when the user specified tests that were not run.
    if (!unrun_tests.empty()) {
      const bool tests_were_run =
          !(test_failures_.empty() && test_successes_.empty());

      // Prepare an assertion result used in the case that tests pass that were
      // expected to fail.
      ::testing::AssertionResult result = ::testing::AssertionFailure();
      result << "When testing type:\n    " << type_name_
             << "\n\nThe following tests were expected to fail but were not "
                "run";

      if (tests_were_run) result << " (was the test name spelled correctly?)";

      result << ":\n\n";

      // List all of the tests that unexpectedly passed.
      for (const auto& test_name : unrun_tests) {
        result << "    " << test_name << "\n";
      }

      if (!tests_were_run) result << "\nNo tests were run.";

      if (!test_failures_.empty()) {
        // List test failures
        result << "\nThe tests that were run and failed are:\n\n";
        for (const auto& test_name : test_failures_) {
          result << "    " << test_name << "\n";
        }
      }

      if (!test_successes_.empty()) {
        // List test successes
        result << "\nThe tests that were run and succeeded are:\n\n";
        for (const auto& test_name : test_successes_) {
          result << "    " << test_name << "\n";
        }
      }

      return result;
    }

    // If some tests passed when they were expected to fail, alert the caller.
    if (nonfailing_tests.empty()) return ::testing::AssertionSuccess();

    // Prepare an assertion result used in the case that tests pass that were
    // expected to fail.
    ::testing::AssertionResult unexpected_successes =
        ::testing::AssertionFailure();
    unexpected_successes << "When testing type:\n    " << type_name_
                         << "\n\nThe following tests passed when they were "
                            "expected to fail:\n\n";

    // List all of the tests that unexpectedly passed.
    for (const auto& test_name : nonfailing_tests) {
      unexpected_successes << "    " << test_name << "\n";
    }

    return unexpected_successes;
  }

 private:
  void outputDivider() {
    assertion_result_ << "========================================";
  }

  void addTestFailureImpl() {}

  template <class H, class... T>
  void addTestFailureImpl(const H& head, const T&... tail) {
    assertion_result_ << head;
    addTestFailureImpl(tail...);
  }

  ::testing::AssertionResult assertion_result_;
  std::set<std::string> test_failures_;
  std::set<std::string> test_successes_;
  std::string type_name_;
  bool has_error_ = false;
};

template <class T, class /*Enabler*/ = void>
struct PropertiesOfImpl {};

template <class T>
struct PropertiesOfImpl<T, absl::void_t<typename T::properties>> {
  using type = typename T::properties;
};

template <class T>
struct PropertiesOfImpl<T, absl::void_t<typename T::profile_alias_of>> {
  using type = typename PropertiesOfImpl<typename T::profile_alias_of>::type;
};

template <class T>
struct PropertiesOf : PropertiesOfImpl<T> {};

template <class T>
using PropertiesOfT = typename PropertiesOf<T>::type;

// NOTE: These enums use this naming convention to be consistent with the
// standard trait names, which is useful since it allows us to match up each
// enum name with a corresponding trait name in macro definitions.

// An enum that describes the various expectations on an operations existence.
enum class function_support { maybe, yes, nothrow, trivial };

constexpr const char* PessimisticPropertyDescription(function_support v) {
  return v == function_support::maybe
             ? "no"
             : v == function_support::yes
                   ? "yes, potentially throwing"
                   : v == function_support::nothrow ? "yes, nothrow"
                                                    : "yes, trivial";
}

// Return a string that describes the kind of property support that was
// expected.
inline std::string ExpectedFunctionKindList(function_support min,
                                            function_support max) {
  if (min == max) {
    std::string result =
        absl::StrCat("Expected:\n  ",
                     PessimisticPropertyDescription(
                         static_cast<function_support>(UnderlyingValue(min))),
                     "\n");
    return result;
  }

  std::string result = "Expected one of:\n";
  for (auto curr_support = UnderlyingValue(min);
       curr_support <= UnderlyingValue(max); ++curr_support) {
    absl::StrAppend(&result, "  ",
                    PessimisticPropertyDescription(
                        static_cast<function_support>(curr_support)),
                    "\n");
  }

  return result;
}

template <class Enum>
void ExpectModelOfImpl(ConformanceErrors* errors, Enum min_support,
                       Enum max_support, Enum kind) {
  const auto kind_value = UnderlyingValue(kind);
  const auto min_support_value = UnderlyingValue(min_support);
  const auto max_support_value = UnderlyingValue(max_support);

  if (!(kind_value >= min_support_value && kind_value <= max_support_value)) {
    errors->addTestFailure(
        PropertyName(kind), "**Failed property expectation**\n\n",
        ExpectedFunctionKindList(
            static_cast<function_support>(min_support_value),
            static_cast<function_support>(max_support_value)),
        '\n', "Actual:\n  ",
        PessimisticPropertyDescription(
            static_cast<function_support>(kind_value)));
  } else {
    errors->addTestSuccess(PropertyName(kind));
  }
}

#define ABSL_INTERNAL_SPECIAL_MEMBER_FUNCTION_ENUM(description, name) \
  enum class name { maybe, yes, nothrow, trivial };                   \
                                                                      \
  constexpr const char* PropertyName(name v) { return description; }  \
  static_assert(true, "")  // Force a semicolon when using this macro.

ABSL_INTERNAL_SPECIAL_MEMBER_FUNCTION_ENUM("support for default construction",
                                           default_constructible);
ABSL_INTERNAL_SPECIAL_MEMBER_FUNCTION_ENUM("support for move construction",
                                           move_constructible);
ABSL_INTERNAL_SPECIAL_MEMBER_FUNCTION_ENUM("support for copy construction",
                                           copy_constructible);
ABSL_INTERNAL_SPECIAL_MEMBER_FUNCTION_ENUM("support for move assignment",
                                           move_assignable);
ABSL_INTERNAL_SPECIAL_MEMBER_FUNCTION_ENUM("support for copy assignment",
                                           copy_assignable);
ABSL_INTERNAL_SPECIAL_MEMBER_FUNCTION_ENUM("support for destruction",
                                           destructible);

#undef ABSL_INTERNAL_SPECIAL_MEMBER_FUNCTION_ENUM

#define ABSL_INTERNAL_INTRINSIC_FUNCTION_ENUM(description, name)     \
  enum class name { maybe, yes, nothrow };                           \
                                                                     \
  constexpr const char* PropertyName(name v) { return description; } \
  static_assert(true, "")  // Force a semicolon when using this macro.

ABSL_INTERNAL_INTRINSIC_FUNCTION_ENUM("support for ==", equality_comparable);
ABSL_INTERNAL_INTRINSIC_FUNCTION_ENUM("support for !=", inequality_comparable);
ABSL_INTERNAL_INTRINSIC_FUNCTION_ENUM("support for <", less_than_comparable);
ABSL_INTERNAL_INTRINSIC_FUNCTION_ENUM("support for <=", less_equal_comparable);
ABSL_INTERNAL_INTRINSIC_FUNCTION_ENUM("support for >=",
                                      greater_equal_comparable);
ABSL_INTERNAL_INTRINSIC_FUNCTION_ENUM("support for >", greater_than_comparable);

ABSL_INTERNAL_INTRINSIC_FUNCTION_ENUM("support for swap", swappable);

#undef ABSL_INTERNAL_INTRINSIC_FUNCTION_ENUM

enum class hashable { maybe, yes };

constexpr const char* PropertyName(hashable v) {
  return "support for std::hash";
}

template <class T>
using AlwaysFalse = std::false_type;

#define ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_SPECIAL_MEMBER(name, property)   \
  template <class T>                                                        \
  constexpr property property##_support_of() {                              \
    return std::is_##property<T>::value                                     \
               ? std::is_nothrow_##property<T>::value                       \
                     ? absl::is_trivially_##property<T>::value              \
                           ? property::trivial                              \
                           : property::nothrow                              \
                     : property::yes                                        \
               : property::maybe;                                           \
  }                                                                         \
                                                                            \
  template <class T, class MinProf, class MaxProf>                          \
  void ExpectModelOf##name(ConformanceErrors* errors) {                     \
    (ExpectModelOfImpl)(errors, PropertiesOfT<MinProf>::property##_support, \
                        PropertiesOfT<MaxProf>::property##_support,         \
                        property##_support_of<T>());                        \
  }

ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_SPECIAL_MEMBER(DefaultConstructible,
                                                  default_constructible);

ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_SPECIAL_MEMBER(MoveConstructible,
                                                  move_constructible);

ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_SPECIAL_MEMBER(CopyConstructible,
                                                  copy_constructible);

ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_SPECIAL_MEMBER(MoveAssignable,
                                                  move_assignable);

ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_SPECIAL_MEMBER(CopyAssignable,
                                                  copy_assignable);

ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_SPECIAL_MEMBER(Destructible, destructible);

#undef ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_SPECIAL_MEMBER

void BoolFunction(bool) noexcept;

////////////////////////////////////////////////////////////////////////////////
//
// A metafunction for checking if an operation exists through SFINAE.
//
// `T` is the type to test and Op is an alias containing the expression to test.
template <class T, template <class...> class Op, class = void>
struct IsOpableImpl : std::false_type {};

template <class T, template <class...> class Op>
struct IsOpableImpl<T, Op, absl::void_t<Op<T>>> : std::true_type {};

template <template <class...> class Op>
struct IsOpable {
  template <class T>
  using apply = typename IsOpableImpl<T, Op>::type;
};
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// A metafunction for checking if an operation exists and is also noexcept
// through SFINAE and the noexcept operator.
///
// `T` is the type to test and Op is an alias containing the expression to test.
template <class T, template <class...> class Op, class = void>
struct IsNothrowOpableImpl : std::false_type {};

template <class T, template <class...> class Op>
struct IsNothrowOpableImpl<T, Op, absl::enable_if_t<Op<T>::value>>
    : std::true_type {};

template <template <class...> class Op>
struct IsNothrowOpable {
  template <class T>
  using apply = typename IsNothrowOpableImpl<T, Op>::type;
};
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// A macro that produces the necessary function for reporting what kind of
// support a specific comparison operation has and a function for reporting an
// error if a given type's support for that operation does not meet the expected
// requirements.
#define ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_COMPARISON(name, property, op)      \
  template <class T,                                                           \
            class Result = std::integral_constant<                             \
                bool, noexcept((BoolFunction)(std::declval<const T&>() op      \
                                                  std::declval<const T&>()))>> \
  using name = Result;                                                         \
                                                                               \
  template <class T>                                                           \
  constexpr property property##_support_of() {                                 \
    return IsOpable<name>::apply<T>::value                                     \
               ? IsNothrowOpable<name>::apply<T>::value ? property::nothrow    \
                                                        : property::yes        \
               : property::maybe;                                              \
  }                                                                            \
                                                                               \
  template <class T, class MinProf, class MaxProf>                             \
  void ExpectModelOf##name(ConformanceErrors* errors) {                        \
    (ExpectModelOfImpl)(errors, PropertiesOfT<MinProf>::property##_support,    \
                        PropertiesOfT<MaxProf>::property##_support,            \
                        property##_support_of<T>());                           \
  }
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// Generate the necessary support-checking and error reporting functions for
// each of the comparison operators.
ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_COMPARISON(EqualityComparable,
                                              equality_comparable, ==);

ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_COMPARISON(InequalityComparable,
                                              inequality_comparable, !=);

ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_COMPARISON(LessThanComparable,
                                              less_than_comparable, <);

ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_COMPARISON(LessEqualComparable,
                                              less_equal_comparable, <=);

ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_COMPARISON(GreaterEqualComparable,
                                              greater_equal_comparable, >=);

ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_COMPARISON(GreaterThanComparable,
                                              greater_than_comparable, >);

#undef ABSL_INTERNAL_PESSIMISTIC_MODEL_OF_COMPARISON
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// The necessary support-checking and error-reporting functions for swap.
template <class T>
constexpr swappable swappable_support_of() {
  return type_traits_internal::IsSwappable<T>::value
             ? type_traits_internal::IsNothrowSwappable<T>::value
                   ? swappable::nothrow
                   : swappable::yes
             : swappable::maybe;
}

template <class T, class MinProf, class MaxProf>
void ExpectModelOfSwappable(ConformanceErrors* errors) {
  (ExpectModelOfImpl)(errors, PropertiesOfT<MinProf>::swappable_support,
                      PropertiesOfT<MaxProf>::swappable_support,
                      swappable_support_of<T>());
}
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// The necessary support-checking and error-reporting functions for std::hash.
template <class T>
constexpr hashable hashable_support_of() {
  return type_traits_internal::IsHashable<T>::value ? hashable::yes
                                                    : hashable::maybe;
}

template <class T, class MinProf, class MaxProf>
void ExpectModelOfHashable(ConformanceErrors* errors) {
  (ExpectModelOfImpl)(errors, PropertiesOfT<MinProf>::hashable_support,
                      PropertiesOfT<MaxProf>::hashable_support,
                      hashable_support_of<T>());
}
//
////////////////////////////////////////////////////////////////////////////////

template <
    default_constructible DefaultConstructibleValue =
        default_constructible::maybe,
    move_constructible MoveConstructibleValue = move_constructible::maybe,
    copy_constructible CopyConstructibleValue = copy_constructible::maybe,
    move_assignable MoveAssignableValue = move_assignable::maybe,
    copy_assignable CopyAssignableValue = copy_assignable::maybe,
    destructible DestructibleValue = destructible::maybe,
    equality_comparable EqualityComparableValue = equality_comparable::maybe,
    inequality_comparable InequalityComparableValue =
        inequality_comparable::maybe,
    less_than_comparable LessThanComparableValue = less_than_comparable::maybe,
    less_equal_comparable LessEqualComparableValue =
        less_equal_comparable::maybe,
    greater_equal_comparable GreaterEqualComparableValue =
        greater_equal_comparable::maybe,
    greater_than_comparable GreaterThanComparableValue =
        greater_than_comparable::maybe,
    swappable SwappableValue = swappable::maybe,
    hashable HashableValue = hashable::maybe>
struct ConformanceProfile {
  using properties = ConformanceProfile;

  static constexpr default_constructible
      default_constructible_support =  // NOLINT
      DefaultConstructibleValue;

  static constexpr move_constructible move_constructible_support =  // NOLINT
      MoveConstructibleValue;

  static constexpr copy_constructible copy_constructible_support =  // NOLINT
      CopyConstructibleValue;

  static constexpr move_assignable move_assignable_support =  // NOLINT
      MoveAssignableValue;

  static constexpr copy_assignable copy_assignable_support =  // NOLINT
      CopyAssignableValue;

  static constexpr destructible destructible_support =  // NOLINT
      DestructibleValue;

  static constexpr equality_comparable equality_comparable_support =  // NOLINT
      EqualityComparableValue;

  static constexpr inequality_comparable
      inequality_comparable_support =  // NOLINT
      InequalityComparableValue;

  static constexpr less_than_comparable
      less_than_comparable_support =  // NOLINT
      LessThanComparableValue;

  static constexpr less_equal_comparable
      less_equal_comparable_support =  // NOLINT
      LessEqualComparableValue;

  static constexpr greater_equal_comparable
      greater_equal_comparable_support =  // NOLINT
      GreaterEqualComparableValue;

  static constexpr greater_than_comparable
      greater_than_comparable_support =  // NOLINT
      GreaterThanComparableValue;

  static constexpr swappable swappable_support = SwappableValue;  // NOLINT

  static constexpr hashable hashable_support = HashableValue;  // NOLINT

  static constexpr bool is_default_constructible =  // NOLINT
      DefaultConstructibleValue != default_constructible::maybe;

  static constexpr bool is_move_constructible =  // NOLINT
      MoveConstructibleValue != move_constructible::maybe;

  static constexpr bool is_copy_constructible =  // NOLINT
      CopyConstructibleValue != copy_constructible::maybe;

  static constexpr bool is_move_assignable =  // NOLINT
      MoveAssignableValue != move_assignable::maybe;

  static constexpr bool is_copy_assignable =  // NOLINT
      CopyAssignableValue != copy_assignable::maybe;

  static constexpr bool is_destructible =  // NOLINT
      DestructibleValue != destructible::maybe;

  static constexpr bool is_equality_comparable =  // NOLINT
      EqualityComparableValue != equality_comparable::maybe;

  static constexpr bool is_inequality_comparable =  // NOLINT
      InequalityComparableValue != inequality_comparable::maybe;

  static constexpr bool is_less_than_comparable =  // NOLINT
      LessThanComparableValue != less_than_comparable::maybe;

  static constexpr bool is_less_equal_comparable =  // NOLINT
      LessEqualComparableValue != less_equal_comparable::maybe;

  static constexpr bool is_greater_equal_comparable =  // NOLINT
      GreaterEqualComparableValue != greater_equal_comparable::maybe;

  static constexpr bool is_greater_than_comparable =  // NOLINT
      GreaterThanComparableValue != greater_than_comparable::maybe;

  static constexpr bool is_swappable =  // NOLINT
      SwappableValue != swappable::maybe;

  static constexpr bool is_hashable =  // NOLINT
      HashableValue != hashable::maybe;
};

////////////////////////////////////////////////////////////////////////////////
//
// Compliant SFINAE-friendliness is not always present on the standard library
// implementations that we support. This helper-struct (and associated enum) is
// used as a means to conditionally check the hashability support of a type.
enum class CheckHashability { no, yes };

template <class T, CheckHashability ShouldCheckHashability>
struct conservative_hashable_support_of;

template <class T>
struct conservative_hashable_support_of<T, CheckHashability::no> {
  static constexpr hashable Invoke() { return hashable::maybe; }
};

template <class T>
struct conservative_hashable_support_of<T, CheckHashability::yes> {
  static constexpr hashable Invoke() { return hashable_support_of<T>(); }
};
//
////////////////////////////////////////////////////////////////////////////////

// The ConformanceProfile that is expected based on introspection into the type
// by way of trait checks.
template <class T, CheckHashability ShouldCheckHashability>
struct SyntacticConformanceProfileOf {
  using properties = ConformanceProfile<
      default_constructible_support_of<T>(), move_constructible_support_of<T>(),
      copy_constructible_support_of<T>(), move_assignable_support_of<T>(),
      copy_assignable_support_of<T>(), destructible_support_of<T>(),
      equality_comparable_support_of<T>(),
      inequality_comparable_support_of<T>(),
      less_than_comparable_support_of<T>(),
      less_equal_comparable_support_of<T>(),
      greater_equal_comparable_support_of<T>(),
      greater_than_comparable_support_of<T>(), swappable_support_of<T>(),
      conservative_hashable_support_of<T, ShouldCheckHashability>::Invoke()>;
};

#define ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF_IMPL(type, name)     \
  template <default_constructible DefaultConstructibleValue,                   \
            move_constructible MoveConstructibleValue,                         \
            copy_constructible CopyConstructibleValue,                         \
            move_assignable MoveAssignableValue,                               \
            copy_assignable CopyAssignableValue,                               \
            destructible DestructibleValue,                                    \
            equality_comparable EqualityComparableValue,                       \
            inequality_comparable InequalityComparableValue,                   \
            less_than_comparable LessThanComparableValue,                      \
            less_equal_comparable LessEqualComparableValue,                    \
            greater_equal_comparable GreaterEqualComparableValue,              \
            greater_than_comparable GreaterThanComparableValue,                \
            swappable SwappableValue, hashable HashableValue>                  \
  constexpr type ConformanceProfile<                                           \
      DefaultConstructibleValue, MoveConstructibleValue,                       \
      CopyConstructibleValue, MoveAssignableValue, CopyAssignableValue,        \
      DestructibleValue, EqualityComparableValue, InequalityComparableValue,   \
      LessThanComparableValue, LessEqualComparableValue,                       \
      GreaterEqualComparableValue, GreaterThanComparableValue, SwappableValue, \
      HashableValue>::name

#define ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(type)           \
  ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF_IMPL(type,            \
                                                         type##_support); \
  ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF_IMPL(bool, is_##type)

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(default_constructible);
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(move_constructible);
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(copy_constructible);
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(move_assignable);
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(copy_assignable);
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(destructible);
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(equality_comparable);
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(inequality_comparable);
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(less_than_comparable);
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(less_equal_comparable);
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(greater_equal_comparable);
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(greater_than_comparable);
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(swappable);
ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF(hashable);
#endif

#undef ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF
#undef ABSL_INTERNAL_CONFORMANCE_TESTING_DATA_MEMBER_DEF_IMPL

// Retrieve the enum with the minimum underlying value.
// Note: std::min is not constexpr in C++11, which is why this is necessary.
template <class H>
constexpr H MinEnum(H head) {
  return head;
}

template <class H, class N, class... T>
constexpr H MinEnum(H head, N next, T... tail) {
  return (UnderlyingValue)(head) < (UnderlyingValue)(next)
             ? (MinEnum)(head, tail...)
             : (MinEnum)(next, tail...);
}

template <class... Profs>
struct MinimalProfiles {
  static constexpr default_constructible
      default_constructible_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::default_constructible_support...);

  static constexpr move_constructible move_constructible_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::move_constructible_support...);

  static constexpr copy_constructible copy_constructible_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::copy_constructible_support...);

  static constexpr move_assignable move_assignable_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::move_assignable_support...);

  static constexpr copy_assignable copy_assignable_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::copy_assignable_support...);

  static constexpr destructible destructible_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::destructible_support...);

  static constexpr equality_comparable equality_comparable_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::equality_comparable_support...);

  static constexpr inequality_comparable
      inequality_comparable_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::inequality_comparable_support...);

  static constexpr less_than_comparable
      less_than_comparable_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::less_than_comparable_support...);

  static constexpr less_equal_comparable
      less_equal_comparable_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::less_equal_comparable_support...);

  static constexpr greater_equal_comparable
      greater_equal_comparable_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::greater_equal_comparable_support...);

  static constexpr greater_than_comparable
      greater_than_comparable_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::greater_than_comparable_support...);

  static constexpr swappable swappable_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::swappable_support...);

  static constexpr hashable hashable_support =  // NOLINT
      (MinEnum)(PropertiesOfT<Profs>::hashable_support...);

  using properties = ConformanceProfile<
      default_constructible_support, move_constructible_support,
      copy_constructible_support, move_assignable_support,
      copy_assignable_support, destructible_support,
      equality_comparable_support, inequality_comparable_support,
      less_than_comparable_support, less_equal_comparable_support,
      greater_equal_comparable_support, greater_than_comparable_support,
      swappable_support, hashable_support>;
};

// Retrieve the enum with the greatest underlying value.
// Note: std::max is not constexpr in C++11, which is why this is necessary.
template <class H>
constexpr H MaxEnum(H head) {
  return head;
}

template <class H, class N, class... T>
constexpr H MaxEnum(H head, N next, T... tail) {
  return (UnderlyingValue)(next) < (UnderlyingValue)(head)
             ? (MaxEnum)(head, tail...)
             : (MaxEnum)(next, tail...);
}

template <class... Profs>
struct CombineProfilesImpl {
  static constexpr default_constructible
      default_constructible_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::default_constructible_support...);

  static constexpr move_constructible move_constructible_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::move_constructible_support...);

  static constexpr copy_constructible copy_constructible_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::copy_constructible_support...);

  static constexpr move_assignable move_assignable_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::move_assignable_support...);

  static constexpr copy_assignable copy_assignable_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::copy_assignable_support...);

  static constexpr destructible destructible_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::destructible_support...);

  static constexpr equality_comparable equality_comparable_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::equality_comparable_support...);

  static constexpr inequality_comparable
      inequality_comparable_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::inequality_comparable_support...);

  static constexpr less_than_comparable
      less_than_comparable_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::less_than_comparable_support...);

  static constexpr less_equal_comparable
      less_equal_comparable_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::less_equal_comparable_support...);

  static constexpr greater_equal_comparable
      greater_equal_comparable_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::greater_equal_comparable_support...);

  static constexpr greater_than_comparable
      greater_than_comparable_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::greater_than_comparable_support...);

  static constexpr swappable swappable_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::swappable_support...);

  static constexpr hashable hashable_support =  // NOLINT
      (MaxEnum)(PropertiesOfT<Profs>::hashable_support...);

  using properties = ConformanceProfile<
      default_constructible_support, move_constructible_support,
      copy_constructible_support, move_assignable_support,
      copy_assignable_support, destructible_support,
      equality_comparable_support, inequality_comparable_support,
      less_than_comparable_support, less_equal_comparable_support,
      greater_equal_comparable_support, greater_than_comparable_support,
      swappable_support, hashable_support>;
};

// NOTE: We use this as opposed to a direct alias of CombineProfilesImpl so that
// when named aliases of CombineProfiles are created (such as in
// conformance_aliases.h), we only pay for the combination algorithm on the
// profiles that are actually used.
template <class... Profs>
struct CombineProfiles {
  using profile_alias_of = CombineProfilesImpl<Profs...>;
};

template <>
struct CombineProfiles<> {
  using properties = ConformanceProfile<>;
};

template <class Profile, class Tag>
struct StrongProfileTypedef {
  using properties = PropertiesOfT<Profile>;
};

template <class T, class /*Enabler*/ = void>
struct IsProfileImpl : std::false_type {};

template <class T>
struct IsProfileImpl<T, absl::void_t<PropertiesOfT<T>>> : std::true_type {};

template <class T>
struct IsProfile : IsProfileImpl<T>::type {};

// A tag that describes which set of properties we will check when the user
// requires a strict match in conformance (as opposed to a loose match which
// allows more-refined support of any given operation).
//
// Currently only the RegularityDomain exists and it includes all operations
// that the conformance testing suite knows about. The intent is that if the
// suite is expanded to support extension, such as for checking conformance of
// concepts like Iterators or Containers, additional corresponding domains can
// be created.
struct RegularityDomain {};

}  // namespace types_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TYPES_INTERNAL_CONFORMANCE_PROFILE_H_
