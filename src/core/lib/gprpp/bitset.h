// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_LIB_GPRPP_BITSET_H
#define GRPC_CORE_LIB_GPRPP_BITSET_H

#include <grpc/impl/codegen/port_platform.h>

#include <utility>

namespace grpc_core {

template <std::size_t kBits> struct UintSelector;
template <> struct UintSelector<8> { typedef uint8_t Type; };
template <> struct UintSelector<16> { typedef uint16_t Type; };
template <> struct UintSelector<32> { typedef uint32_t Type; };
template <> struct UintSelector<64> { typedef uint64_t Type; };

template <std::size_t kBits> using Uint = typename UintSelector<kBits>::Type;

constexpr std::size_t ChooseUnitBits(std::size_t total_bits) {
    return total_bits <= 8? 8 :
           total_bits <= 16? 16 :
           total_bits <= 32? 32 :
           total_bits <= 64? 64 :
           total_bits <= 96? 32 :
           64;
}

template <std::size_t kTotalBits, std::size_t kUnitBits = ChooseUnitBits(kTotalBits)>
class BitSet {
    static constexpr std::size_t kUnits = (kTotalBits + kUnitBits - 1) / kUnitBits;

public:
    constexpr BitSet() : units_{} {}

    void set(int i) {
        units_[unit_for(i)] |= mask_for(i);
    }

    void set(int i, bool is_set) {
    	if (is_set) set(i); else clear(i);
    }

    void clear(int i) {
        units_[unit_for(i)] &= ~mask_for(i);
    }

    constexpr bool is_set(int i) const {
        return (units_[unit_for(i)] & mask_for(i)) != 0;
    }

private:
    static constexpr std::size_t unit_for(std::size_t bit) {
        return bit / kUnitBits;
    }

    static constexpr Uint<kUnitBits> mask_for(std::size_t bit) {
        return Uint<kUnitBits>{1} << (bit % kUnitBits);
    }

    Uint<kUnitBits> units_[kUnits];
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_BITSET_H
