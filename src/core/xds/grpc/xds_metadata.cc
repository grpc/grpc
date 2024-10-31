//
// Copyright 2024 gRPC authors.
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
//

#include "src/core/xds/grpc/xds_metadata.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

void XdsMetadataMap::Insert(absl::string_view key,
                            std::unique_ptr<XdsMetadataValue> value) {
  CHECK(value != nullptr);
  CHECK(map_.emplace(key, std::move(value)).second) << "duplicate key: " << key;
}

const XdsMetadataValue* XdsMetadataMap::Find(absl::string_view key) const {
  auto it = map_.find(key);
  if (it == map_.end()) return nullptr;
  return it->second.get();
}

bool XdsMetadataMap::operator==(const XdsMetadataMap& other) const {
  if (map_.size() != other.map_.size()) return false;
  for (const auto& p : map_) {
    auto it = other.map_.find(p.first);
    if (it == other.map_.end()) return false;
    if (*p.second != *it->second) return false;
  }
  return true;
}

std::string XdsMetadataMap::ToString() const {
  std::vector<std::string> entries;
  for (const auto& p : map_) {
    entries.push_back(absl::StrCat(p.first, "=", p.second->ToString()));
  }
  std::sort(entries.begin(), entries.end());
  return absl::StrCat("{", absl::StrJoin(entries, ", "), "}");
}

}  // namespace grpc_core
