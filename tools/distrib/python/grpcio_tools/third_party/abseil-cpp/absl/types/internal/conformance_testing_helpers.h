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

#ifndef ABSL_TYPES_INTERNAL_CONFORMANCE_TESTING_HELPERS_H_
#define ABSL_TYPES_INTERNAL_CONFORMANCE_TESTING_HELPERS_H_

// Checks to determine whether or not we can use abi::__cxa_demangle
#if (defined(__ANDROID__) || defined(ANDROID)) && !defined(OS_ANDROID)
#define ABSL_INTERNAL_OS_ANDROID
#endif

// We support certain compilers only.  See demangle.h for details.
#if defined(OS_ANDROID) && (defined(__i386__) || defined(__x86_64__))
#define ABSL_TYPES_INTERNAL_HAS_CXA_DEMANGLE 0
#elif (__GNUC__ >= 4 || (__GNUC__ >= 3 && __GNUC_MINOR__ >= 4)) && \
    !defined(__mips__)
#define ABSL_TYPES_INTERNAL_HAS_CXA_DEMANGLE 1
#elif defined(__clang__) && !defined(_MSC_VER)
#define ABSL_TYPES_INTERNAL_HAS_CXA_DEMANGLE 1
#else
#define ABSL_TYPES_INTERNAL_HAS_CXA_DEMANGLE 0
#endif

#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/meta/type_traits.h"
#include "absl/strings/string_view.h"
#include "absl/utility/utility.h"

#if ABSL_TYPES_INTERNAL_HAS_CXA_DEMANGLE
#include <cxxabi.h>

#include <cstdlib>
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace types_internal {

// Return a readable name for type T.
template <class T>
absl::string_view NameOfImpl() {
// TODO(calabrese) Investigate using debugging:internal_demangle as a fallback.
#if ABSL_TYPES_INTERNAL_HAS_CXA_DEMANGLE
  int status = 0;
  char* demangled_name = nullptr;

  demangled_name =
      abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status);

  if (status == 0 && demangled_name != nullptr) {
    return demangled_name;
  } else {
    return typeid(T).name();
  }
#else
  return typeid(T).name();
#endif
  // NOTE: We intentionally leak demangled_name so that it remains valid
  // throughout the remainder of the program.
}

// Given a type, returns as nice of a type name as we can produce (demangled).
//
// Note: This currently strips cv-qualifiers and references, but that is okay
// because we only use this internally with unqualified object types.
template <class T>
std::string NameOf() {
  static const absl::string_view result = NameOfImpl<T>();
  return std::string(result);
}

////////////////////////////////////////////////////////////////////////////////
//
// Metafunction to check if a type is callable with no explicit arguments
template <class Fun, class /*Enabler*/ = void>
struct IsNullaryCallableImpl : std::false_type {};

template <class Fun>
struct IsNullaryCallableImpl<
    Fun, absl::void_t<decltype(std::declval<const Fun&>()())>>
    : std::true_type {
  using result_type = decltype(std::declval<const Fun&>()());

  template <class ValueType>
  using for_type = std::is_same<ValueType, result_type>;

  using void_if_true = void;
};

template <class Fun>
struct IsNullaryCallable : IsNullaryCallableImpl<Fun> {};
//
////////////////////////////////////////////////////////////////////////////////

// A type that contains a function object that returns an instance of a type
// that is undergoing conformance testing. This function is required to always
// return the same value upon invocation.
template <class Fun>
struct GeneratorType;

// A type that contains a tuple of GeneratorType<Fun> where each Fun has the
// same return type. The result of each of the different generators should all
// be equal values, though the underlying object representation may differ (such
// as if one returns 0.0 and another return -0.0, or if one returns an empty
// vector and another returns an empty vector with a different capacity.
template <class... Funs>
struct EquivalenceClassType;

////////////////////////////////////////////////////////////////////////////////
//
// A metafunction to check if a type is a specialization of EquivalenceClassType
template <class T>
struct IsEquivalenceClass : std::false_type {};

template <>
struct IsEquivalenceClass<EquivalenceClassType<>> : std::true_type {
  using self = IsEquivalenceClass;

  // A metafunction to check if this EquivalenceClassType is a valid
  // EquivalenceClassType for a type `ValueType` that is undergoing testing
  template <class ValueType>
  using for_type = std::true_type;
};

template <class Head, class... Tail>
struct IsEquivalenceClass<EquivalenceClassType<Head, Tail...>>
    : std::true_type {
  using self = IsEquivalenceClass;

  // The type undergoing conformance testing that this EquivalenceClass
  // corresponds to
  using result_type = typename IsNullaryCallable<Head>::result_type;

  // A metafunction to check if this EquivalenceClassType is a valid
  // EquivalenceClassType for a type `ValueType` that is undergoing testing
  template <class ValueType>
  using for_type = std::is_same<ValueType, result_type>;
};
//
////////////////////////////////////////////////////////////////////////////////

// A type that contains an ordered series of EquivalenceClassTypes, where the
// the function object of each underlying GeneratorType has the same return type
//
// These equivalence classes are required to be in a logical ascending order
// that is consistent with comparison operators that are defined for the return
// type of each GeneratorType, if any.
template <class... EqClasses>
struct OrderedEquivalenceClasses;

////////////////////////////////////////////////////////////////////////////////
//
// A metafunction to determine the return type of the function object contained
// in a GeneratorType specialization.
template <class T>
struct ResultOfGenerator {};

template <class Fun>
struct ResultOfGenerator<GeneratorType<Fun>> {
  using type = decltype(std::declval<const Fun&>()());
};

template <class Fun>
using ResultOfGeneratorT = typename ResultOfGenerator<GeneratorType<Fun>>::type;
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// A metafunction that yields true iff each of Funs is a GeneratorType
// specialization and they all contain functions with the same return type
template <class /*Enabler*/, class... Funs>
struct AreGeneratorsWithTheSameReturnTypeImpl : std::false_type {};

template <>
struct AreGeneratorsWithTheSameReturnTypeImpl<void> : std::true_type {};

template <class Head, class... Tail>
struct AreGeneratorsWithTheSameReturnTypeImpl<
    typename std::enable_if<absl::conjunction<std::is_same<
        ResultOfGeneratorT<Head>, ResultOfGeneratorT<Tail>>...>::value>::type,
    Head, Tail...> : std::true_type {};

template <class... Funs>
struct AreGeneratorsWithTheSameReturnType
    : AreGeneratorsWithTheSameReturnTypeImpl<void, Funs...>::type {};
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// A metafunction that yields true iff each of Funs is an EquivalenceClassType
// specialization and they all contain GeneratorType specializations that have
// the same return type
template <class... EqClasses>
struct AreEquivalenceClassesOfTheSameType {
  static_assert(sizeof...(EqClasses) != sizeof...(EqClasses), "");
};

template <>
struct AreEquivalenceClassesOfTheSameType<> : std::true_type {
  using self = AreEquivalenceClassesOfTheSameType;

  // Metafunction to check that a type is the same as all of the equivalence
  // classes, if any.
  // Note: In this specialization there are no equivalence classes, so the
  // value type is always compatible.
  template <class /*ValueType*/>
  using for_type = std::true_type;
};

template <class... Funs>
struct AreEquivalenceClassesOfTheSameType<EquivalenceClassType<Funs...>>
    : std::true_type {
  using self = AreEquivalenceClassesOfTheSameType;

  // Metafunction to check that a type is the same as all of the equivalence
  // classes, if any.
  template <class ValueType>
  using for_type = typename IsEquivalenceClass<
      EquivalenceClassType<Funs...>>::template for_type<ValueType>;
};

template <class... TailEqClasses>
struct AreEquivalenceClassesOfTheSameType<
    EquivalenceClassType<>, EquivalenceClassType<>, TailEqClasses...>
    : AreEquivalenceClassesOfTheSameType<TailEqClasses...>::self {};

template <class HeadNextFun, class... TailNextFuns, class... TailEqClasses>
struct AreEquivalenceClassesOfTheSameType<
    EquivalenceClassType<>, EquivalenceClassType<HeadNextFun, TailNextFuns...>,
    TailEqClasses...>
    : AreEquivalenceClassesOfTheSameType<
          EquivalenceClassType<HeadNextFun, TailNextFuns...>,
          TailEqClasses...>::self {};

template <class HeadHeadFun, class... TailHeadFuns, class... TailEqClasses>
struct AreEquivalenceClassesOfTheSameType<
    EquivalenceClassType<HeadHeadFun, TailHeadFuns...>, EquivalenceClassType<>,
    TailEqClasses...>
    : AreEquivalenceClassesOfTheSameType<
          EquivalenceClassType<HeadHeadFun, TailHeadFuns...>,
          TailEqClasses...>::self {};

template <class HeadHeadFun, class... TailHeadFuns, class HeadNextFun,
          class... TailNextFuns, class... TailEqClasses>
struct AreEquivalenceClassesOfTheSameType<
    EquivalenceClassType<HeadHeadFun, TailHeadFuns...>,
    EquivalenceClassType<HeadNextFun, TailNextFuns...>, TailEqClasses...>
    : absl::conditional_t<
          IsNullaryCallable<HeadNextFun>::template for_type<
              typename IsNullaryCallable<HeadHeadFun>::result_type>::value,
          AreEquivalenceClassesOfTheSameType<
              EquivalenceClassType<HeadHeadFun, TailHeadFuns...>,
              TailEqClasses...>,
          std::false_type> {};
//
////////////////////////////////////////////////////////////////////////////////

// Execute a function for each passed-in parameter.
template <class Fun, class... Cases>
void ForEachParameter(const Fun& fun, const Cases&... cases) {
  const std::initializer_list<bool> results = {
      (static_cast<void>(fun(cases)), true)...};

  (void)results;
}

// Execute a function on each passed-in parameter (using a bound function).
template <class Fun>
struct ForEachParameterFun {
  template <class... T>
  void operator()(const T&... cases) const {
    (ForEachParameter)(fun, cases...);
  }

  Fun fun;
};

// Execute a function on each element of a tuple.
template <class Fun, class Tup>
void ForEachTupleElement(const Fun& fun, const Tup& tup) {
  absl::apply(ForEachParameterFun<Fun>{fun}, tup);
}

////////////////////////////////////////////////////////////////////////////////
//
// Execute a function for each combination of two elements of a tuple, including
// combinations of an element with itself.
template <class Fun, class... T>
struct ForEveryTwoImpl {
  template <class Lhs>
  struct WithBoundLhs {
    template <class Rhs>
    void operator()(const Rhs& rhs) const {
      fun(lhs, rhs);
    }

    Fun fun;
    Lhs lhs;
  };

  template <class Lhs>
  void operator()(const Lhs& lhs) const {
    (ForEachTupleElement)(WithBoundLhs<Lhs>{fun, lhs}, args);
  }

  Fun fun;
  std::tuple<T...> args;
};

template <class Fun, class... T>
void ForEveryTwo(const Fun& fun, std::tuple<T...> args) {
  (ForEachTupleElement)(ForEveryTwoImpl<Fun, T...>{fun, args}, args);
}
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// Insert all values into an associative container
template<class Container>
void InsertEach(Container* cont) {
}

template<class Container, class H, class... T>
void InsertEach(Container* cont, H&& head, T&&... tail) {
  cont->insert(head);
  (InsertEach)(cont, tail...);
}
//
////////////////////////////////////////////////////////////////////////////////
// A template with a nested "Invoke" static-member-function that executes a
// passed-in Callable when `Condition` is true, otherwise it ignores the
// Callable. This is useful for executing a function object with a condition
// that corresponds to whether or not the Callable can be safely instantiated.
// It has some overlapping uses with C++17 `if constexpr`.
template <bool Condition>
struct If;

template <>
struct If</*Condition =*/false> {
  template <class Fun, class... P>
  static void Invoke(const Fun& /*fun*/, P&&... /*args*/) {}
};

template <>
struct If</*Condition =*/true> {
  template <class Fun, class... P>
  static void Invoke(const Fun& fun, P&&... args) {
    // TODO(calabrese) Use std::invoke equivalent instead of function-call.
    fun(absl::forward<P>(args)...);
  }
};

//
// ABSL_INTERNAL_STRINGIZE(...)
//
// This variadic macro transforms its arguments into a c-string literal after
// expansion.
//
// Example:
//
//   ABSL_INTERNAL_STRINGIZE(std::array<int, 10>)
//
// Results in:
//
//   "std::array<int, 10>"
#define ABSL_INTERNAL_STRINGIZE(...) ABSL_INTERNAL_STRINGIZE_IMPL((__VA_ARGS__))
#define ABSL_INTERNAL_STRINGIZE_IMPL(arg) ABSL_INTERNAL_STRINGIZE_IMPL2 arg
#define ABSL_INTERNAL_STRINGIZE_IMPL2(...) #__VA_ARGS__

}  // namespace types_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TYPES_INTERNAL_CONFORMANCE_TESTING_HELPERS_H_
