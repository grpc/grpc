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
// conformance_archetype.h
// -----------------------------------------------------------------------------
//
// This file contains a facility for generating "archetypes" of out of
// "Conformance Profiles" (see "conformance_profiles.h" for more information
// about Conformance Profiles). An archetype is a type that aims to support the
// bare minimum requirements of a given Conformance Profile. For instance, an
// archetype that corresponds to an ImmutableProfile has exactly a nothrow
// move-constructor, a potentially-throwing copy constructor, a nothrow
// destructor, with all other special-member-functions deleted. These archetypes
// are useful for testing to make sure that templates are able to work with the
// kinds of types that they claim to support (i.e. that they do not accidentally
// under-constrain),
//
// The main type template in this file is the Archetype template, which takes
// a Conformance Profile as a template argument and its instantiations are a
// minimum-conforming model of that profile.

#ifndef ABSL_TYPES_INTERNAL_CONFORMANCE_ARCHETYPE_H_
#define ABSL_TYPES_INTERNAL_CONFORMANCE_ARCHETYPE_H_

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

#include "absl/meta/type_traits.h"
#include "absl/types/internal/conformance_profile.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace types_internal {

// A minimum-conforming implementation of a type with properties specified in
// `Prof`, where `Prof` is a valid Conformance Profile.
template <class Prof, class /*Enabler*/ = void>
class Archetype;

// Given an Archetype, obtain the properties of the profile associated with that
// archetype.
template <class Archetype>
struct PropertiesOfArchetype;

template <class Prof>
struct PropertiesOfArchetype<Archetype<Prof>> {
  using type = PropertiesOfT<Prof>;
};

template <class Archetype>
using PropertiesOfArchetypeT = typename PropertiesOfArchetype<Archetype>::type;

// A metafunction to determine if a type is an `Archetype`.
template <class T>
struct IsArchetype : std::false_type {};

template <class Prof>
struct IsArchetype<Archetype<Prof>> : std::true_type {};

// A constructor tag type used when creating an Archetype with internal state.
struct MakeArchetypeState {};

// Data stored within an archetype that is copied/compared/hashed when the
// corresponding operations are used.
using ArchetypeState = std::size_t;

////////////////////////////////////////////////////////////////////////////////
//   This section of the file defines a chain of base classes for Archetype,  //
//   where each base defines a specific special member function with the      //
//   appropriate properties (deleted, noexcept(false), noexcept, or trivial). //
////////////////////////////////////////////////////////////////////////////////

// The bottom-most base, which contains the state and the default constructor.
template <default_constructible DefaultConstructibleValue>
struct ArchetypeStateBase {
  static_assert(DefaultConstructibleValue == default_constructible::yes ||
                    DefaultConstructibleValue == default_constructible::nothrow,
                "");

  ArchetypeStateBase() noexcept(
      DefaultConstructibleValue ==
      default_constructible::
          nothrow) /*Vacuous archetype_state initialization*/ {}
  explicit ArchetypeStateBase(MakeArchetypeState, ArchetypeState state) noexcept
      : archetype_state(state) {}

  ArchetypeState archetype_state;
};

template <>
struct ArchetypeStateBase<default_constructible::maybe> {
  explicit ArchetypeStateBase() = delete;
  explicit ArchetypeStateBase(MakeArchetypeState, ArchetypeState state) noexcept
      : archetype_state(state) {}

  ArchetypeState archetype_state;
};

template <>
struct ArchetypeStateBase<default_constructible::trivial> {
  ArchetypeStateBase() = default;
  explicit ArchetypeStateBase(MakeArchetypeState, ArchetypeState state) noexcept
      : archetype_state(state) {}

  ArchetypeState archetype_state;
};

// The move-constructor base
template <default_constructible DefaultConstructibleValue,
          move_constructible MoveConstructibleValue>
struct ArchetypeMoveConstructor
    : ArchetypeStateBase<DefaultConstructibleValue> {
  static_assert(MoveConstructibleValue == move_constructible::yes ||
                    MoveConstructibleValue == move_constructible::nothrow,
                "");

  explicit ArchetypeMoveConstructor(MakeArchetypeState,
                                    ArchetypeState state) noexcept
      : ArchetypeStateBase<DefaultConstructibleValue>(MakeArchetypeState(),
                                                      state) {}

  ArchetypeMoveConstructor() = default;
  ArchetypeMoveConstructor(ArchetypeMoveConstructor&& other) noexcept(
      MoveConstructibleValue == move_constructible::nothrow)
      : ArchetypeStateBase<DefaultConstructibleValue>(MakeArchetypeState(),
                                                      other.archetype_state) {}
  ArchetypeMoveConstructor(const ArchetypeMoveConstructor&) = default;
  ArchetypeMoveConstructor& operator=(ArchetypeMoveConstructor&&) = default;
  ArchetypeMoveConstructor& operator=(const ArchetypeMoveConstructor&) =
      default;
};

template <default_constructible DefaultConstructibleValue>
struct ArchetypeMoveConstructor<DefaultConstructibleValue,
                                move_constructible::trivial>
    : ArchetypeStateBase<DefaultConstructibleValue> {
  explicit ArchetypeMoveConstructor(MakeArchetypeState,
                                    ArchetypeState state) noexcept
      : ArchetypeStateBase<DefaultConstructibleValue>(MakeArchetypeState(),
                                                      state) {}

  ArchetypeMoveConstructor() = default;
};

// The copy-constructor base
template <default_constructible DefaultConstructibleValue,
          move_constructible MoveConstructibleValue,
          copy_constructible CopyConstructibleValue>
struct ArchetypeCopyConstructor
    : ArchetypeMoveConstructor<DefaultConstructibleValue,
                               MoveConstructibleValue> {
  static_assert(CopyConstructibleValue == copy_constructible::yes ||
                    CopyConstructibleValue == copy_constructible::nothrow,
                "");
  explicit ArchetypeCopyConstructor(MakeArchetypeState,
                                    ArchetypeState state) noexcept
      : ArchetypeMoveConstructor<DefaultConstructibleValue,
                                 MoveConstructibleValue>(MakeArchetypeState(),
                                                         state) {}

  ArchetypeCopyConstructor() = default;
  ArchetypeCopyConstructor(ArchetypeCopyConstructor&&) = default;
  ArchetypeCopyConstructor(const ArchetypeCopyConstructor& other) noexcept(
      CopyConstructibleValue == copy_constructible::nothrow)
      : ArchetypeMoveConstructor<DefaultConstructibleValue,
                                 MoveConstructibleValue>(
            MakeArchetypeState(), other.archetype_state) {}
  ArchetypeCopyConstructor& operator=(ArchetypeCopyConstructor&&) = default;
  ArchetypeCopyConstructor& operator=(const ArchetypeCopyConstructor&) =
      default;
};

template <default_constructible DefaultConstructibleValue,
          move_constructible MoveConstructibleValue>
struct ArchetypeCopyConstructor<DefaultConstructibleValue,
                                MoveConstructibleValue,
                                copy_constructible::maybe>
    : ArchetypeMoveConstructor<DefaultConstructibleValue,
                               MoveConstructibleValue> {
  explicit ArchetypeCopyConstructor(MakeArchetypeState,
                                    ArchetypeState state) noexcept
      : ArchetypeMoveConstructor<DefaultConstructibleValue,
                                 MoveConstructibleValue>(MakeArchetypeState(),
                                                         state) {}

  ArchetypeCopyConstructor() = default;
  ArchetypeCopyConstructor(ArchetypeCopyConstructor&&) = default;
  ArchetypeCopyConstructor(const ArchetypeCopyConstructor&) = delete;
  ArchetypeCopyConstructor& operator=(ArchetypeCopyConstructor&&) = default;
  ArchetypeCopyConstructor& operator=(const ArchetypeCopyConstructor&) =
      default;
};

template <default_constructible DefaultConstructibleValue,
          move_constructible MoveConstructibleValue>
struct ArchetypeCopyConstructor<DefaultConstructibleValue,
                                MoveConstructibleValue,
                                copy_constructible::trivial>
    : ArchetypeMoveConstructor<DefaultConstructibleValue,
                               MoveConstructibleValue> {
  explicit ArchetypeCopyConstructor(MakeArchetypeState,
                                    ArchetypeState state) noexcept
      : ArchetypeMoveConstructor<DefaultConstructibleValue,
                                 MoveConstructibleValue>(MakeArchetypeState(),
                                                         state) {}

  ArchetypeCopyConstructor() = default;
};

// The move-assign base
template <default_constructible DefaultConstructibleValue,
          move_constructible MoveConstructibleValue,
          copy_constructible CopyConstructibleValue,
          move_assignable MoveAssignableValue>
struct ArchetypeMoveAssign
    : ArchetypeCopyConstructor<DefaultConstructibleValue,
                               MoveConstructibleValue, CopyConstructibleValue> {
  static_assert(MoveAssignableValue == move_assignable::yes ||
                    MoveAssignableValue == move_assignable::nothrow,
                "");
  explicit ArchetypeMoveAssign(MakeArchetypeState,
                               ArchetypeState state) noexcept
      : ArchetypeCopyConstructor<DefaultConstructibleValue,
                                 MoveConstructibleValue,
                                 CopyConstructibleValue>(MakeArchetypeState(),
                                                         state) {}

  ArchetypeMoveAssign() = default;
  ArchetypeMoveAssign(ArchetypeMoveAssign&&) = default;
  ArchetypeMoveAssign(const ArchetypeMoveAssign&) = default;
  ArchetypeMoveAssign& operator=(ArchetypeMoveAssign&& other) noexcept(
      MoveAssignableValue == move_assignable::nothrow) {
    this->archetype_state = other.archetype_state;
    return *this;
  }

  ArchetypeMoveAssign& operator=(const ArchetypeMoveAssign&) = default;
};

template <default_constructible DefaultConstructibleValue,
          move_constructible MoveConstructibleValue,
          copy_constructible CopyConstructibleValue>
struct ArchetypeMoveAssign<DefaultConstructibleValue, MoveConstructibleValue,
                           CopyConstructibleValue, move_assignable::trivial>
    : ArchetypeCopyConstructor<DefaultConstructibleValue,
                               MoveConstructibleValue, CopyConstructibleValue> {
  explicit ArchetypeMoveAssign(MakeArchetypeState,
                               ArchetypeState state) noexcept
      : ArchetypeCopyConstructor<DefaultConstructibleValue,
                                 MoveConstructibleValue,
                                 CopyConstructibleValue>(MakeArchetypeState(),
                                                         state) {}

  ArchetypeMoveAssign() = default;
};

// The copy-assign base
template <default_constructible DefaultConstructibleValue,
          move_constructible MoveConstructibleValue,
          copy_constructible CopyConstructibleValue,
          move_assignable MoveAssignableValue,
          copy_assignable CopyAssignableValue>
struct ArchetypeCopyAssign
    : ArchetypeMoveAssign<DefaultConstructibleValue, MoveConstructibleValue,
                          CopyConstructibleValue, MoveAssignableValue> {
  static_assert(CopyAssignableValue == copy_assignable::yes ||
                    CopyAssignableValue == copy_assignable::nothrow,
                "");
  explicit ArchetypeCopyAssign(MakeArchetypeState,
                               ArchetypeState state) noexcept
      : ArchetypeMoveAssign<DefaultConstructibleValue, MoveConstructibleValue,
                            CopyConstructibleValue, MoveAssignableValue>(
            MakeArchetypeState(), state) {}

  ArchetypeCopyAssign() = default;
  ArchetypeCopyAssign(ArchetypeCopyAssign&&) = default;
  ArchetypeCopyAssign(const ArchetypeCopyAssign&) = default;
  ArchetypeCopyAssign& operator=(ArchetypeCopyAssign&&) = default;

  ArchetypeCopyAssign& operator=(const ArchetypeCopyAssign& other) noexcept(
      CopyAssignableValue == copy_assignable::nothrow) {
    this->archetype_state = other.archetype_state;
    return *this;
  }
};

template <default_constructible DefaultConstructibleValue,
          move_constructible MoveConstructibleValue,
          copy_constructible CopyConstructibleValue,
          move_assignable MoveAssignableValue>
struct ArchetypeCopyAssign<DefaultConstructibleValue, MoveConstructibleValue,
                           CopyConstructibleValue, MoveAssignableValue,
                           copy_assignable::maybe>
    : ArchetypeMoveAssign<DefaultConstructibleValue, MoveConstructibleValue,
                          CopyConstructibleValue, MoveAssignableValue> {
  explicit ArchetypeCopyAssign(MakeArchetypeState,
                               ArchetypeState state) noexcept
      : ArchetypeMoveAssign<DefaultConstructibleValue, MoveConstructibleValue,
                            CopyConstructibleValue, MoveAssignableValue>(
            MakeArchetypeState(), state) {}

  ArchetypeCopyAssign() = default;
  ArchetypeCopyAssign(ArchetypeCopyAssign&&) = default;
  ArchetypeCopyAssign(const ArchetypeCopyAssign&) = default;
  ArchetypeCopyAssign& operator=(ArchetypeCopyAssign&&) = default;
  ArchetypeCopyAssign& operator=(const ArchetypeCopyAssign&) = delete;
};

template <default_constructible DefaultConstructibleValue,
          move_constructible MoveConstructibleValue,
          copy_constructible CopyConstructibleValue,
          move_assignable MoveAssignableValue>
struct ArchetypeCopyAssign<DefaultConstructibleValue, MoveConstructibleValue,
                           CopyConstructibleValue, MoveAssignableValue,
                           copy_assignable::trivial>
    : ArchetypeMoveAssign<DefaultConstructibleValue, MoveConstructibleValue,
                          CopyConstructibleValue, MoveAssignableValue> {
  explicit ArchetypeCopyAssign(MakeArchetypeState,
                               ArchetypeState state) noexcept
      : ArchetypeMoveAssign<DefaultConstructibleValue, MoveConstructibleValue,
                            CopyConstructibleValue, MoveAssignableValue>(
            MakeArchetypeState(), state) {}

  ArchetypeCopyAssign() = default;
};

// The destructor base
template <default_constructible DefaultConstructibleValue,
          move_constructible MoveConstructibleValue,
          copy_constructible CopyConstructibleValue,
          move_assignable MoveAssignableValue,
          copy_assignable CopyAssignableValue, destructible DestructibleValue>
struct ArchetypeDestructor
    : ArchetypeCopyAssign<DefaultConstructibleValue, MoveConstructibleValue,
                          CopyConstructibleValue, MoveAssignableValue,
                          CopyAssignableValue> {
  static_assert(DestructibleValue == destructible::yes ||
                    DestructibleValue == destructible::nothrow,
                "");

  explicit ArchetypeDestructor(MakeArchetypeState,
                               ArchetypeState state) noexcept
      : ArchetypeCopyAssign<DefaultConstructibleValue, MoveConstructibleValue,
                            CopyConstructibleValue, MoveAssignableValue,
                            CopyAssignableValue>(MakeArchetypeState(), state) {}

  ArchetypeDestructor() = default;
  ArchetypeDestructor(ArchetypeDestructor&&) = default;
  ArchetypeDestructor(const ArchetypeDestructor&) = default;
  ArchetypeDestructor& operator=(ArchetypeDestructor&&) = default;
  ArchetypeDestructor& operator=(const ArchetypeDestructor&) = default;
  ~ArchetypeDestructor() noexcept(DestructibleValue == destructible::nothrow) {}
};

template <default_constructible DefaultConstructibleValue,
          move_constructible MoveConstructibleValue,
          copy_constructible CopyConstructibleValue,
          move_assignable MoveAssignableValue,
          copy_assignable CopyAssignableValue>
struct ArchetypeDestructor<DefaultConstructibleValue, MoveConstructibleValue,
                           CopyConstructibleValue, MoveAssignableValue,
                           CopyAssignableValue, destructible::trivial>
    : ArchetypeCopyAssign<DefaultConstructibleValue, MoveConstructibleValue,
                          CopyConstructibleValue, MoveAssignableValue,
                          CopyAssignableValue> {
  explicit ArchetypeDestructor(MakeArchetypeState,
                               ArchetypeState state) noexcept
      : ArchetypeCopyAssign<DefaultConstructibleValue, MoveConstructibleValue,
                            CopyConstructibleValue, MoveAssignableValue,
                            CopyAssignableValue>(MakeArchetypeState(), state) {}

  ArchetypeDestructor() = default;
};

// An alias to the top of the chain of bases for special-member functions.
// NOTE: move_constructible::maybe, move_assignable::maybe, and
// destructible::maybe are handled in the top-level type by way of SFINAE.
// Because of this, we never instantiate the base classes with
// move_constructible::maybe, move_assignable::maybe, or destructible::maybe so
// that we minimize the number of different possible type-template
// instantiations.
template <default_constructible DefaultConstructibleValue,
          move_constructible MoveConstructibleValue,
          copy_constructible CopyConstructibleValue,
          move_assignable MoveAssignableValue,
          copy_assignable CopyAssignableValue, destructible DestructibleValue>
using ArchetypeSpecialMembersBase = ArchetypeDestructor<
    DefaultConstructibleValue,
    MoveConstructibleValue != move_constructible::maybe
        ? MoveConstructibleValue
        : move_constructible::nothrow,
    CopyConstructibleValue,
    MoveAssignableValue != move_assignable::maybe ? MoveAssignableValue
                                                  : move_assignable::nothrow,
    CopyAssignableValue,
    DestructibleValue != destructible::maybe ? DestructibleValue
                                             : destructible::nothrow>;

// A function that is used to create an archetype with some associated state.
template <class Arch>
Arch MakeArchetype(ArchetypeState state) noexcept {
  static_assert(IsArchetype<Arch>::value,
                "The explicit template argument to MakeArchetype is required "
                "to be an Archetype.");
  return Arch(MakeArchetypeState(), state);
}

// This is used to conditionally delete "copy" and "move" constructors in a way
// that is consistent with what the ConformanceProfile requires and that also
// strictly enforces the arguments to the copy/move to not come from implicit
// conversions when dealing with the Archetype.
template <class Prof, class T>
constexpr bool ShouldDeleteConstructor() {
  return !((PropertiesOfT<Prof>::move_constructible_support !=
                move_constructible::maybe &&
            std::is_same<T, Archetype<Prof>>::value) ||
           (PropertiesOfT<Prof>::copy_constructible_support !=
                copy_constructible::maybe &&
            (std::is_same<T, const Archetype<Prof>&>::value ||
             std::is_same<T, Archetype<Prof>&>::value ||
             std::is_same<T, const Archetype<Prof>>::value)));
}

// This is used to conditionally delete "copy" and "move" assigns in a way
// that is consistent with what the ConformanceProfile requires and that also
// strictly enforces the arguments to the copy/move to not come from implicit
// conversions when dealing with the Archetype.
template <class Prof, class T>
constexpr bool ShouldDeleteAssign() {
  return !(
      (PropertiesOfT<Prof>::move_assignable_support != move_assignable::maybe &&
       std::is_same<T, Archetype<Prof>>::value) ||
      (PropertiesOfT<Prof>::copy_assignable_support != copy_assignable::maybe &&
       (std::is_same<T, const Archetype<Prof>&>::value ||
        std::is_same<T, Archetype<Prof>&>::value ||
        std::is_same<T, const Archetype<Prof>>::value)));
}

// TODO(calabrese) Inherit from a chain of secondary bases to pull in the
// associated functions of other concepts.
template <class Prof, class Enabler>
class Archetype : ArchetypeSpecialMembersBase<
                      PropertiesOfT<Prof>::default_constructible_support,
                      PropertiesOfT<Prof>::move_constructible_support,
                      PropertiesOfT<Prof>::copy_constructible_support,
                      PropertiesOfT<Prof>::move_assignable_support,
                      PropertiesOfT<Prof>::copy_assignable_support,
                      PropertiesOfT<Prof>::destructible_support> {
  static_assert(std::is_same<Enabler, void>::value,
                "An explicit type must not be passed as the second template "
                "argument to 'Archetype`.");

  // The cases mentioned in these static_asserts are expected to be handled in
  // the partial template specializations of Archetype that follow this
  // definition.
  static_assert(PropertiesOfT<Prof>::destructible_support !=
                    destructible::maybe,
                "");
  static_assert(PropertiesOfT<Prof>::move_constructible_support !=
                        move_constructible::maybe ||
                    PropertiesOfT<Prof>::copy_constructible_support ==
                        copy_constructible::maybe,
                "");
  static_assert(PropertiesOfT<Prof>::move_assignable_support !=
                        move_assignable::maybe ||
                    PropertiesOfT<Prof>::copy_assignable_support ==
                        copy_assignable::maybe,
                "");

 public:
  Archetype() = default;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteConstructor<Prof, T>()>::type* = nullptr>
  Archetype(T&&) = delete;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteAssign<Prof, T>()>::type* = nullptr>
  Archetype& operator=(T&&) = delete;

  using ArchetypeSpecialMembersBase<
      PropertiesOfT<Prof>::default_constructible_support,
      PropertiesOfT<Prof>::move_constructible_support,
      PropertiesOfT<Prof>::copy_constructible_support,
      PropertiesOfT<Prof>::move_assignable_support,
      PropertiesOfT<Prof>::copy_assignable_support,
      PropertiesOfT<Prof>::destructible_support>::archetype_state;

 private:
  explicit Archetype(MakeArchetypeState, ArchetypeState state) noexcept
      : ArchetypeSpecialMembersBase<
            PropertiesOfT<Prof>::default_constructible_support,
            PropertiesOfT<Prof>::move_constructible_support,
            PropertiesOfT<Prof>::copy_constructible_support,
            PropertiesOfT<Prof>::move_assignable_support,
            PropertiesOfT<Prof>::copy_assignable_support,
            PropertiesOfT<Prof>::destructible_support>(MakeArchetypeState(),
                                                       state) {}

  friend Archetype MakeArchetype<Archetype>(ArchetypeState) noexcept;
};

template <class Prof>
class Archetype<Prof, typename std::enable_if<
                          PropertiesOfT<Prof>::move_constructible_support !=
                              move_constructible::maybe &&
                          PropertiesOfT<Prof>::move_assignable_support ==
                              move_assignable::maybe &&
                          PropertiesOfT<Prof>::destructible_support !=
                              destructible::maybe>::type>
    : ArchetypeSpecialMembersBase<
          PropertiesOfT<Prof>::default_constructible_support,
          PropertiesOfT<Prof>::move_constructible_support,
          PropertiesOfT<Prof>::copy_constructible_support,
          PropertiesOfT<Prof>::move_assignable_support,
          PropertiesOfT<Prof>::copy_assignable_support,
          PropertiesOfT<Prof>::destructible_support> {
 public:
  Archetype() = default;
  Archetype(Archetype&&) = default;
  Archetype(const Archetype&) = default;
  Archetype& operator=(Archetype&&) = delete;
  Archetype& operator=(const Archetype&) = default;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteConstructor<Prof, T>()>::type* = nullptr>
  Archetype(T&&) = delete;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteAssign<Prof, T>()>::type* = nullptr>
  Archetype& operator=(T&&) = delete;

  using ArchetypeSpecialMembersBase<
      PropertiesOfT<Prof>::default_constructible_support,
      PropertiesOfT<Prof>::move_constructible_support,
      PropertiesOfT<Prof>::copy_constructible_support,
      PropertiesOfT<Prof>::move_assignable_support,
      PropertiesOfT<Prof>::copy_assignable_support,
      PropertiesOfT<Prof>::destructible_support>::archetype_state;

 private:
  explicit Archetype(MakeArchetypeState, ArchetypeState state) noexcept
      : ArchetypeSpecialMembersBase<
            PropertiesOfT<Prof>::default_constructible_support,
            PropertiesOfT<Prof>::move_constructible_support,
            PropertiesOfT<Prof>::copy_constructible_support,
            PropertiesOfT<Prof>::move_assignable_support,
            PropertiesOfT<Prof>::copy_assignable_support,
            PropertiesOfT<Prof>::destructible_support>(MakeArchetypeState(),
                                                       state) {}

  friend Archetype MakeArchetype<Archetype>(ArchetypeState) noexcept;
};

template <class Prof>
class Archetype<Prof, typename std::enable_if<
                          PropertiesOfT<Prof>::move_constructible_support ==
                              move_constructible::maybe &&
                          PropertiesOfT<Prof>::move_assignable_support ==
                              move_assignable::maybe &&
                          PropertiesOfT<Prof>::destructible_support !=
                              destructible::maybe>::type>
    : ArchetypeSpecialMembersBase<
          PropertiesOfT<Prof>::default_constructible_support,
          PropertiesOfT<Prof>::move_constructible_support,
          PropertiesOfT<Prof>::copy_constructible_support,
          PropertiesOfT<Prof>::move_assignable_support,
          PropertiesOfT<Prof>::copy_assignable_support,
          PropertiesOfT<Prof>::destructible_support> {
 public:
  Archetype() = default;
  Archetype(Archetype&&) = delete;
  Archetype(const Archetype&) = default;
  Archetype& operator=(Archetype&&) = delete;
  Archetype& operator=(const Archetype&) = default;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteConstructor<Prof, T>()>::type* = nullptr>
  Archetype(T&&) = delete;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteAssign<Prof, T>()>::type* = nullptr>
  Archetype& operator=(T&&) = delete;

  using ArchetypeSpecialMembersBase<
      PropertiesOfT<Prof>::default_constructible_support,
      PropertiesOfT<Prof>::move_constructible_support,
      PropertiesOfT<Prof>::copy_constructible_support,
      PropertiesOfT<Prof>::move_assignable_support,
      PropertiesOfT<Prof>::copy_assignable_support,
      PropertiesOfT<Prof>::destructible_support>::archetype_state;

 private:
  explicit Archetype(MakeArchetypeState, ArchetypeState state) noexcept
      : ArchetypeSpecialMembersBase<
            PropertiesOfT<Prof>::default_constructible_support,
            PropertiesOfT<Prof>::move_constructible_support,
            PropertiesOfT<Prof>::copy_constructible_support,
            PropertiesOfT<Prof>::move_assignable_support,
            PropertiesOfT<Prof>::copy_assignable_support,
            PropertiesOfT<Prof>::destructible_support>(MakeArchetypeState(),
                                                       state) {}

  friend Archetype MakeArchetype<Archetype>(ArchetypeState) noexcept;
};

template <class Prof>
class Archetype<Prof, typename std::enable_if<
                          PropertiesOfT<Prof>::move_constructible_support ==
                              move_constructible::maybe &&
                          PropertiesOfT<Prof>::move_assignable_support !=
                              move_assignable::maybe &&
                          PropertiesOfT<Prof>::destructible_support !=
                              destructible::maybe>::type>
    : ArchetypeSpecialMembersBase<
          PropertiesOfT<Prof>::default_constructible_support,
          PropertiesOfT<Prof>::move_constructible_support,
          PropertiesOfT<Prof>::copy_constructible_support,
          PropertiesOfT<Prof>::move_assignable_support,
          PropertiesOfT<Prof>::copy_assignable_support,
          PropertiesOfT<Prof>::destructible_support> {
 public:
  Archetype() = default;
  Archetype(Archetype&&) = delete;
  Archetype(const Archetype&) = default;
  Archetype& operator=(Archetype&&) = default;
  Archetype& operator=(const Archetype&) = default;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteConstructor<Prof, T>()>::type* = nullptr>
  Archetype(T&&) = delete;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteAssign<Prof, T>()>::type* = nullptr>
  Archetype& operator=(T&&) = delete;

  using ArchetypeSpecialMembersBase<
      PropertiesOfT<Prof>::default_constructible_support,
      PropertiesOfT<Prof>::move_constructible_support,
      PropertiesOfT<Prof>::copy_constructible_support,
      PropertiesOfT<Prof>::move_assignable_support,
      PropertiesOfT<Prof>::copy_assignable_support,
      PropertiesOfT<Prof>::destructible_support>::archetype_state;

 private:
  explicit Archetype(MakeArchetypeState, ArchetypeState state) noexcept
      : ArchetypeSpecialMembersBase<
            PropertiesOfT<Prof>::default_constructible_support,
            PropertiesOfT<Prof>::move_constructible_support,
            PropertiesOfT<Prof>::copy_constructible_support,
            PropertiesOfT<Prof>::move_assignable_support,
            PropertiesOfT<Prof>::copy_assignable_support,
            PropertiesOfT<Prof>::destructible_support>(MakeArchetypeState(),
                                                       state) {}

  friend Archetype MakeArchetype<Archetype>(ArchetypeState) noexcept;
};

template <class Prof>
class Archetype<Prof, typename std::enable_if<
                          PropertiesOfT<Prof>::move_constructible_support !=
                              move_constructible::maybe &&
                          PropertiesOfT<Prof>::move_assignable_support ==
                              move_assignable::maybe &&
                          PropertiesOfT<Prof>::destructible_support ==
                              destructible::maybe>::type>
    : ArchetypeSpecialMembersBase<
          PropertiesOfT<Prof>::default_constructible_support,
          PropertiesOfT<Prof>::move_constructible_support,
          PropertiesOfT<Prof>::copy_constructible_support,
          PropertiesOfT<Prof>::move_assignable_support,
          PropertiesOfT<Prof>::copy_assignable_support,
          PropertiesOfT<Prof>::destructible_support> {
 public:
  Archetype() = default;
  Archetype(Archetype&&) = default;
  Archetype(const Archetype&) = default;
  Archetype& operator=(Archetype&&) = delete;
  Archetype& operator=(const Archetype&) = default;
  ~Archetype() = delete;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteConstructor<Prof, T>()>::type* = nullptr>
  Archetype(T&&) = delete;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteAssign<Prof, T>()>::type* = nullptr>
  Archetype& operator=(T&&) = delete;

  using ArchetypeSpecialMembersBase<
      PropertiesOfT<Prof>::default_constructible_support,
      PropertiesOfT<Prof>::move_constructible_support,
      PropertiesOfT<Prof>::copy_constructible_support,
      PropertiesOfT<Prof>::move_assignable_support,
      PropertiesOfT<Prof>::copy_assignable_support,
      PropertiesOfT<Prof>::destructible_support>::archetype_state;

 private:
  explicit Archetype(MakeArchetypeState, ArchetypeState state) noexcept
      : ArchetypeSpecialMembersBase<
            PropertiesOfT<Prof>::default_constructible_support,
            PropertiesOfT<Prof>::move_constructible_support,
            PropertiesOfT<Prof>::copy_constructible_support,
            PropertiesOfT<Prof>::move_assignable_support,
            PropertiesOfT<Prof>::copy_assignable_support,
            PropertiesOfT<Prof>::destructible_support>(MakeArchetypeState(),
                                                       state) {}

  friend Archetype MakeArchetype<Archetype>(ArchetypeState) noexcept;
};

template <class Prof>
class Archetype<Prof, typename std::enable_if<
                          PropertiesOfT<Prof>::move_constructible_support ==
                              move_constructible::maybe &&
                          PropertiesOfT<Prof>::move_assignable_support ==
                              move_assignable::maybe &&
                          PropertiesOfT<Prof>::destructible_support ==
                              destructible::maybe>::type>
    : ArchetypeSpecialMembersBase<
          PropertiesOfT<Prof>::default_constructible_support,
          PropertiesOfT<Prof>::move_constructible_support,
          PropertiesOfT<Prof>::copy_constructible_support,
          PropertiesOfT<Prof>::move_assignable_support,
          PropertiesOfT<Prof>::copy_assignable_support,
          PropertiesOfT<Prof>::destructible_support> {
 public:
  Archetype() = default;
  Archetype(Archetype&&) = delete;
  Archetype(const Archetype&) = default;
  Archetype& operator=(Archetype&&) = delete;
  Archetype& operator=(const Archetype&) = default;
  ~Archetype() = delete;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteConstructor<Prof, T>()>::type* = nullptr>
  Archetype(T&&) = delete;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteAssign<Prof, T>()>::type* = nullptr>
  Archetype& operator=(T&&) = delete;

  using ArchetypeSpecialMembersBase<
      PropertiesOfT<Prof>::default_constructible_support,
      PropertiesOfT<Prof>::move_constructible_support,
      PropertiesOfT<Prof>::copy_constructible_support,
      PropertiesOfT<Prof>::move_assignable_support,
      PropertiesOfT<Prof>::copy_assignable_support,
      PropertiesOfT<Prof>::destructible_support>::archetype_state;

 private:
  explicit Archetype(MakeArchetypeState, ArchetypeState state) noexcept
      : ArchetypeSpecialMembersBase<
            PropertiesOfT<Prof>::default_constructible_support,
            PropertiesOfT<Prof>::move_constructible_support,
            PropertiesOfT<Prof>::copy_constructible_support,
            PropertiesOfT<Prof>::move_assignable_support,
            PropertiesOfT<Prof>::copy_assignable_support,
            PropertiesOfT<Prof>::destructible_support>(MakeArchetypeState(),
                                                       state) {}

  friend Archetype MakeArchetype<Archetype>(ArchetypeState) noexcept;
};

template <class Prof>
class Archetype<Prof, typename std::enable_if<
                          PropertiesOfT<Prof>::move_constructible_support ==
                              move_constructible::maybe &&
                          PropertiesOfT<Prof>::move_assignable_support !=
                              move_assignable::maybe &&
                          PropertiesOfT<Prof>::destructible_support ==
                              destructible::maybe>::type>
    : ArchetypeSpecialMembersBase<
          PropertiesOfT<Prof>::default_constructible_support,
          PropertiesOfT<Prof>::move_constructible_support,
          PropertiesOfT<Prof>::copy_constructible_support,
          PropertiesOfT<Prof>::move_assignable_support,
          PropertiesOfT<Prof>::copy_assignable_support,
          PropertiesOfT<Prof>::destructible_support> {
 public:
  Archetype() = default;
  Archetype(Archetype&&) = delete;
  Archetype(const Archetype&) = default;
  Archetype& operator=(Archetype&&) = default;
  Archetype& operator=(const Archetype&) = default;
  ~Archetype() = delete;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteConstructor<Prof, T>()>::type* = nullptr>
  Archetype(T&&) = delete;

  // Disallow moves when requested, and disallow implicit conversions.
  template <class T, typename std::enable_if<
                         ShouldDeleteAssign<Prof, T>()>::type* = nullptr>
  Archetype& operator=(T&&) = delete;

  using ArchetypeSpecialMembersBase<
      PropertiesOfT<Prof>::default_constructible_support,
      PropertiesOfT<Prof>::move_constructible_support,
      PropertiesOfT<Prof>::copy_constructible_support,
      PropertiesOfT<Prof>::move_assignable_support,
      PropertiesOfT<Prof>::copy_assignable_support,
      PropertiesOfT<Prof>::destructible_support>::archetype_state;

 private:
  explicit Archetype(MakeArchetypeState, ArchetypeState state) noexcept
      : ArchetypeSpecialMembersBase<
            PropertiesOfT<Prof>::default_constructible_support,
            PropertiesOfT<Prof>::move_constructible_support,
            PropertiesOfT<Prof>::copy_constructible_support,
            PropertiesOfT<Prof>::move_assignable_support,
            PropertiesOfT<Prof>::copy_assignable_support,
            PropertiesOfT<Prof>::destructible_support>(MakeArchetypeState(),
                                                       state) {}

  friend Archetype MakeArchetype<Archetype>(ArchetypeState) noexcept;
};

// Explicitly deleted swap for Archetype if the profile does not require swap.
// It is important to delete it rather than simply leave it out so that the
// "using std::swap;" idiom will result in this deleted overload being picked.
template <class Prof,
          absl::enable_if_t<!PropertiesOfT<Prof>::is_swappable, int> = 0>
void swap(Archetype<Prof>&, Archetype<Prof>&) = delete;  // NOLINT

// A conditionally-noexcept swap implementation for Archetype when the profile
// supports swap.
template <class Prof,
          absl::enable_if_t<PropertiesOfT<Prof>::is_swappable, int> = 0>
void swap(Archetype<Prof>& lhs, Archetype<Prof>& rhs)  // NOLINT
    noexcept(PropertiesOfT<Prof>::swappable_support != swappable::yes) {
  std::swap(lhs.archetype_state, rhs.archetype_state);
}

// A convertible-to-bool type that is used as the return type of comparison
// operators since the standard doesn't always require exactly bool.
struct NothrowBool {
  explicit NothrowBool() = delete;
  ~NothrowBool() = default;

  // TODO(calabrese) Delete the copy constructor in C++17 mode since guaranteed
  // elision makes it not required when returning from a function.
  // NothrowBool(NothrowBool const&) = delete;

  NothrowBool& operator=(NothrowBool const&) = delete;

  explicit operator bool() const noexcept { return value; }

  static NothrowBool make(bool const value) noexcept {
    return NothrowBool(value);
  }

 private:
  explicit NothrowBool(bool const value) noexcept : value(value) {}

  bool value;
};

// A convertible-to-bool type that is used as the return type of comparison
// operators since the standard doesn't always require exactly bool.
// Note: ExceptionalBool has a conversion operator that is not noexcept, so
// that even when a comparison operator is noexcept, that operation may still
// potentially throw when converted to bool.
struct ExceptionalBool {
  explicit ExceptionalBool() = delete;
  ~ExceptionalBool() = default;

  // TODO(calabrese) Delete the copy constructor in C++17 mode since guaranteed
  // elision makes it not required when returning from a function.
  // ExceptionalBool(ExceptionalBool const&) = delete;

  ExceptionalBool& operator=(ExceptionalBool const&) = delete;

  explicit operator bool() const { return value; }  // NOLINT

  static ExceptionalBool make(bool const value) noexcept {
    return ExceptionalBool(value);
  }

 private:
  explicit ExceptionalBool(bool const value) noexcept : value(value) {}

  bool value;
};

// The following macro is only used as a helper in this file to stamp out
// comparison operator definitions. It is undefined after usage.
//
// NOTE: Non-nothrow operators throw via their result's conversion to bool even
// though the operation itself is noexcept.
#define ABSL_TYPES_INTERNAL_OP(enum_name, op)                                \
  template <class Prof>                                                      \
  absl::enable_if_t<!PropertiesOfT<Prof>::is_##enum_name, bool> operator op( \
      const Archetype<Prof>&, const Archetype<Prof>&) = delete;              \
                                                                             \
  template <class Prof>                                                      \
  typename absl::enable_if_t<                                                \
      PropertiesOfT<Prof>::is_##enum_name,                                   \
      std::conditional<PropertiesOfT<Prof>::enum_name##_support ==           \
                           enum_name::nothrow,                               \
                       NothrowBool, ExceptionalBool>>::type                  \
  operator op(const Archetype<Prof>& lhs,                                    \
              const Archetype<Prof>& rhs) noexcept {                         \
    return absl::conditional_t<                                              \
        PropertiesOfT<Prof>::enum_name##_support == enum_name::nothrow,      \
        NothrowBool, ExceptionalBool>::make(lhs.archetype_state op           \
                                                rhs.archetype_state);        \
  }

ABSL_TYPES_INTERNAL_OP(equality_comparable, ==);
ABSL_TYPES_INTERNAL_OP(inequality_comparable, !=);
ABSL_TYPES_INTERNAL_OP(less_than_comparable, <);
ABSL_TYPES_INTERNAL_OP(less_equal_comparable, <=);
ABSL_TYPES_INTERNAL_OP(greater_equal_comparable, >=);
ABSL_TYPES_INTERNAL_OP(greater_than_comparable, >);

#undef ABSL_TYPES_INTERNAL_OP

// Base class for std::hash specializations when an Archetype doesn't support
// hashing.
struct PoisonedHash {
  PoisonedHash() = delete;
  PoisonedHash(const PoisonedHash&) = delete;
  PoisonedHash& operator=(const PoisonedHash&) = delete;
};

// Base class for std::hash specializations when an Archetype supports hashing.
template <class Prof>
struct EnabledHash {
  using argument_type = Archetype<Prof>;
  using result_type = std::size_t;
  result_type operator()(const argument_type& arg) const {
    return std::hash<ArchetypeState>()(arg.archetype_state);
  }
};

}  // namespace types_internal
ABSL_NAMESPACE_END
}  // namespace absl

namespace std {

template <class Prof>  // NOLINT
struct hash<::absl::types_internal::Archetype<Prof>>
    : conditional<::absl::types_internal::PropertiesOfT<Prof>::is_hashable,
                  ::absl::types_internal::EnabledHash<Prof>,
                  ::absl::types_internal::PoisonedHash>::type {};

}  // namespace std

#endif  // ABSL_TYPES_INTERNAL_CONFORMANCE_ARCHETYPE_H_
