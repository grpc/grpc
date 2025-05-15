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

#ifndef GRPC_SRC_CORE_UTIL_FUNCTION_SIGNATURE_H
#define GRPC_SRC_CORE_UTIL_FUNCTION_SIGNATURE_H

#include "absl/strings/string_view.h"

#if defined(_MSC_VER)
#define GRPC_FUNCTION_SIGNATURE __FUNCSIG__
#elif defined(__GNUC__)
#define GRPC_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#else
#define GRPC_FUNCTION_SIGNATURE "???()"
#endif

namespace grpc_core {

template <typename T>
static constexpr inline absl::string_view TypeName() {
#if defined(__clang__)
  constexpr auto prefix = absl::string_view{"[T = "};
  constexpr auto suffix = absl::string_view{"]"};
#elif defined(__GNUC__)
  constexpr auto prefix = absl::string_view{"[with T = "};
  constexpr auto suffix = absl::string_view{";"};
#elif defined(_MSC_VER)
  constexpr auto prefix = absl::string_view{"TypeName<"};
  constexpr auto suffix = absl::string_view{">(void)"};
#else
  return "unknown";
#endif
  constexpr auto function = std::string_view{GRPC_FUNCTION_SIGNATURE};
  constexpr auto start = function.find(prefix) + prefix.size();
  constexpr auto end = function.rfind(suffix);
  static_assert(start < end);
  return function.substr(start, (end - start));
}

}  // namespace grpc_core

#endif
