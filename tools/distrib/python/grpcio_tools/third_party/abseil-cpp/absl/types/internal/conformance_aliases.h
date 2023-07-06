// Copyright 2018 The Abseil Authors.
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
// regularity_aliases.h
// -----------------------------------------------------------------------------
//
// This file contains type aliases of common ConformanceProfiles and Archetypes
// so that they can be directly used by name without creating them from scratch.

#ifndef ABSL_TYPES_INTERNAL_CONFORMANCE_ALIASES_H_
#define ABSL_TYPES_INTERNAL_CONFORMANCE_ALIASES_H_

#include "absl/types/internal/conformance_archetype.h"
#include "absl/types/internal/conformance_profile.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace types_internal {

// Creates both a Profile and a corresponding Archetype with root name "name".
#define ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(name, ...)                \
  struct name##Profile : __VA_ARGS__ {};                                    \
                                                                            \
  using name##Archetype = ::absl::types_internal::Archetype<name##Profile>; \
                                                                            \
  template <class AbslInternalProfileTag>                                   \
  using name##Archetype##_ = ::absl::types_internal::Archetype<             \
      ::absl::types_internal::StrongProfileTypedef<name##Profile,           \
                                                   AbslInternalProfileTag>>

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasTrivialDefaultConstructor,
    ConformanceProfile<default_constructible::trivial>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasNothrowDefaultConstructor,
    ConformanceProfile<default_constructible::nothrow>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasDefaultConstructor, ConformanceProfile<default_constructible::yes>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasTrivialMoveConstructor, ConformanceProfile<default_constructible::maybe,
                                                  move_constructible::trivial>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasNothrowMoveConstructor, ConformanceProfile<default_constructible::maybe,
                                                  move_constructible::nothrow>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasMoveConstructor,
    ConformanceProfile<default_constructible::maybe, move_constructible::yes>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasTrivialCopyConstructor,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::trivial>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasNothrowCopyConstructor,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::nothrow>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasCopyConstructor,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::yes>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasTrivialMoveAssign,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::trivial>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasNothrowMoveAssign,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::nothrow>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasMoveAssign,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::yes>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasTrivialCopyAssign,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::trivial>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasNothrowCopyAssign,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::nothrow>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasCopyAssign,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::yes>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasTrivialDestructor,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::maybe, destructible::trivial>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasNothrowDestructor,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::maybe, destructible::nothrow>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasDestructor,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::maybe, destructible::yes>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasNothrowEquality,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::maybe, destructible::maybe,
                       equality_comparable::nothrow>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasEquality,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::maybe, destructible::maybe,
                       equality_comparable::yes>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasNothrowInequality,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::maybe, destructible::maybe,
                       equality_comparable::maybe,
                       inequality_comparable::nothrow>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasInequality,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::maybe, destructible::maybe,
                       equality_comparable::maybe, inequality_comparable::yes>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasNothrowLessThan,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::maybe, destructible::maybe,
                       equality_comparable::maybe, inequality_comparable::maybe,
                       less_than_comparable::nothrow>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasLessThan,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::maybe, destructible::maybe,
                       equality_comparable::maybe, inequality_comparable::maybe,
                       less_than_comparable::yes>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasNothrowLessEqual,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::maybe, destructible::maybe,
                       equality_comparable::maybe, inequality_comparable::maybe,
                       less_than_comparable::maybe,
                       less_equal_comparable::nothrow>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasLessEqual,
    ConformanceProfile<default_constructible::maybe, move_constructible::maybe,
                       copy_constructible::maybe, move_assignable::maybe,
                       copy_assignable::maybe, destructible::maybe,
                       equality_comparable::maybe, inequality_comparable::maybe,
                       less_than_comparable::maybe,
                       less_equal_comparable::yes>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasNothrowGreaterEqual,
    ConformanceProfile<
        default_constructible::maybe, move_constructible::maybe,
        copy_constructible::maybe, move_assignable::maybe,
        copy_assignable::maybe, destructible::maybe, equality_comparable::maybe,
        inequality_comparable::maybe, less_than_comparable::maybe,
        less_equal_comparable::maybe, greater_equal_comparable::nothrow>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasGreaterEqual,
    ConformanceProfile<
        default_constructible::maybe, move_constructible::maybe,
        copy_constructible::maybe, move_assignable::maybe,
        copy_assignable::maybe, destructible::maybe, equality_comparable::maybe,
        inequality_comparable::maybe, less_than_comparable::maybe,
        less_equal_comparable::maybe, greater_equal_comparable::yes>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasNothrowGreaterThan,
    ConformanceProfile<
        default_constructible::maybe, move_constructible::maybe,
        copy_constructible::maybe, move_assignable::maybe,
        copy_assignable::maybe, destructible::maybe, equality_comparable::maybe,
        inequality_comparable::maybe, less_than_comparable::maybe,
        less_equal_comparable::maybe, greater_equal_comparable::maybe,
        greater_than_comparable::nothrow>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasGreaterThan,
    ConformanceProfile<
        default_constructible::maybe, move_constructible::maybe,
        copy_constructible::maybe, move_assignable::maybe,
        copy_assignable::maybe, destructible::maybe, equality_comparable::maybe,
        inequality_comparable::maybe, less_than_comparable::maybe,
        less_equal_comparable::maybe, greater_equal_comparable::maybe,
        greater_than_comparable::yes>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasNothrowSwap,
    ConformanceProfile<
        default_constructible::maybe, move_constructible::maybe,
        copy_constructible::maybe, move_assignable::maybe,
        copy_assignable::maybe, destructible::maybe, equality_comparable::maybe,
        inequality_comparable::maybe, less_than_comparable::maybe,
        less_equal_comparable::maybe, greater_equal_comparable::maybe,
        greater_than_comparable::maybe, swappable::nothrow>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasSwap,
    ConformanceProfile<
        default_constructible::maybe, move_constructible::maybe,
        copy_constructible::maybe, move_assignable::maybe,
        copy_assignable::maybe, destructible::maybe, equality_comparable::maybe,
        inequality_comparable::maybe, less_than_comparable::maybe,
        less_equal_comparable::maybe, greater_equal_comparable::maybe,
        greater_than_comparable::maybe, swappable::yes>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HasStdHashSpecialization,
    ConformanceProfile<
        default_constructible::maybe, move_constructible::maybe,
        copy_constructible::maybe, move_assignable::maybe,
        copy_assignable::maybe, destructible::maybe, equality_comparable::maybe,
        inequality_comparable::maybe, less_than_comparable::maybe,
        less_equal_comparable::maybe, greater_equal_comparable::maybe,
        greater_than_comparable::maybe, swappable::maybe, hashable::yes>);

////////////////////////////////////////////////////////////////////////////////
////     The remaining aliases are combinations of the previous aliases.    ////
////////////////////////////////////////////////////////////////////////////////

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    Equatable, CombineProfiles<HasEqualityProfile, HasInequalityProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    Comparable,
    CombineProfiles<EquatableProfile, HasLessThanProfile, HasLessEqualProfile,
                    HasGreaterEqualProfile, HasGreaterThanProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    NothrowEquatable,
    CombineProfiles<HasNothrowEqualityProfile, HasNothrowInequalityProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    NothrowComparable,
    CombineProfiles<NothrowEquatableProfile, HasNothrowLessThanProfile,
                    HasNothrowLessEqualProfile, HasNothrowGreaterEqualProfile,
                    HasNothrowGreaterThanProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    Value,
    CombineProfiles<HasNothrowMoveConstructorProfile, HasCopyConstructorProfile,
                    HasNothrowMoveAssignProfile, HasCopyAssignProfile,
                    HasNothrowDestructorProfile, HasNothrowSwapProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    EquatableValue, CombineProfiles<EquatableProfile, ValueProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    ComparableValue, CombineProfiles<ComparableProfile, ValueProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    DefaultConstructibleValue,
    CombineProfiles<HasDefaultConstructorProfile, ValueProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    NothrowMoveConstructible, CombineProfiles<HasNothrowMoveConstructorProfile,
                                              HasNothrowDestructorProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    EquatableNothrowMoveConstructible,
    CombineProfiles<EquatableProfile, NothrowMoveConstructibleProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    ComparableNothrowMoveConstructible,
    CombineProfiles<ComparableProfile, NothrowMoveConstructibleProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    DefaultConstructibleNothrowMoveConstructible,
    CombineProfiles<HasDefaultConstructorProfile,
                    NothrowMoveConstructibleProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    CopyConstructible,
    CombineProfiles<HasNothrowMoveConstructorProfile, HasCopyConstructorProfile,
                    HasNothrowDestructorProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    EquatableCopyConstructible,
    CombineProfiles<EquatableProfile, CopyConstructibleProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    ComparableCopyConstructible,
    CombineProfiles<ComparableProfile, CopyConstructibleProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    DefaultConstructibleCopyConstructible,
    CombineProfiles<HasDefaultConstructorProfile, CopyConstructibleProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    NothrowMovable,
    CombineProfiles<HasNothrowMoveConstructorProfile,
                    HasNothrowMoveAssignProfile, HasNothrowDestructorProfile,
                    HasNothrowSwapProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    EquatableNothrowMovable,
    CombineProfiles<EquatableProfile, NothrowMovableProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    ComparableNothrowMovable,
    CombineProfiles<ComparableProfile, NothrowMovableProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    DefaultConstructibleNothrowMovable,
    CombineProfiles<HasDefaultConstructorProfile, NothrowMovableProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    TrivialSpecialMemberFunctions,
    CombineProfiles<HasTrivialDefaultConstructorProfile,
                    HasTrivialMoveConstructorProfile,
                    HasTrivialCopyConstructorProfile,
                    HasTrivialMoveAssignProfile, HasTrivialCopyAssignProfile,
                    HasTrivialDestructorProfile, HasNothrowSwapProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    TriviallyComplete,
    CombineProfiles<TrivialSpecialMemberFunctionsProfile, ComparableProfile,
                    HasStdHashSpecializationProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HashableNothrowMoveConstructible,
    CombineProfiles<HasStdHashSpecializationProfile,
                    NothrowMoveConstructibleProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HashableCopyConstructible,
    CombineProfiles<HasStdHashSpecializationProfile, CopyConstructibleProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HashableNothrowMovable,
    CombineProfiles<HasStdHashSpecializationProfile, NothrowMovableProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    HashableValue,
    CombineProfiles<HasStdHashSpecializationProfile, ValueProfile>);

ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS(
    ComparableHashableValue,
    CombineProfiles<HashableValueProfile, ComparableProfile>);

// The "preferred" profiles that we support in Abseil.
template <template <class...> class Receiver>
using ExpandBasicProfiles =
    Receiver<NothrowMoveConstructibleProfile, CopyConstructibleProfile,
             NothrowMovableProfile, ValueProfile>;

// The basic profiles except that they are also all Equatable.
template <template <class...> class Receiver>
using ExpandBasicEquatableProfiles =
    Receiver<EquatableNothrowMoveConstructibleProfile,
             EquatableCopyConstructibleProfile, EquatableNothrowMovableProfile,
             EquatableValueProfile>;

// The basic profiles except that they are also all Comparable.
template <template <class...> class Receiver>
using ExpandBasicComparableProfiles =
    Receiver<ComparableNothrowMoveConstructibleProfile,
             ComparableCopyConstructibleProfile,
             ComparableNothrowMovableProfile, ComparableValueProfile>;

// The basic profiles except that they are also all Hashable.
template <template <class...> class Receiver>
using ExpandBasicHashableProfiles =
    Receiver<HashableNothrowMoveConstructibleProfile,
             HashableCopyConstructibleProfile, HashableNothrowMovableProfile,
             HashableValueProfile>;

// The basic profiles except that they are also all DefaultConstructible.
template <template <class...> class Receiver>
using ExpandBasicDefaultConstructibleProfiles =
    Receiver<DefaultConstructibleNothrowMoveConstructibleProfile,
             DefaultConstructibleCopyConstructibleProfile,
             DefaultConstructibleNothrowMovableProfile,
             DefaultConstructibleValueProfile>;

// The type profiles that we support in Abseil (all of the previous lists).
template <template <class...> class Receiver>
using ExpandSupportedProfiles = Receiver<
    NothrowMoveConstructibleProfile, CopyConstructibleProfile,
    NothrowMovableProfile, ValueProfile,
    EquatableNothrowMoveConstructibleProfile, EquatableCopyConstructibleProfile,
    EquatableNothrowMovableProfile, EquatableValueProfile,
    ComparableNothrowMoveConstructibleProfile,
    ComparableCopyConstructibleProfile, ComparableNothrowMovableProfile,
    ComparableValueProfile, DefaultConstructibleNothrowMoveConstructibleProfile,
    DefaultConstructibleCopyConstructibleProfile,
    DefaultConstructibleNothrowMovableProfile, DefaultConstructibleValueProfile,
    HashableNothrowMoveConstructibleProfile, HashableCopyConstructibleProfile,
    HashableNothrowMovableProfile, HashableValueProfile>;

// TODO(calabrese) Include types that have throwing move constructors, since in
// practice we still need to support them because of standard library types with
// (potentially) non-noexcept moves.

}  // namespace types_internal
ABSL_NAMESPACE_END
}  // namespace absl

#undef ABSL_INTERNAL_PROFILE_AND_ARCHETYPE_ALIAS

#endif  // ABSL_TYPES_INTERNAL_CONFORMANCE_ALIASES_H_
