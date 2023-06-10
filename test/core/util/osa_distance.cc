// Copyright 2023 gRPC authors.
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

#include "test/core/util/osa_distance.h"

#include <stdint.h>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace grpc_core {

size_t OsaDistance(absl::string_view s1, absl::string_view s2) {
  if (s1.size() > s2.size()) std::swap(s1, s2);
  if (s1.empty()) return static_cast<uint8_t>(s2.size());

  const auto width = s1.size() + 1;
  const auto height = s2.size() + 1;
  std::vector<size_t> matrix(width * height,
                             std::numeric_limits<size_t>::max());
#define MATRIX_CELL(x, y) matrix[(y)*width + (x)]

  MATRIX_CELL(0, 0) = 0;
  for (size_t i = 1; i <= s1.size(); ++i) {
    MATRIX_CELL(i, 0) = i;
  }
  for (size_t j = 1; j <= s2.size(); ++j) {
    MATRIX_CELL(0, j) = j;
  }
  for (size_t i = 1; i <= s1.size(); ++i) {
    for (size_t j = 1; j <= s2.size(); ++j) {
      const size_t cost = s1[i - 1] == s2[j - 1] ? 0 : 1;
      MATRIX_CELL(i, j) = std::min({
          MATRIX_CELL(i - 1, j) + 1,        // deletion
          MATRIX_CELL(i, j - 1) + 1,        // insertion
          MATRIX_CELL(i - 1, j - 1) + cost  // substitution
      });
      if (i > 1 && j > 1 && s1[i - 1] == s2[j - 2] && s1[i - 2] == s2[j - 1]) {
        MATRIX_CELL(i, j) = std::min(
            MATRIX_CELL(i, j), MATRIX_CELL(i - 2, j - 2) + 1);  // transposition
      }
    }
  }
  return MATRIX_CELL(s1.size(), s2.size());
}

}  // namespace grpc_core
