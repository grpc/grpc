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

#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "src/core/util/upb_utils.h"
#include "src/proto/grpc/channelz/v2/property_list.upb.h"
#include "upb/mem/arena.hpp"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace channelz {
namespace v2tov1 {

namespace {

const grpc_channelz_v2_PropertyValue* FindProperty(
    const grpc_channelz_v2_PropertyList* pl, absl::string_view name) {
  size_t size;
  const grpc_channelz_v2_PropertyList_Element* const* elements =
      grpc_channelz_v2_PropertyList_properties(pl, &size);
  for (size_t i = 0; i < size; i++) {
    const grpc_channelz_v2_PropertyList_Element* element = elements[i];
    upb_StringView label = grpc_channelz_v2_PropertyList_Element_key(element);
    if (absl::string_view(label.data, label.size) == name) {
      return grpc_channelz_v2_PropertyList_Element_value(element);
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
    return UpbStringToStdString(
        grpc_channelz_v2_PropertyValue_string_value(prop));
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
    auto type_url = UpbStringToAbsl(google_protobuf_Any_type_url(any));
    if (type_url != "type.googleapis.com/grpc.channelz.v2.PropertyList") {
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
