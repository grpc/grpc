// Copyright 2024 The gRPC Authors
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

#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {
bool IsGlob(absl::string_view trace_glob) {
  return std::any_of(trace_glob.begin(), trace_glob.end(),
                     [](const char c) { return c == '?' || c == '*'; });
}
}  // namespace

bool GlobMatch(absl::string_view name, absl::string_view pattern) {
  if (!IsGlob(pattern)) return name == pattern;
  size_t name_idx = 0;
  size_t trace_idx = 0;
  // pointers for iterative wildcard * matching.
  size_t name_next_idx = name_idx;
  size_t trace_next_idx = trace_idx;

  while (trace_idx < pattern.length() || name_idx < name.length()) {
    if (trace_idx < pattern.length()) {
      switch (pattern.at(trace_idx)) {
        case '?':
          if (name_idx < name.length()) {
            ++trace_idx;
            ++name_idx;
            continue;
          }
          break;
        case '*':
          trace_next_idx = trace_idx;
          name_next_idx = name_idx + 1;
          ++trace_idx;
          continue;
        default:
          if (name_idx < name.length() &&
              name.at(name_idx) == pattern.at(trace_idx)) {
            ++trace_idx;
            ++name_idx;
            continue;
          }
          break;
      }
    }
    // Failed to match a character. Restart if possible.
    if (name_next_idx > 0 && name_next_idx <= name.length()) {
      trace_idx = trace_next_idx;
      name_idx = name_next_idx;
      continue;
    }
    return false;
  }
  return true;
}

}  // namespace grpc_core
