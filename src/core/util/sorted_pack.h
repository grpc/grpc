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

template <typename L1, typename L2>
struct Concat;

template <typename... T1s, typename... T2s>
struct Concat<Typelist<T1s...>, Typelist<T2s...>> {
  using Result = Typelist<T1s..., T2s...>;
};

// Single-pass MultiPartition minimizes template instantiations to exactly O(N).
template <typename L64, typename L32, typename L16, typename L8, typename L4, typename L2, typename L1, typename... Ts>
struct MultiPartition;

template <typename L64, typename L32, typename L16, typename L8, typename L4, typename L2, typename L1, typename T, typename... Rest>
struct MultiPartition<L64, L32, L16, L8, L4, L2, L1, T, Rest...> {
  using NextL64 = typename std::conditional<alignof(T) == 64, typename L64::template PushBack<T>, L64>::type;
  using NextL32 = typename std::conditional<alignof(T) == 32, typename L32::template PushBack<T>, L32>::type;
  using NextL16 = typename std::conditional<alignof(T) == 16, typename L16::template PushBack<T>, L16>::type;
  using NextL8  = typename std::conditional<alignof(T) == 8,  typename L8::template PushBack<T>,  L8>::type;
  using NextL4  = typename std::conditional<alignof(T) == 4,  typename L4::template PushBack<T>,  L4>::type;
  using NextL2  = typename std::conditional<alignof(T) == 2,  typename L2::template PushBack<T>,  L2>::type;
  using NextL1  = typename std::conditional<alignof(T) == 1,  typename L1::template PushBack<T>,  L1>::type;

  using Result = typename MultiPartition<NextL64, NextL32, NextL16, NextL8, NextL4, NextL2, NextL1, Rest...>::Result;
};

template <typename L64, typename L32, typename L16, typename L8, typename L4, typename L2, typename L1>
struct MultiPartition<L64, L32, L16, L8, L4, L2, L1> {
  using Result = typename Concat<L64,
                   typename Concat<L32,
                     typename Concat<L16,
                       typename Concat<L8,
                         typename Concat<L4,
                           typename Concat<L2, L1>::Result
                         >::Result
                       >::Result
                     >::Result
                   >::Result
                 >::Result;
};

}  // namespace sorted_pack_detail

// WithSortedPack: A single-pass, exact-alignment bucket sort.
template <template <typename...> class T,
          template <typename, typename> class Cmp, typename... Args>
struct WithSortedPack {
  static_assert(sizeof...(Args) == 0 || sorted_pack_detail::ValidateAlignments<Args...>(),
                "Unsupported alignment in WithSortedPack");

  using SortedList = typename sorted_pack_detail::MultiPartition<
      Typelist<>, Typelist<>, Typelist<>, Typelist<>,
      Typelist<>, Typelist<>, Typelist<>, Args...>::Result;

  using Type = typename SortedList::template Instantiate<T>;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_UTIL_SORTED_PACK_H
