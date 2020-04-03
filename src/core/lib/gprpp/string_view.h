/*
 *
 * Copyright 2019 gRPC authors.
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
#ifndef GRPC_CORE_LIB_GPRPP_STRING_VIEW_H
#define GRPC_CORE_LIB_GPRPP_STRING_VIEW_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include "absl/strings/string_view.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"

namespace grpc_core {

using StringView = absl::string_view;

// Converts grpc_slice to StringView.
inline absl::string_view StringViewFromSlice(const grpc_slice& slice) {
  return absl::string_view(
      reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(slice)),
      GRPC_SLICE_LENGTH(slice));
}

// Creates a dup of the string viewed by this class.
// Return value is null-terminated and never nullptr.
inline grpc_core::UniquePtr<char> StringViewToCString(const StringView sv) {
  char* str = static_cast<char*>(gpr_malloc(sv.size() + 1));
  if (sv.size() > 0) memcpy(str, sv.data(), sv.size());
  str[sv.size()] = '\0';
  return grpc_core::UniquePtr<char>(str);
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_STRING_VIEW_H */
