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

// Macros defined:
// GRPC_FUNCTION_SIGNATURE
//   - a c-string variable that contains a reasonable rendering of the function
//     name
// GRPC_FUNCTION_SIGNATURE_CAPTURES_LAMBDA_FILENAMES
//   - a bool that indicates if a lambda is captured, will it contain the
//     filename - this can be used for tests that want to check the filename
//     is there on some platforms

#if defined(_MSC_VER)
#define GRPC_FUNCTION_SIGNATURE __FUNCSIG__
#elif defined(__GNUC__)
#define GRPC_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#else
#define GRPC_FUNCTION_SIGNATURE "???()"
#endif

namespace grpc_core {

// Debug helper function to extract a string type name from a C++ type.
// This is absolutely best effort and certainly doesn't actually work on some
// platforms. Do not use this for actual functionality, but it's super useful
// for exporting debug/trace information.
template <typename T>
static constexpr inline absl::string_view TypeName() {
#if ABSL_USES_STD_STRING_VIEW
  // absl::string_view doesn't have the constexpr find methods we need
  // here.
#if defined(__clang__)
  constexpr absl::string_view kPrefix{"[T = "};
  constexpr absl::string_view kSuffix{"]"};
#elif defined(__GNUC__)
#if __GNUC__ < 9
#define GRPC_FUNCTION_SIGNATURE_TYPE_NAME_USE_FALLBACK
#endif
  constexpr absl::string_view kPrefix{"[with T = "};
  constexpr absl::string_view kSuffix{";"};
#elif defined(_MSC_VER)
  constexpr absl::string_view kPrefix{"TypeName<"};
  constexpr absl::string_view kSuffix{">(void)"};
#else
#define GRPC_FUNCTION_SIGNATURE_TYPE_NAME_USE_FALLBACK
#endif
#else  // !ABSL_USE_STD_STRING_VIEW
#define GRPC_FUNCTION_SIGNATURE_TYPE_NAME_USE_FALLBACK
#endif

#ifdef GRPC_FUNCTION_SIGNATURE_TYPE_NAME_USE_FALLBACK
  return "unknown";
#else
  constexpr absl::string_view kFunction{GRPC_FUNCTION_SIGNATURE};
  constexpr size_t kStart = kFunction.find(kPrefix) + kPrefix.size();
  constexpr size_t kEnd = kFunction.rfind(kSuffix);
  static_assert(kStart < kEnd);
  return kFunction.substr(kStart, (kEnd - kStart));
#endif
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_UTIL_FUNCTION_SIGNATURE_H
