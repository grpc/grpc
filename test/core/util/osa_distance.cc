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

namespace {
class Matrix {
 public:
  Matrix(size_t width, size_t height)
      : width_(width),
        data_(width * height, std::numeric_limits<size_t>::max()) {}

  size_t& operator()(size_t x, size_t y) { return data_[y * width_ + x]; }

 private:
  size_t width_;
  std::vector<size_t> data_;
};
}  // namespace

size_t OsaDistance(absl::string_view s1, absl::string_view s2) {
  if (s1.size() > s2.size()) std::swap(s1, s2);
  if (s1.empty()) return static_cast<uint8_t>(s2.size());

  Matrix d(s1.size() + 1, s2.size() + 1);
  d(0, 0) = 0;
  for (size_t i = 1; i <= s1.size(); ++i) {
    d(i, 0) = i;
  }
  for (size_t j = 1; j <= s2.size(); ++j) {
    d(0, j) = j;
  }
  for (size_t i = 1; i <= s1.size(); ++i) {
    for (size_t j = 1; j <= s2.size(); ++j) {
      const size_t cost = s1[i - 1] == s2[j - 1] ? 0 : 1;
      d(i, j) = std::min({
          d(i - 1, j) + 1,        // deletion
          d(i, j - 1) + 1,        // insertion
          d(i - 1, j - 1) + cost  // substitution
      });
      if (i > 1 && j > 1 && s1[i - 1] == s2[j - 2] && s1[i - 2] == s2[j - 1]) {
        d(i, j) = std::min(d(i, j), d(i - 2, j - 2) + 1);  // transposition
      }
    }
  }
  return d(s1.size(), s2.size());
}

}  // namespace grpc_core
