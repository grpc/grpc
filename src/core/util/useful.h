// Copyright 2015 gRPC authors.
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

#ifndef GRPC_SRC_CORE_UTIL_USEFUL_H
#define GRPC_SRC_CORE_UTIL_USEFUL_H

#include <grpc/support/port_platform.h>

#include <cstddef>
#include <limits>
#include <type_traits>
#include <variant>

#include "absl/log/check.h"
#include "absl/numeric/bits.h"
#include "absl/strings/string_view.h"

/// useful utilities that don't belong anywhere else

namespace grpc_core {

template <typename T>
T Clamp(T val, T min, T max) {
  if (val < min) return min;
  if (max < val) return max;
  return val;
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

template <typename... X>
int QsortCompare(const std::variant<X...>& a, const std::variant<X...>& b) {
  const int index = QsortCompare(a.index(), b.index());
  if (index != 0) return index;
  return std::visit(
      [&](const auto& x) {
        return QsortCompare(x, std::get<absl::remove_cvref_t<decltype(x)>>(b));
      },
      a);
}

inline int QsortCompare(absl::string_view a, absl::string_view b) {
  return a.compare(b);
}

inline int QsortCompare(const std::string& a, const std::string& b) {
  return a.compare(b);
}

template <typename A, typename B>
int QsortCompare(const std::pair<A, B>& a, const std::pair<A, B>& b) {
  const int first = QsortCompare(a.first, b.first);
  if (first != 0) return first;
  return QsortCompare(a.second, b.second);
}

template <typename T>
constexpr size_t HashPointer(T* p, size_t range) {
  return (((reinterpret_cast<size_t>(p)) >> 4) ^
          ((reinterpret_cast<size_t>(p)) >> 9) ^
          ((reinterpret_cast<size_t>(p)) >> 14)) %
         range;
}

// Compute a+b.
// If the result is greater than MAX, return MAX.
// If the result is less than MIN, return MIN.
template <typename T>
inline T SaturatingAdd(T a, T b) {
  if (a > 0) {
    if (b > std::numeric_limits<T>::max() - a) {
      return std::numeric_limits<T>::max();
    }
  } else if (b < std::numeric_limits<T>::min() - a) {
    return std::numeric_limits<T>::min();
  }
  return a + b;
}

template <
    typename T,
    std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>, int> = 0>
inline T SaturatingMul(T a, T b) {
  if (a == 0 || b == 0) return 0;
  if (b > std::numeric_limits<T>::max() / a) {
    return std::numeric_limits<T>::max();
  }
  return a * b;
}

template <
    typename T,
    std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>, int> = 0>
inline T SaturatingMul(T a, T b) {
  if (a == 0 || b == 0) return 0;
  if (a == std::numeric_limits<T>::min()) {
    // negation is ub
    if (b == -1) return std::numeric_limits<T>::max();
    if (b == 1) return std::numeric_limits<T>::min();
    if (b > 1) return std::numeric_limits<T>::min();
    return std::numeric_limits<T>::max();
  }
  if (b == std::numeric_limits<T>::min()) {
    if (a == -1) return std::numeric_limits<T>::max();
    if (a == 1) return std::numeric_limits<T>::min();
    if (a > 1) return std::numeric_limits<T>::min();
    return std::numeric_limits<T>::max();
  }
  if (a > 0 && b > 0) {
    // both positive
    if (a > std::numeric_limits<T>::max() / b) {
      return std::numeric_limits<T>::max();
    }
  } else if (a < 0 && b < 0) {
    // both negative
    if (a < std::numeric_limits<T>::max() / b) {
      return std::numeric_limits<T>::max();
    }
  } else {
    // one positive, one negative
    if (a > 0) {
      if (b < std::numeric_limits<T>::min() / a) {
        return std::numeric_limits<T>::min();
      }
    } else {
      if (a < std::numeric_limits<T>::min() / b) {
        return std::numeric_limits<T>::min();
      }
    }
  }
  return a * b;
}

inline uint32_t RoundUpToPowerOf2(uint32_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  return v;
}

// Return a value with only the lowest bit left on.
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline uint8_t LowestOneBit(uint8_t x) {
  return x & -x;
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline uint16_t LowestOneBit(uint16_t x) {
  return x & -x;
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline uint32_t LowestOneBit(uint32_t x) {
  return x & -x;
}

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline uint64_t LowestOneBit(uint64_t x) {
  return x & -x;
}

namespace useful_detail {

// Constexpr implementation of std::log for base e.
// This is a simple implementation using a Taylor series expansion and may not
// be as accurate as std::log from <cmath>. It is intended for use in constexpr
// contexts.
// It uses the identity log(y) = 2 * atanh((y-1)/(y+1))
// where atanh(x) = x + x^3/3 + x^5/5 + ...
constexpr double ConstexprLog(double y) {
  if (y < 0) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (y == 0) {
    return -std::numeric_limits<double>::infinity();
  }
  if (y == 1) {
    return 0.0;
  }
  // Bring y into the range [1, 2) to improve convergence.
  // log(y) = log(y / 2^k) + k*log(2)
  int k = 0;
  while (y > 2.0) {
    y /= 2.0;
    k++;
  }
  while (y < 1.0) {
    y *= 2.0;
    k--;
  }
  // Now y is in [1, 2).
  // x = (y-1)/(y+1) is in [0, 1/3).
  // The series will converge quickly.
  double x = (y - 1) / (y + 1);
  double x2 = x * x;
  double term = x;
  double sum = term;
  for (int i = 1; i < 100; ++i) {
    term *= x2;
    double next_sum = sum + term / (2 * i + 1);
    if (next_sum == sum) break;
    sum = next_sum;
  }
  constexpr double kLog2 = 0.693147180559945309417;
  return 2 * sum + k * kLog2;
}

// Constexpr implementation of std::exp.
// This is a simple implementation using a Taylor series expansion and may not
// be as accurate as std::exp from <cmath>. It is intended for use in constexpr
// contexts.
// It uses exp(x) = 1 + x + x^2/2! + x^3/3! + ...
// For better convergence, we use range reduction via exp(x) = (exp(x/2))^2.
constexpr double ConstexprExp(double x) {
  if (x > 2.0 || x < -2.0) {
    const double half = ConstexprExp(x / 2.0);
    return half * half;
  }
  double sum = 1.0;
  double term = 1.0;
  for (int i = 1; i < 30; ++i) {
    term *= x / i;
    double next_sum = sum + term;
    if (next_sum == sum) break;
    sum = next_sum;
  }
  return sum;
}

}  // namespace useful_detail

// Constexpr implementation of std::pow.
// This is a simple implementation and may not be as accurate as std::pow from
// <cmath>. It is intended for use in constexpr contexts.
// Fuzztests exist in useful_fuzztest.cc to test that the constexpr and
// non-constexpr implementations are within an acceptable error bound.
// Replace with std::pow when we move to C++26.
constexpr double ConstexprPow(double base, double exponent) {
  // For simplicity, only handle non-negative bases.
  // std::pow has more complex rules for negative bases.
  if (base < 0) return std::numeric_limits<double>::quiet_NaN();
  if (base == 0) {
    if (exponent > 0) return 0.0;
    if (exponent == 0) return 1.0;
    return std::numeric_limits<double>::infinity();
  }
  if (exponent == 0.0) return 1.0;
  if (exponent == 1.0) return base;
  return useful_detail::ConstexprExp(exponent *
                                     useful_detail::ConstexprLog(base));
}

}  // namespace grpc_core

#define GPR_ARRAY_SIZE(array) (sizeof(array) / sizeof(*(array)))

#endif  // GRPC_SRC_CORE_UTIL_USEFUL_H
