// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_UTIL_MEMORY_USAGE_H
#define GRPC_SRC_CORE_UTIL_MEMORY_USAGE_H

#include <cstddef>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "src/core/util/time.h"

namespace grpc_core {

namespace memory_usage_detail {

template <typename SfinaeVoid, typename T, typename... Args>
struct IsBraceConstructible : public std::false_type {};

template <typename T, typename... Args>
struct IsBraceConstructible<std::void_t<decltype(T{std::declval<Args>()...})>,
                            T, Args...> : public std::true_type {};

template <typename T, typename... Args>
constexpr bool kIsBraceConstructible =
    IsBraceConstructible<void, T, Args...>::value;

template <typename T>
constexpr bool kAnyTypeConvertibleTo = true;
template <typename T>
constexpr bool kAnyTypeConvertibleTo<std::optional<T>> = false;

struct AnyType {
  template <typename T, std::enable_if_t<kAnyTypeConvertibleTo<T>, int> = 0>
  // NOLINTNEXTLINE
  constexpr operator T() {
    LOG(FATAL) << "unreachable";
  }
};

enum class Category {
  kUncategorized,
  kOptional,
  kAbslStatusOr,
  kSimple,        // memory usage == sizeof(T)
  kOwnedPointer,  // memory usage is pointer + usage of pointed to value
  kVector,        // memory usage is container + elements
};

template <typename T, typename = void>
constexpr Category kMemoryUsageCategory = Category::kUncategorized;

template <typename T>
constexpr Category
    kMemoryUsageCategory<T, std::enable_if_t<std::is_fundamental_v<T>>> =
        Category::kSimple;

template <typename T>
constexpr Category
    kMemoryUsageCategory<T, std::enable_if_t<std::is_enum_v<T>>> =
        Category::kSimple;

template <typename T, typename D>
constexpr Category kMemoryUsageCategory<std::unique_ptr<T, D>> =
    Category::kOwnedPointer;

template <typename T>
constexpr Category kMemoryUsageCategory<std::shared_ptr<T>> =
    Category::kOwnedPointer;

template <typename T>
constexpr Category kMemoryUsageCategory<std::vector<T>> = Category::kVector;

template <typename C, typename Traits, typename Allocator>
constexpr Category
    kMemoryUsageCategory<std::basic_string<C, Traits, Allocator>> =
        Category::kVector;

template <typename T>
constexpr Category kMemoryUsageCategory<std::optional<T>> = Category::kOptional;

template <typename T, typename = void>
constexpr bool kHasMemoryUsageMethod = false;

template <typename T>
constexpr bool kHasMemoryUsageMethod<
    T, std::void_t<decltype(std::declval<T>().MemoryUsage())>> = true;

}  // namespace memory_usage_detail

// Given an object x, `MemoryUsage(x)` returns a size_t that approximates the
// memory used by x. It's not totally accurate, but "good enough" for systems
// that need to roughly bound memory usage of a collection of elements.

template <typename T>
size_t MemoryUsageOf(const T& x) {
  using memory_usage_detail::AnyType;
  using memory_usage_detail::Category;
  using memory_usage_detail::kHasMemoryUsageMethod;
  using memory_usage_detail::kIsBraceConstructible;
  constexpr Category category = memory_usage_detail::kMemoryUsageCategory<T>;
  if constexpr (kHasMemoryUsageMethod<T>) {
    return x.MemoryUsage();
  } else if constexpr (std::is_empty_v<T>) {
    return sizeof(T);
  } else if constexpr (std::is_same_v<T, absl::Status>) {
    if (x.ok()) return sizeof(T);
    return 2 * sizeof(T) + x.message().length();  // wrong, but not super wrong
  } else if constexpr (std::is_same_v<T, absl::Time>) {
    return sizeof(T);
  } else if constexpr (std::is_same_v<T, absl::string_view>) {
    // Assume that the string_view is not owning the string.
    return sizeof(T);
  } else if constexpr (std::is_same_v<T, Timestamp>) {
    return sizeof(T);
  } else if constexpr (std::is_same_v<T, Duration>) {
    return sizeof(T);
  } else if constexpr (category == Category::kSimple) {
    return sizeof(T);
  } else if constexpr (category == Category::kOwnedPointer) {
    if (x == nullptr) return sizeof(T);
    return sizeof(T) + MemoryUsageOf(*x);
  } else if constexpr (category == Category::kVector) {
    size_t total = sizeof(T) + sizeof(*x.begin()) * (x.capacity() - x.size());
    for (const auto& e : x) {
      total += MemoryUsageOf(e);
    }
    return total;
  } else if constexpr (category == Category::kOptional) {
    if (x.has_value()) return MemoryUsageOf(*x) + sizeof(T) - sizeof(*x);
    return sizeof(T);
  } else if constexpr (category == Category::kAbslStatusOr) {
    if (x.ok()) return MemoryUsageOf(*x) + sizeof(absl::Status);
    return MemoryUsageOf(x.status()) + sizeof(T);
  } else if constexpr (category == Category::kUncategorized) {
    // Structs!
    // If you have more than 8 fields, you can add more cases here.
    // Keep sorted longest to shortest.
    if constexpr (kIsBraceConstructible<T, AnyType, AnyType, AnyType, AnyType,
                                        AnyType, AnyType, AnyType, AnyType,
                                        AnyType, AnyType>) {
      const auto& [v1, v2, v3, v4, v5, v6, v7, v8, v9, v10] = x;
      constexpr auto padding = sizeof(T) - sizeof(v1) - sizeof(v2) -
                               sizeof(v3) - sizeof(v4) - sizeof(v5) -
                               sizeof(v6) - sizeof(v7) - sizeof(v8) -
                               sizeof(v9) - sizeof(v10);
      static_assert(padding >= 0);
      return MemoryUsageOf(v1) + MemoryUsageOf(v2) + MemoryUsageOf(v3) +
             MemoryUsageOf(v4) + MemoryUsageOf(v5) + MemoryUsageOf(v6) +
             MemoryUsageOf(v7) + MemoryUsageOf(v8) + MemoryUsageOf(v9) +
             MemoryUsageOf(v10) + padding;
    } else if constexpr (kIsBraceConstructible<T, AnyType, AnyType, AnyType,
                                               AnyType, AnyType, AnyType,
                                               AnyType, AnyType, AnyType>) {
      const auto& [v1, v2, v3, v4, v5, v6, v7, v8, v9] = x;
      constexpr auto padding =
          sizeof(T) - sizeof(v1) - sizeof(v2) - sizeof(v3) - sizeof(v4) -
          sizeof(v5) - sizeof(v6) - sizeof(v7) - sizeof(v8) - sizeof(v9);
      static_assert(padding >= 0);
      return MemoryUsageOf(v1) + MemoryUsageOf(v2) + MemoryUsageOf(v3) +
             MemoryUsageOf(v4) + MemoryUsageOf(v5) + MemoryUsageOf(v6) +
             MemoryUsageOf(v7) + MemoryUsageOf(v8) + MemoryUsageOf(v9) +
             padding;
    } else if constexpr (kIsBraceConstructible<T, AnyType, AnyType, AnyType,
                                               AnyType, AnyType, AnyType,
                                               AnyType, AnyType>) {
      const auto& [v1, v2, v3, v4, v5, v6, v7, v8] = x;
      constexpr auto padding = sizeof(T) - sizeof(v1) - sizeof(v2) -
                               sizeof(v3) - sizeof(v4) - sizeof(v5) -
                               sizeof(v6) - sizeof(v7) - sizeof(v8);
      static_assert(padding >= 0);
      return MemoryUsageOf(v1) + MemoryUsageOf(v2) + MemoryUsageOf(v3) +
             MemoryUsageOf(v4) + MemoryUsageOf(v5) + MemoryUsageOf(v6) +
             MemoryUsageOf(v7) + MemoryUsageOf(v8) + padding;
    } else if constexpr (kIsBraceConstructible<T, AnyType, AnyType, AnyType,
                                               AnyType, AnyType, AnyType,
                                               AnyType>) {
      const auto& [v1, v2, v3, v4, v5, v6, v7] = x;
      constexpr auto padding = sizeof(T) - sizeof(v1) - sizeof(v2) -
                               sizeof(v3) - sizeof(v4) - sizeof(v5) -
                               sizeof(v6) - sizeof(v7);
      static_assert(padding >= 0);
      return MemoryUsageOf(v1) + MemoryUsageOf(v2) + MemoryUsageOf(v3) +
             MemoryUsageOf(v4) + MemoryUsageOf(v5) + MemoryUsageOf(v6) +
             MemoryUsageOf(v7) + padding;
    } else if constexpr (kIsBraceConstructible<T, AnyType, AnyType, AnyType,
                                               AnyType, AnyType, AnyType>) {
      const auto& [v1, v2, v3, v4, v5, v6] = x;
      constexpr auto padding = sizeof(T) - sizeof(v1) - sizeof(v2) -
                               sizeof(v3) - sizeof(v4) - sizeof(v5) -
                               sizeof(v6);
      static_assert(padding >= 0);
      return MemoryUsageOf(v1) + MemoryUsageOf(v2) + MemoryUsageOf(v3) +
             MemoryUsageOf(v4) + MemoryUsageOf(v5) + MemoryUsageOf(v6) +
             padding;
    } else if constexpr (kIsBraceConstructible<T, AnyType, AnyType, AnyType,
                                               AnyType, AnyType>) {
      const auto& [v1, v2, v3, v4, v5] = x;
      constexpr auto padding = sizeof(T) - sizeof(v1) - sizeof(v2) -
                               sizeof(v3) - sizeof(v4) - sizeof(v5);
      static_assert(padding >= 0);
      return MemoryUsageOf(v1) + MemoryUsageOf(v2) + MemoryUsageOf(v3) +
             MemoryUsageOf(v4) + MemoryUsageOf(v5) + padding;
    } else if constexpr (kIsBraceConstructible<T, AnyType, AnyType, AnyType,
                                               AnyType>) {
      const auto& [v1, v2, v3, v4] = x;
      constexpr auto padding =
          sizeof(T) - sizeof(v1) - sizeof(v2) - sizeof(v3) - sizeof(v4);
      static_assert(padding >= 0);
      return MemoryUsageOf(v1) + MemoryUsageOf(v2) + MemoryUsageOf(v3) +
             MemoryUsageOf(v4) + padding;
    } else if constexpr (kIsBraceConstructible<T, AnyType, AnyType, AnyType>) {
      const auto& [v1, v2, v3] = x;
      constexpr auto padding = sizeof(T) - sizeof(v1) - sizeof(v2) - sizeof(v3);
      static_assert(padding >= 0);
      return MemoryUsageOf(v1) + MemoryUsageOf(v2) + MemoryUsageOf(v3) +
             padding;
    } else if constexpr (kIsBraceConstructible<T, AnyType, AnyType>) {
      const auto& [v1, v2] = x;
      constexpr auto padding = sizeof(T) - sizeof(v1) - sizeof(v2);
      static_assert(padding >= 0);
      return MemoryUsageOf(v1) + MemoryUsageOf(v2) + padding;
    } else if constexpr (kIsBraceConstructible<T, AnyType>) {
      const auto& [v1] = x;
      constexpr auto padding = sizeof(T) - sizeof(v1);
      static_assert(padding >= 0);
      return MemoryUsageOf(v1) + padding;
    } else {
      // Probably wrong, but we've not figured any better...
      LOG(DFATAL) << "Unsupported type";
      return sizeof(T);
    }
  } else {
    // Probably wrong, but we've not figured any better...
    LOG(DFATAL) << "Unsupported type";
    return sizeof(T);
  }
}

template <typename... Args>
size_t MemoryUsageOf(const std::tuple<Args...>& t) {
  return std::apply(
      [](const auto&... args) { return (MemoryUsageOf(args) + ... + 0); }, t);
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_UTIL_MEMORY_USAGE_H
