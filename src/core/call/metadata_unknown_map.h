// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CALL_METADATA_UNKNOWN_MAP_H
#define GRPC_SRC_CORE_CALL_METADATA_UNKNOWN_MAP_H

#include <grpc/support/port_platform.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "src/core/lib/slice/slice.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace metadata_detail {

class UnknownMap {
 public:
  void Append(absl::string_view key, Slice value);
  void Remove(absl::string_view key);
  std::optional<absl::string_view> GetStringValue(absl::string_view key, std::string* buffer) const;
  void Clear();
  bool empty() const { return unknown_.empty(); }
  size_t size() const { return unknown_.size(); }

  std::vector<std::pair<Slice, Slice>>& unknown() { return unknown_; }
  const std::vector<std::pair<Slice, Slice>>& unknown() const { return unknown_; }

 private:
  std::vector<std::pair<Slice, Slice>> unknown_;
};

}  // namespace metadata_detail
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CALL_METADATA_UNKNOWN_MAP_H
