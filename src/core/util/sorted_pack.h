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

// Trait to extract the integer score for a type.
// By default, it is engineered to keep PackedTable sorted by alignment
// (descending) and then by size (descending) using a standard ascending radix sort.
template <typename T, typename = void>
struct RadixScore {
  // We assume alignment <= 256. Drop sizeof(T) to minimize key size.
  static constexpr size_t kValue = 256 - alignof(T);
};

// If the type provides its own sorting key for testing:
template <typename T>
struct RadixScore<T, std::void_t<decltype(T::kRadixScore)>> {
  static constexpr size_t kValue = T::kRadixScore;
};

// Pair a type with its statically evaluated score.
template <typename T, size_t S>
struct Node {
  using Type = T;
  static constexpr size_t kScore = S;
};

// Compute bit width of an integer at compile time.
constexpr int BitWidth(size_t x) {
  int bits = 0;
  while (x > 0) {
    bits++;
    x >>= 1;
  }
  return bits;
}

// Phase 1: Map input types into Node pairs while determining the MaxScore.
template <typename... Ts>
struct MapAndBitOR;

template <typename T, typename... Ts>
struct MapAndBitOR<T, Ts...> {
  using Rest = MapAndBitOR<Ts...>;
  // Bitwise OR of scores to determine the maximum active bit width.
  static constexpr size_t kMaxScore = RadixScore<T>::kValue | Rest::kMaxScore;
  using List = typename Rest::List::template PushFront<
      Node<T, RadixScore<T>::kValue>>;
};

template <>
struct MapAndBitOR<> {
  static constexpr size_t kMaxScore = 0;
  using List = Typelist<>;
};

// Filter nodes where the specified bitmask matches the flag.
template <size_t Mask, bool Flag, typename InputList>
struct FilterList;

template <size_t Mask, bool Flag, typename Node, typename... Rest>
struct FilterList<Mask, Flag, Typelist<Node, Rest...>> {
  using Prev = typename FilterList<Mask, Flag, Typelist<Rest...>>::Result;
  static constexpr bool kMatches = ((Node::kScore & Mask) != 0) == Flag;
  using Result = typename std::conditional<kMatches,
                                           typename Prev::template PushFront<Node>,
                                           Prev>::type;
};

template <size_t Mask, bool Flag>
struct FilterList<Mask, Flag, Typelist<>> {
  using Result = Typelist<>;
};

// Concatenate two typelists.
template <typename L1, typename L2>
struct Concat;

template <typename... T1s, typename... T2s>
struct Concat<Typelist<T1s...>, Typelist<T2s...>> {
  using Result = Typelist<T1s..., T2s...>;
};

// Execute a single stable radix sort pass for the given bitmask.
// Standard stable ascending radix sort: elements with bit=0 come BEFORE elements with bit=1.
template <size_t Mask, typename InputList>
struct RadixSortPass {
  using Zeroes = typename FilterList<Mask, false, InputList>::Result;
  using Ones = typename FilterList<Mask, true, InputList>::Result;
  using Result = typename Concat<Zeroes, Ones>::Result;
};

// Perform radix sort across all necessary bits (LSD to MSD).
template <int Bit, int MaxBit, typename InputList>
struct RadixSortLoop {
  using Next = typename RadixSortPass<1ULL << Bit, InputList>::Result;
  using Result =
      typename RadixSortLoop<Bit + 1, MaxBit, Next>::Result;
};

template <int MaxBit, typename InputList>
struct RadixSortLoop<MaxBit, MaxBit, InputList> {
  using Result = InputList;
};

// Strip the Node overhead to recover the final sorted list of types.
template <typename OutputList>
struct Unpack;

template <typename... Nodes>
struct Unpack<Typelist<Nodes...>> {
  using Result = Typelist<typename Nodes::Type...>;
};

}  // namespace sorted_pack_detail

// WithSortedPack: A completely stable, O(N)-instantiation Radix Sort.
template <template <typename...> class T,
          template <typename, typename> class Cmp, typename... Args>
struct WithSortedPack {
  // Phase 1: Evaluate node scores and determine bit width.
  using Phase1 = sorted_pack_detail::MapAndBitOR<Args...>;
  static constexpr int kBits = sorted_pack_detail::BitWidth(Phase1::kMaxScore);

  // Phase 2: Radix sort across required bits.
  using Phase2 = typename sorted_pack_detail::RadixSortLoop<
      0, kBits, typename Phase1::List>::Result;

  // Phase 3: Unpack the types and instantiate T.
  using Type = typename sorted_pack_detail::Unpack<Phase2>::Result::template Instantiate<T>;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_UTIL_SORTED_PACK_H
