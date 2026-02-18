// Copyright 2025 The gRPC Authors
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

#include "src/core/channelz/zviz/property_list.h"

#include <string>
#include <utility>
#include <vector>

#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "src/proto/grpc/channelz/v2/property_list.pb.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace grpc_zviz {

namespace {

// Forward declaration
std::optional<std::string> GetPropertyAsStringFromList(
    const grpc::channelz::v2::PropertyList& property_list,
    absl::string_view path);

std::string PropertyValueToString(
    const grpc::channelz::v2::PropertyValue& property) {
  switch (property.kind_case()) {
    case grpc::channelz::v2::PropertyValue::kStringValue:
      return property.string_value();
    case grpc::channelz::v2::PropertyValue::kInt64Value:
      return absl::StrCat(property.int64_value());
    case grpc::channelz::v2::PropertyValue::kUint64Value:
      return absl::StrCat(property.uint64_value());
    case grpc::channelz::v2::PropertyValue::kDoubleValue:
      return absl::StrCat(property.double_value());
    case grpc::channelz::v2::PropertyValue::kBoolValue:
      return property.bool_value() ? "true" : "false";
    case grpc::channelz::v2::PropertyValue::kTimestampValue:
      return absl::FormatTime(
          absl::FromUnixSeconds(property.timestamp_value().seconds()) +
          absl::Nanoseconds(property.timestamp_value().nanos()));
    case grpc::channelz::v2::PropertyValue::kDurationValue:
      return absl::FormatDuration(
          absl::Seconds(property.duration_value().seconds()) +
          absl::Nanoseconds(property.duration_value().nanos()));
    case grpc::channelz::v2::PropertyValue::kEmptyValue:
    case grpc::channelz::v2::PropertyValue::kAnyValue:
    case grpc::channelz::v2::PropertyValue::KIND_NOT_SET:
      return "";
  }
  return "";
}

std::optional<std::string> GetPropertyAsStringFromList(
    const grpc::channelz::v2::PropertyList& property_list,
    absl::string_view path) {
  std::vector<absl::string_view> parts = absl::StrSplit(path, '.');
  for (const auto& element : property_list.properties()) {
    if (element.key() != parts[0]) continue;
    const auto& value = element.value();
    if (parts.size() == 1) return PropertyValueToString(value);
    if (value.kind_case() == grpc::channelz::v2::PropertyValue::kAnyValue) {
      const auto& any = value.any_value();
      if (any.Is<grpc::channelz::v2::PropertyList>()) {
        grpc::channelz::v2::PropertyList nested_list;
        if (any.UnpackTo(&nested_list)) {
          return GetPropertyAsStringFromList(
              nested_list, path.substr(parts[0].length() + 1));
        }
      }
    }
    // Path asks for more nesting but this property is not a list.
    return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace

std::optional<std::string> GetPropertyAsString(
    const grpc::channelz::v2::Entity& entity, absl::string_view path) {
  if (path.empty()) return std::nullopt;
  if (path == "id") return absl::StrCat(entity.id());
  if (path == "kind") return entity.kind();

  std::vector<absl::string_view> parts = absl::StrSplit(path, '.');
  absl::string_view first_part = parts[0];

  for (const auto& data : entity.data()) {
    if (data.name() == first_part) {
      if (data.value().Is<grpc::channelz::v2::PropertyList>()) {
        grpc::channelz::v2::PropertyList property_list;
        if (data.value().UnpackTo(&property_list)) {
          if (parts.size() == 1) {
            // The path refers to the PropertyList itself, not a key within it.
            // This is not a valid scenario as per the requirements, which
            // expect a path to a specific property. Returning nullopt.
            return std::nullopt;
          }
          auto result = GetPropertyAsStringFromList(
              property_list, path.substr(first_part.length() + 1));
          if (result.has_value()) {
            return result;
          }
        }
      }
    }
  }
  return std::nullopt;
}

}  // namespace grpc_zviz
