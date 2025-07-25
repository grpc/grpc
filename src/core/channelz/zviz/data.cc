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

#include "src/core/channelz/zviz/data.h"

#include <algorithm>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/core/channelz/zviz/environment.h"
#include "src/core/channelz/zviz/layout.h"
#include "src/core/util/no_destruct.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "src/proto/grpc/channelz/v2/property_list.pb.h"

namespace grpc_zviz {

namespace {

void Format(Environment& env, const grpc::channelz::v2::PropertyValue& value,
            layout::Element& element) {
  switch (value.kind_case()) {
    case grpc::channelz::v2::PropertyValue::KIND_NOT_SET:
    case grpc::channelz::v2::PropertyValue::kEmptyValue:
      break;
    case grpc::channelz::v2::PropertyValue::kAnyValue:
      Format(env, value.any_value(), element);
      break;
    case grpc::channelz::v2::PropertyValue::kStringValue:
      element.AppendText(layout::Intent::kValue, value.string_value());
      break;
    case grpc::channelz::v2::PropertyValue::kInt64Value:
      element.AppendText(layout::Intent::kValue,
                         absl::StrCat(value.int64_value()));
      break;
    case grpc::channelz::v2::PropertyValue::kUint64Value:
      element.AppendText(layout::Intent::kValue,
                         absl::StrCat(value.uint64_value()));
      break;
    case grpc::channelz::v2::PropertyValue::kDoubleValue:
      element.AppendText(layout::Intent::kValue,
                         absl::StrCat(value.double_value()));
      break;
    case grpc::channelz::v2::PropertyValue::kBoolValue:
      element.AppendText(layout::Intent::kValue,
                         value.bool_value() ? "true" : "false");
      break;
    case grpc::channelz::v2::PropertyValue::kTimestampValue:
      element.AppendTimestamp(value.timestamp_value());
      break;
    case grpc::channelz::v2::PropertyValue::kDurationValue:
      element.AppendDuration(value.duration_value());
      break;
  }
}

bool PropertyListFormatter(Environment& env, google::protobuf::Any value,
                           layout::Element& element) {
  grpc::channelz::v2::PropertyList property_list;
  if (!value.UnpackTo(&property_list)) return false;
  if (property_list.properties().empty()) return true;
  auto& table = element.AppendTable(layout::TableIntent::kPropertyList);
  for (const auto& el : property_list.properties()) {
    table.AppendColumn().AppendText(layout::Intent::kKey, el.key());
    Format(env, el.value(), table.AppendColumn());
    table.NewRow();
  }
  return true;
}

bool PropertyGridFormatter(Environment& env, google::protobuf::Any value,
                           layout::Element& element) {
  grpc::channelz::v2::PropertyGrid property_grid;
  if (!value.UnpackTo(&property_grid)) return false;
  auto& table = element.AppendTable(layout::TableIntent::kPropertyGrid);
  table.AppendColumn();
  for (const auto& column : property_grid.columns()) {
    table.AppendColumn().AppendText(layout::Intent::kKey, column);
  }
  table.NewRow();
  for (const auto& row : property_grid.rows()) {
    table.AppendColumn().AppendText(layout::Intent::kKey, row.label());
    for (const auto& value : row.value()) {
      Format(env, value, table.AppendColumn());
    }
    table.NewRow();
  }
  return true;
}

bool PropertyTableFormatter(Environment& env, google::protobuf::Any value,
                            layout::Element& element) {
  grpc::channelz::v2::PropertyTable property_table;
  if (!value.UnpackTo(&property_table)) return false;
  auto& table = element.AppendTable(layout::TableIntent::kPropertyTable);
  for (const auto& column : property_table.columns()) {
    table.AppendColumn().AppendText(layout::Intent::kKey, column);
  }
  for (const auto& row : property_table.rows()) {
    table.NewRow();
    for (const auto& value : row.value()) {
      Format(env, value, table.AppendColumn());
    }
  }
  return true;
}

using Formatter = bool (*)(Environment&, google::protobuf::Any,
                           layout::Element&);

const grpc_core::NoDestruct<absl::flat_hash_map<absl::string_view, Formatter>>
    kDataFormatters([]() {
      absl::flat_hash_map<absl::string_view, Formatter> formatters;
      formatters.emplace("type.googleapis.com/grpc.channelz.v2.PropertyList",
                         PropertyListFormatter);
      formatters.emplace("type.googleapis.com/grpc.channelz.v2.PropertyGrid",
                         PropertyGridFormatter);
      formatters.emplace("type.googleapis.com/grpc.channelz.v2.PropertyTable",
                         PropertyTableFormatter);
      return formatters;
    }());

void Failed(absl::string_view message, const google::protobuf::Any& value,
            layout::Element& element) {
  element.AppendText(layout::Intent::kNote, message);
  element.AppendText(layout::Intent::kData, value.DebugString());
}

}  // namespace

void Format(Environment& env, const google::protobuf::Any& value,
            layout::Element& element) {
  auto formatter = kDataFormatters->find(value.type_url());
  if (formatter != kDataFormatters->end()) {
    if (!formatter->second(env, value, element)) {
      Failed("Failed to format type", value, element);
    }
  } else {
    Failed("Unknown type", value, element);
  }
}

void Format(Environment& env, const grpc::channelz::v2::Data& data,
            layout::Element& element) {
  Format(env, data.value(),
         element.AppendData(data.name(), data.value().type_url()));
}

}  // namespace grpc_zviz
