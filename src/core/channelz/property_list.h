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

#ifndef GRPC_SRC_CORE_CHANNELZ_PROPERTY_LIST_H
#define GRPC_SRC_CORE_CHANNELZ_PROPERTY_LIST_H

#include <cstddef>
#include <type_traits>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.upb.h"
#include "src/core/util/json/json.h"
#include "src/core/util/string.h"
#include "src/core/util/time.h"
#include "src/core/util/upb_utils.h"
#include "src/proto/grpc/channelz/v2/channelz.upb.h"
#include "src/proto/grpc/channelz/v2/property_list.upb.h"
#include "upb/mem/arena.h"

namespace grpc_core::channelz {

class PropertyList;
class PropertyGrid;
class PropertyTable;

using PropertyValue =
    std::variant<absl::string_view, std::string, int64_t, uint64_t, double,
                 bool, Duration, Timestamp, absl::Status,
                 std::shared_ptr<PropertyList>, std::shared_ptr<PropertyGrid>,
                 std::shared_ptr<PropertyTable>>;

namespace property_list_detail {

template <typename T, typename = void>
struct Wrapper {
  static std::optional<PropertyValue> Wrap(T value) {
    return PropertyValue(std::move(value));
  }
};

template <typename T>
struct Wrapper<
    T, std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>>> {
  static std::optional<PropertyValue> Wrap(T value) {
    return PropertyValue(static_cast<uint64_t>(value));
  }
};

template <typename T>
struct Wrapper<T,
               std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>>> {
  static std::optional<PropertyValue> Wrap(T value) {
    return PropertyValue(static_cast<int64_t>(value));
  }
};

template <typename T>
struct Wrapper<std::optional<T>> {
  static std::optional<PropertyValue> Wrap(std::optional<T> value) {
    if (value.has_value()) return Wrapper<T>::Wrap(*std::move(value));
    return std::nullopt;
  }
};

template <>
struct Wrapper<const char*> {
  static std::optional<PropertyValue> Wrap(const char* value) {
    if (value == nullptr) return std::nullopt;
    return absl::string_view(value);
  }
};

template <>
struct Wrapper<bool> {
  static std::optional<PropertyValue> Wrap(bool value) {
    return PropertyValue(value);
  }
};

}  // namespace property_list_detail

// PropertyList contains a bag of key->value (for mostly arbitrary value
// types) for reporting out state from channelz - the big idea is that you
// should be able to call `PropertyList().Set("a", this->a)` and generate
// something that channelz presenters can interpret. It's currently defined in
// terms of JSON, but as we adopt channelz-v2 it's expected we'll change this
// to capture protobuf directly.
class PropertyList {
 public:
  template <typename T>
  PropertyList& Set(absl::string_view key, T value) {
    SetInternal(key, property_list_detail::Wrapper<T>::Wrap(value));
    return *this;
  }

  PropertyList& Merge(PropertyList other);

  // TODO(ctiller): remove soon, switch to just FillUpbProto.
  Json::Object TakeJsonObject();
  void FillUpbProto(grpc_channelz_v2_PropertyList* proto, upb_Arena* arena);
  void FillAny(google_protobuf_Any* any, upb_Arena* arena);

 private:
  void SetInternal(absl::string_view key, std::optional<PropertyValue> value);

  friend class PropertyGrid;
  friend class PropertyTable;

  absl::flat_hash_map<std::string, PropertyValue> property_list_;
};

namespace property_list_detail {

template <>
struct Wrapper<PropertyList> {
  static std::optional<PropertyValue> Wrap(PropertyList value) {
    return PropertyValue(std::make_shared<PropertyList>(std::move(value)));
  }
};

};  // namespace property_list_detail

// PropertyGrid is much the same as PropertyList, but it is two dimensional.
// Each row and column can be set independently.
// Rows and columns are ordered by the first setting of a value on them.
class PropertyGrid {
 public:
  template <typename T>
  PropertyGrid& Set(absl::string_view column, absl::string_view row, T value) {
    SetInternal(column, row, property_list_detail::Wrapper<T>::Wrap(value));
    return *this;
  }

  PropertyGrid& SetColumn(absl::string_view column, PropertyList values);
  PropertyGrid& SetRow(absl::string_view row, PropertyList values);

  Json::Object TakeJsonObject();
  void FillUpbProto(grpc_channelz_v2_PropertyGrid* proto, upb_Arena* arena);
  void FillAny(google_protobuf_Any* any, upb_Arena* arena);

 private:
  void SetInternal(absl::string_view column, absl::string_view row,
                   std::optional<PropertyValue> value);

  std::vector<std::string> columns_;
  std::vector<std::string> rows_;
  absl::flat_hash_map<std::pair<size_t, size_t>, PropertyValue> grid_;
};

namespace property_list_detail {

template <>
struct Wrapper<PropertyGrid> {
  static std::optional<PropertyValue> Wrap(PropertyGrid value) {
    return PropertyValue(std::make_shared<PropertyGrid>(std::move(value)));
  }
};

};  // namespace property_list_detail

// PropertyTable is much the same as PropertyGrid, but has numbered rows
// instead of named rows.
class PropertyTable {
 public:
  template <typename T>
  PropertyTable& Set(absl::string_view column, size_t row, T value) {
    SetInternal(column, row, property_list_detail::Wrapper<T>::Wrap(value));
    return *this;
  }

  PropertyTable& SetRow(size_t row, PropertyList values);
  PropertyTable& AppendRow(PropertyList values) {
    return SetRow(num_rows_, std::move(values));
  }

  Json::Object TakeJsonObject();
  void FillUpbProto(grpc_channelz_v2_PropertyTable* proto, upb_Arena* arena);
  void FillAny(google_protobuf_Any* any, upb_Arena* arena);

 private:
  void SetInternal(absl::string_view column, size_t row,
                   std::optional<PropertyValue> value);

  std::vector<std::string> columns_;
  size_t num_rows_ = 0;
  absl::flat_hash_map<std::pair<size_t, size_t>, PropertyValue> grid_;
};

namespace property_list_detail {

template <>
struct Wrapper<PropertyTable> {
  static std::optional<PropertyValue> Wrap(PropertyTable value) {
    return PropertyValue(std::make_shared<PropertyTable>(std::move(value)));
  }
};

};  // namespace property_list_detail

}  // namespace grpc_core::channelz

#endif  // GRPC_SRC_CORE_CHANNELZ_PROPERTY_LIST_H
