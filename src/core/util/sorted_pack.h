// Copyright 2022 gRPC authors.
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

#ifndef GRPC_SRC_CORE_UTIL_SORTED_PACK_H
#define GRPC_SRC_CORE_UTIL_SORTED_PACK_H

#include <grpc/support/port_platform.h>

#include <cstddef>
#include <type_traits>

#include "src/core/util/type_list.h"

namespace grpc_core {

namespace sorted_pack_detail {

constexpr bool IsValidAlignment(size_t a) {
  return a == 64 || a == 32 || a == 16 || a == 8 || a == 4 || a == 2 || a == 1;
}

template <typename... Ts>
constexpr bool ValidateAlignments() {
  return (IsValidAlignment(alignof(Ts)) && ...);
}

template <size_t TargetAlignment, typename InputList>
struct FilterByAlignment;

template <size_t TargetAlignment, typename T, typename... Rest>
struct FilterByAlignment<TargetAlignment, Typelist<T, Rest...>> {
  using Prev = typename FilterByAlignment<TargetAlignment, Typelist<Rest...>>::Result;
  static constexpr bool kMatches = alignof(T) == TargetAlignment;
  using Result = typename std::conditional<kMatches,
                                           typename Prev::template PushFront<T>,
                                           Prev>::type;
};

template <size_t TargetAlignment>
struct FilterByAlignment<TargetAlignment, Typelist<>> {
  using Result = Typelist<>;
};

template <typename L1, typename L2>
struct Concat;

template <typename... T1s, typename... T2s>
struct Concat<Typelist<T1s...>, Typelist<T2s...>> {
  using Result = Typelist<T1s..., T2s...>;
};

template <typename InputList, size_t... Alignments>
struct MultiFilter;

template <typename InputList, size_t A, size_t... Rest>
struct MultiFilter<InputList, A, Rest...> {
  using Result = typename Concat<
      typename FilterByAlignment<A, InputList>::Result,
      typename MultiFilter<InputList, Rest...>::Result>::Result;
};

template <typename InputList>
struct MultiFilter<InputList> {
  using Result = Typelist<>;
};

}  // namespace sorted_pack_detail

// WithSortedPack: A minimal-instantiation, exact-alignment bucket sort.
template <template <typename...> class T,
          template <typename, typename> class Cmp, typename... Args>
struct WithSortedPack {
  static_assert(sizeof...(Args) == 0 || sorted_pack_detail::ValidateAlignments<Args...>(),
                "Unsupported alignment in WithSortedPack");

  using SortedList = typename sorted_pack_detail::MultiFilter<
      Typelist<Args...>,
      64, 32, 16, 8, 4, 2, 1>::Result;

  using Type = typename SortedList::template Instantiate<T>;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_UTIL_SORTED_PACK_H
