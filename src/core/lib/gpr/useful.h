/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_GPR_USEFUL_H
#define GRPC_CORE_LIB_GPR_USEFUL_H

#include <grpc/support/port_platform.h>

#include <cstddef>

/** useful utilities that don't belong anywhere else */

namespace grpc_core {

template <typename T>
T Clamp(T val, T min, T max) {
  if (val < min) return min;
  if (max < val) return max;
  return val;
}

/** rotl, rotr assume x is unsigned */
template <typename T>
constexpr T RotateLeft(T x, T n) {
  return ((x << n) | (x >> (sizeof(x) * 8 - n)));
}
template <typename T>
constexpr T RotateRight(T x, T n) {
  return ((x >> n) | (x << (sizeof(x) * 8 - n)));
}

// Set the n-th bit of i
template <typename T>
T SetBit(T* i, size_t n) {
  return *i |= (T(1) << n);
}

// Clear the n-th bit of i
template <typename T>
T ClearBit(T* i, size_t n) {
  return *i &= ~(T(1) << n);
}

// Get the n-th bit of i
template <typename T>
bool GetBit(T i, size_t n) {
  return (i & (T(1) << n)) != 0;
}

namespace useful_detail {
inline constexpr uint32_t HexdigitBitcount(uint32_t x) {
  return (x - ((x >> 1) & 0x77777777) - ((x >> 2) & 0x33333333) -
          ((x >> 3) & 0x11111111));
}
}  // namespace useful_detail

inline constexpr uint32_t BitCount(uint32_t i) {
  return (((useful_detail::HexdigitBitcount(i) +
            (useful_detail::HexdigitBitcount(i) >> 4)) &
           0x0f0f0f0f) %
          255);
}

inline constexpr uint32_t BitCount(uint64_t i) {
  return BitCount(uint32_t(i)) + BitCount(uint32_t(i >> 32));
}

inline constexpr uint32_t BitCount(uint16_t i) { return BitCount(uint32_t(i)); }
inline constexpr uint32_t BitCount(uint8_t i) { return BitCount(uint32_t(i)); }
inline constexpr uint32_t BitCount(int64_t i) { return BitCount(uint64_t(i)); }
inline constexpr uint32_t BitCount(int32_t i) { return BitCount(uint32_t(i)); }
inline constexpr uint32_t BitCount(int16_t i) { return BitCount(uint16_t(i)); }
inline constexpr uint32_t BitCount(int8_t i) { return BitCount(uint8_t(i)); }

// This function uses operator< to implement a qsort-style comparison, whereby:
// if a is smaller than b, a number smaller than 0 is returned.
// if a is bigger than b, a number greater than 0 is returned.
// if a is neither smaller nor bigger than b, 0 is returned.
template <typename T>
int QsortCompare(const T& a, const T& b) {
  if (a < b) return -1;
  if (b < a) return 1;
  return 0;
}

template <typename T>
constexpr size_t HashPointer(T* p, size_t range) {
  return (((reinterpret_cast<size_t>(p)) >> 4) ^
          ((reinterpret_cast<size_t>(p)) >> 9) ^
          ((reinterpret_cast<size_t>(p)) >> 14)) %
         range;
}

inline uint32_t MixHash32(uint32_t a, uint32_t b) {
  return RotateLeft(a, 2u) ^ b;
}

}  // namespace grpc_core

#define GPR_ARRAY_SIZE(array) (sizeof(array) / sizeof(*(array)))

#endif /* GRPC_CORE_LIB_GPR_USEFUL_H */
