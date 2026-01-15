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

#include <algorithm>
#include <vector>

#include "google/protobuf/any.upb.h"
#include "google/protobuf/empty.upb.h"
#include "src/core/util/match.h"
#include "src/proto/grpc/channelz/v2/property_list.upb.h"

namespace grpc_core::channelz {

namespace {
size_t GetIndex(std::vector<std::string>& vec, absl::string_view value) {
  auto it = std::find(vec.begin(), vec.end(), value);
  if (it == vec.end()) {
    vec.emplace_back(value);
    return vec.size() - 1;
  } else {
    return it - vec.begin();
  }
}

void FillUpbValue(const PropertyValue& value,
                  grpc_channelz_v2_PropertyValue* proto, upb_Arena* arena) {
  Match(
      value,
      [proto](absl::string_view v) {
        grpc_channelz_v2_PropertyValue_set_string_value(
            proto, StdStringToUpbString(v));
      },
      [proto, arena](const std::string& v) {
        grpc_channelz_v2_PropertyValue_set_string_value(
            proto, CopyStdStringToUpbString(v, arena));
      },
      [proto](int64_t v) {
        grpc_channelz_v2_PropertyValue_set_int64_value(proto, v);
      },
      [proto](uint64_t v) {
        grpc_channelz_v2_PropertyValue_set_uint64_value(proto, v);
      },
      [proto](double v) {
        grpc_channelz_v2_PropertyValue_set_double_value(proto, v);
      },
      [proto](bool v) {
        grpc_channelz_v2_PropertyValue_set_bool_value(proto, v);
      },
      [proto, arena](Duration v) {
        auto* duration_proto =
            grpc_channelz_v2_PropertyValue_mutable_duration_value(proto, arena);
        auto ts = v.as_timespec();
        google_protobuf_Duration_set_seconds(duration_proto, ts.tv_sec);
        google_protobuf_Duration_set_nanos(duration_proto, ts.tv_nsec);
      },
      [proto, arena](Timestamp v) {
        auto* timestamp_proto =
            grpc_channelz_v2_PropertyValue_mutable_timestamp_value(proto,
                                                                   arena);
        auto ts = v.as_timespec(GPR_CLOCK_REALTIME);
        google_protobuf_Timestamp_set_seconds(timestamp_proto, ts.tv_sec);
        google_protobuf_Timestamp_set_nanos(timestamp_proto, ts.tv_nsec);
        grpc_channelz_v2_PropertyValue_set_timestamp_value(proto,
                                                           timestamp_proto);
      },
      [proto, arena](absl::Time v) {
        auto* timestamp_proto =
            grpc_channelz_v2_PropertyValue_mutable_timestamp_value(proto,
                                                                   arena);
        auto ts = absl::ToTimespec(v);
        google_protobuf_Timestamp_set_seconds(timestamp_proto, ts.tv_sec);
        google_protobuf_Timestamp_set_nanos(timestamp_proto, ts.tv_nsec);
        grpc_channelz_v2_PropertyValue_set_timestamp_value(proto,
                                                           timestamp_proto);
      },
      [proto, arena](absl::Status v) {
        std::string text = v.ToString();
        grpc_channelz_v2_PropertyValue_set_string_value(
            proto, CopyStdStringToUpbString(text, arena));
      },
      [proto, arena](std::shared_ptr<OtherPropertyValue> v) {
        auto* any =
            grpc_channelz_v2_PropertyValue_mutable_any_value(proto, arena);
        v->FillAny(any, arena);
      });
}

grpc_channelz_v2_PropertyValue* ToUpbProto(const PropertyValue& value,
                                           upb_Arena* arena) {
  auto* proto = grpc_channelz_v2_PropertyValue_new(arena);
  FillUpbValue(value, proto, arena);
  return proto;
}
}  // namespace

void PropertyList::SetInternal(absl::string_view key,
                               std::optional<PropertyValue> value) {
  if (value.has_value()) {
    property_list_.emplace_back(std::string(key), *std::move(value));
  }
}

PropertyList& PropertyList::Merge(PropertyList other) {
  for (auto& [key, value] : other.property_list_) {
    SetInternal(key, std::move(value));
  }
  return *this;
}

void PropertyList::FillUpbProto(grpc_channelz_v2_PropertyList* proto,
                                upb_Arena* arena) {
  auto* elements = grpc_channelz_v2_PropertyList_resize_properties(
      proto, property_list_.size(), arena);
  size_t i = 0;
  for (auto& [key, value] : property_list_) {
    auto* element = grpc_channelz_v2_PropertyList_Element_new(arena);
    grpc_channelz_v2_PropertyList_Element_set_key(
        element, CopyStdStringToUpbString(key, arena));
    grpc_channelz_v2_PropertyList_Element_set_value(element,
                                                    ToUpbProto(value, arena));
    elements[i] = element;
    i++;
  }
}

void PropertyList::FillAny(google_protobuf_Any* any, upb_Arena* arena) {
  auto* p = grpc_channelz_v2_PropertyList_new(arena);
  FillUpbProto(p, arena);
  size_t length;
  auto* bytes = grpc_channelz_v2_PropertyList_serialize(p, arena, &length);
  google_protobuf_Any_set_value(any,
                                upb_StringView_FromDataAndSize(bytes, length));
  google_protobuf_Any_set_type_url(
      any, StdStringToUpbString(
               "type.googleapis.com/grpc.channelz.v2.PropertyList"));
}

void PropertyGrid::FillUpbProto(grpc_channelz_v2_PropertyGrid* proto,
                                upb_Arena* arena) {
  auto* columns_proto = grpc_channelz_v2_PropertyGrid_resize_columns(
      proto, columns_.size(), arena);
  for (size_t i = 0; i < columns_.size(); ++i) {
    columns_proto[i] = StdStringToUpbString(columns_[i]);
  }
  auto* rows_proto =
      grpc_channelz_v2_PropertyGrid_resize_rows(proto, rows_.size(), arena);
  for (size_t r = 0; r < rows_.size(); ++r) {
    auto* row_proto = grpc_channelz_v2_PropertyGrid_Row_new(arena);
    rows_proto[r] = row_proto;
    grpc_channelz_v2_PropertyGrid_Row_set_label(row_proto,
                                                StdStringToUpbString(rows_[r]));
    auto* row_columns_proto = grpc_channelz_v2_PropertyGrid_Row_resize_value(
        row_proto, columns_.size(), arena);
    for (size_t c = 0; c < columns_.size(); ++c) {
      auto it = grid_.find(std::pair(c, r));
      if (it != grid_.end()) {
        row_columns_proto[c] = ToUpbProto(it->second, arena);
      } else {
        auto* val = grpc_channelz_v2_PropertyValue_new(arena);
        grpc_channelz_v2_PropertyValue_set_empty_value(
            val, google_protobuf_Empty_new(arena));
        row_columns_proto[c] = val;
      }
    }
  }
}

void PropertyGrid::FillAny(google_protobuf_Any* any, upb_Arena* arena) {
  auto* p = grpc_channelz_v2_PropertyGrid_new(arena);
  FillUpbProto(p, arena);
  size_t length;
  auto* bytes = grpc_channelz_v2_PropertyGrid_serialize(p, arena, &length);
  google_protobuf_Any_set_value(any,
                                upb_StringView_FromDataAndSize(bytes, length));
  google_protobuf_Any_set_type_url(
      any, StdStringToUpbString(
               "type.googleapis.com/grpc.channelz.v2.PropertyGrid"));
}

void PropertyGrid::SetInternal(absl::string_view column, absl::string_view row,
                               std::optional<PropertyValue> value) {
  int c = GetIndex(columns_, column);
  int r = GetIndex(rows_, row);
  if (value.has_value()) {
    grid_.emplace(std::pair(c, r), *std::move(value));
  } else {
    grid_.erase(std::pair(c, r));
  }
}

PropertyGrid& PropertyGrid::SetColumn(absl::string_view column,
                                      PropertyList values) {
  int c = GetIndex(columns_, column);
  for (auto& [key, value] : values.property_list_) {
    grid_.emplace(std::pair(c, GetIndex(rows_, std::move(key))),
                  std::move(value));
  }
  return *this;
}

PropertyGrid& PropertyGrid::SetRow(absl::string_view row, PropertyList values) {
  int r = GetIndex(rows_, row);
  for (auto& [key, value] : values.property_list_) {
    grid_.emplace(std::pair(GetIndex(columns_, std::move(key)), r),
                  std::move(value));
  }
  return *this;
}

void PropertyTable::SetInternal(absl::string_view column, size_t row,
                                std::optional<PropertyValue> value) {
  int c = GetIndex(columns_, column);
  num_rows_ = std::max(num_rows_, row + 1);
  if (value.has_value()) {
    grid_.emplace(std::pair(c, row), *std::move(value));
  } else {
    grid_.erase(std::pair(c, row));
  }
}

PropertyTable& PropertyTable::SetRow(size_t row, PropertyList values) {
  num_rows_ = std::max(num_rows_, row + 1);
  for (auto& [key, value] : values.property_list_) {
    grid_.emplace(std::pair(GetIndex(columns_, key), row), std::move(value));
  }
  return *this;
}

void PropertyTable::FillUpbProto(grpc_channelz_v2_PropertyTable* proto,
                                 upb_Arena* arena) {
  auto* columns_proto = grpc_channelz_v2_PropertyTable_resize_columns(
      proto, columns_.size(), arena);
  for (size_t i = 0; i < columns_.size(); ++i) {
    columns_proto[i] = StdStringToUpbString(columns_[i]);
  }
  auto* rows_proto =
      grpc_channelz_v2_PropertyTable_resize_rows(proto, num_rows_, arena);
  for (size_t r = 0; r < num_rows_; ++r) {
    auto* row_proto = grpc_channelz_v2_PropertyTable_Row_new(arena);
    rows_proto[r] = row_proto;
    auto* row_columns_proto = grpc_channelz_v2_PropertyTable_Row_resize_value(
        row_proto, columns_.size(), arena);
    for (size_t c = 0; c < columns_.size(); ++c) {
      auto it = grid_.find({c, r});
      if (it != grid_.end()) {
        row_columns_proto[c] = ToUpbProto(it->second, arena);
      } else {
        auto* val = grpc_channelz_v2_PropertyValue_new(arena);
        grpc_channelz_v2_PropertyValue_set_empty_value(
            val, google_protobuf_Empty_new(arena));
        row_columns_proto[c] = val;
      }
    }
  }
}

void PropertyTable::FillAny(google_protobuf_Any* any, upb_Arena* arena) {
  auto* p = grpc_channelz_v2_PropertyTable_new(arena);
  FillUpbProto(p, arena);
  size_t length;
  auto* bytes = grpc_channelz_v2_PropertyTable_serialize(p, arena, &length);
  google_protobuf_Any_set_value(any,
                                upb_StringView_FromDataAndSize(bytes, length));
  google_protobuf_Any_set_type_url(
      any, StdStringToUpbString(
               "type.googleapis.com/grpc.channelz.v2.PropertyTable"));
}

}  // namespace grpc_core::channelz
