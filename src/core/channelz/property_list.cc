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

#include "src/core/channelz/property_list.h"

namespace grpc_core::channelz {

void PropertyList::SetInternal(absl::string_view key,
                               std::optional<Json> value) {
  if (value.has_value()) {
    property_list_.emplace(key, *std::move(value));
  } else {
    property_list_.erase(std::string(key));
  }
}

size_t PropertyGrid::GetIndex(std::vector<std::string>& vec,
                              absl::string_view value) {
  auto it = std::find(vec.begin(), vec.end(), value);
  if (it == vec.end()) {
    vec.emplace_back(value);
    return vec.size() - 1;
  } else {
    return it - vec.begin();
  }
}

Json::Object PropertyGrid::TakeJsonObject() {
  Json::Object json;
  Json::Array columns;
  for (auto& c : columns_) {
    columns.emplace_back(Json::FromString(std::string(c)));
  }
  json.emplace("columns", Json::FromArray(std::move(columns)));
  Json::Array rows;
  for (size_t r = 0; r < rows_.size(); ++r) {
    Json::Object row;
    row.emplace("name", Json::FromString(std::string(rows_[r])));
    Json::Array cells;
    cells.reserve(columns_.size());
    for (size_t c = 0; c < columns_.size(); ++c) {
      auto it = grid_.find(std::pair(c, r));
      if (it != grid_.end()) {
        cells.emplace_back(std::move(it->second));
      } else {
        cells.emplace_back(Json());
      }
    }
    row.emplace("cells", Json::FromArray(std::move(cells)));
    rows.emplace_back(Json::FromObject(std::move(row)));
  }
  json.emplace("rows", Json::FromArray(std::move(rows)));
  return json;
}

void PropertyGrid::SetInternal(absl::string_view column, absl::string_view row,
                               std::optional<Json> value) {
  int c = GetIndex(columns_, column);
  int r = GetIndex(rows_, row);
  if (value.has_value()) {
    grid_[std::pair(c, r)] = *std::move(value);
  } else {
    grid_.erase(std::pair(c, r));
  }
}

PropertyGrid& PropertyGrid::SetColumn(absl::string_view column,
                                      PropertyList values) {
  int c = GetIndex(columns_, column);
  for (auto& [key, value] : values.TakeJsonObject()) {
    grid_[std::pair(c, GetIndex(rows_, key))] = std::move(value);
  }
  return *this;
}

PropertyGrid& PropertyGrid::SetRow(absl::string_view row, PropertyList values) {
  int r = GetIndex(rows_, row);
  for (auto& [key, value] : values.TakeJsonObject()) {
    grid_[std::pair(GetIndex(columns_, key), r)] = std::move(value);
  }
  return *this;
}

}  // namespace grpc_core::channelz
