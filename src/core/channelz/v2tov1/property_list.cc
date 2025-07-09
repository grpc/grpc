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

#include "src/core/channelz/v2tov1/property_list.h"

#include <cstdint>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "src/proto/grpc/channelz/v2/property_list.upb.h"
#include "upb/mem/arena.hpp"

namespace grpc_core {
namespace channelz {
namespace v2tov1 {

namespace {

const grpc_channelz_v2_PropertyValue* FindProperty(
    const grpc_channelz_v2_PropertyList* pl, absl::string_view name) {
  if (pl == nullptr) return nullptr;
  size_t iter = kUpb_Map_Begin;
  upb_StringView key;
  const grpc_channelz_v2_PropertyValue* value;
  while (
      grpc_channelz_v2_PropertyList_properties_next(pl, &key, &value, &iter)) {
    if (absl::string_view(key.data, key.size) == name) {
      return value;
    }
  }
  return nullptr;
}

}  // namespace

std::optional<int64_t> Int64FromPropertyList(
    const grpc_channelz_v2_PropertyList* pl, absl::string_view name) {
  const auto* prop = FindProperty(pl, name);
  if (prop == nullptr) return std::nullopt;
  if (grpc_channelz_v2_PropertyValue_has_int64_value(prop)) {
    return grpc_channelz_v2_PropertyValue_int64_value(prop);
  }
  if (grpc_channelz_v2_PropertyValue_has_uint64_value(prop)) {
    uint64_t val = grpc_channelz_v2_PropertyValue_uint64_value(prop);
    if (val <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      return static_cast<int64_t>(val);
    }
  }
  return std::nullopt;
}

std::optional<std::string> StringFromPropertyList(
    const grpc_channelz_v2_PropertyList* pl, absl::string_view name) {
  const auto* prop = FindProperty(pl, name);
  if (prop == nullptr) return std::nullopt;
  if (grpc_channelz_v2_PropertyValue_has_string_value(prop)) {
    upb_StringView value = grpc_channelz_v2_PropertyValue_string_value(prop);
    return std::string(value.data, value.size);
  }
  return std::nullopt;
}

const google_protobuf_Timestamp* TimestampFromPropertyList(
    const grpc_channelz_v2_PropertyList* pl, absl::string_view name) {
  const auto* prop = FindProperty(pl, name);
  if (prop == nullptr) return nullptr;
  if (grpc_channelz_v2_PropertyValue_has_timestamp_value(prop)) {
    return grpc_channelz_v2_PropertyValue_timestamp_value(prop);
  }
  return nullptr;
}

const grpc_channelz_v2_PropertyList* PropertyListFromPropertyList(
    const grpc_channelz_v2_PropertyList* pl, absl::string_view name,
    upb_Arena* arena) {
  const auto* prop = FindProperty(pl, name);
  if (prop == nullptr) return nullptr;
  if (grpc_channelz_v2_PropertyValue_has_any_value(prop)) {
    const auto* any = grpc_channelz_v2_PropertyValue_any_value(prop);
    upb_StringView type_url = google_protobuf_Any_type_url(any);
    if (absl::string_view(type_url.data, type_url.size) !=
        "type.googleapis.com/grpc.channelz.v2.PropertyList") {
      return nullptr;
    }
    upb_StringView value = google_protobuf_Any_value(any);
    return grpc_channelz_v2_PropertyList_parse(value.data, value.size, arena);
  }
  return nullptr;
}

const google_protobuf_Duration* DurationFromPropertyList(
    const grpc_channelz_v2_PropertyList* pl, absl::string_view name) {
  const auto* prop = FindProperty(pl, name);
  if (prop == nullptr) return nullptr;
  if (grpc_channelz_v2_PropertyValue_has_duration_value(prop)) {
    return grpc_channelz_v2_PropertyValue_duration_value(prop);
  }
  return nullptr;
}

}  // namespace v2tov1
}  // namespace channelz
}  // namespace grpc_core
