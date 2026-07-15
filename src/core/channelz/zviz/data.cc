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
#include <string>
#include <vector>

#include "src/core/channelz/zviz/environment.h"
#include "src/core/channelz/zviz/layout.h"
#include "src/core/util/no_destruct.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "src/proto/grpc/channelz/v2/promise.pb.h"
#include "src/proto/grpc/channelz/v2/property_list.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/span.h"

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

std::string FormatValue(const grpc::channelz::v2::PropertyValue& value) {
  switch (value.kind_case()) {
    case grpc::channelz::v2::PropertyValue::KIND_NOT_SET:
    case grpc::channelz::v2::PropertyValue::kEmptyValue:
      return "";
    case grpc::channelz::v2::PropertyValue::kAnyValue:
      return std::string(value.any_value().type_url());
    case grpc::channelz::v2::PropertyValue::kStringValue:
      return value.string_value();
    case grpc::channelz::v2::PropertyValue::kInt64Value:
      return absl::StrCat(value.int64_value());
    case grpc::channelz::v2::PropertyValue::kUint64Value:
      return absl::StrCat(value.uint64_value());
    case grpc::channelz::v2::PropertyValue::kDoubleValue:
      return absl::StrCat(value.double_value());
    case grpc::channelz::v2::PropertyValue::kBoolValue:
      return value.bool_value() ? "true" : "false";
    case grpc::channelz::v2::PropertyValue::kTimestampValue:
      return value.timestamp_value().DebugString();
    case grpc::channelz::v2::PropertyValue::kDurationValue:
      return value.duration_value().DebugString();
  }
}

std::string FormatFactory(absl::string_view factory) {
  if (absl::ConsumePrefix(&factory, "(lambda at ") &&
      absl::ConsumeSuffix(&factory, ")")) {
    std::vector<absl::string_view> parts = absl::StrSplit(factory, ':');
    if (parts.size() >= 2) {
      std::vector<absl::string_view> path_parts = absl::StrSplit(parts[0], '/');
      return absl::StrCat(path_parts.back(), ":", parts[1]);
    }
  }
  return std::string(factory);
}

void PromiseFormatterImpl(const grpc::channelz::v2::Promise& promise,
                          std::string& out, int indent) {
  switch (promise.promise_case()) {
    case grpc::channelz::v2::Promise::kSeqPromise: {
      const auto& seq = promise.seq_promise();
      absl::StrAppend(&out, seq.kind() == grpc::channelz::v2::Promise::TRY
                                ? "TrySeq(\n"
                                : "Seq(\n");
      for (const auto& step : seq.steps()) {
        if (step.has_polling_promise()) {
          absl::StrAppend(&out, "🟢", std::string(indent, ' '),
                          FormatFactory(step.factory()), ",\n");
          absl::StrAppend(&out, std::string(indent + 2, ' '));
          PromiseFormatterImpl(step.polling_promise(), out, indent + 2);
          absl::StrAppend(&out, ",\n");
        } else {
          absl::StrAppend(&out, std::string(indent + 2, ' '),
                          FormatFactory(step.factory()), ",\n");
        }
      }
      absl::StrAppend(&out, std::string(indent, ' '), ")");
      break;
    }
    case grpc::channelz::v2::Promise::kJoinPromise: {
      const auto& join = promise.join_promise();
      absl::StrAppend(&out, join.kind() == grpc::channelz::v2::Promise::TRY
                                ? "TryJoin(\n"
                                : "Join(\n");
      for (const auto& branch : join.branches()) {
        if (branch.has_polling_promise()) {
          absl::StrAppend(&out, "🟢", std::string(indent, ' '),
                          FormatFactory(branch.factory()), ",\n");
          absl::StrAppend(&out, std::string(indent + 2, ' '));
          PromiseFormatterImpl(branch.polling_promise(), out, indent + 2);
          absl::StrAppend(&out, ",\n");
        } else if (branch.has_result()) {
          absl::StrAppend(&out, "✅", std::string(indent, ' '),
                          FormatFactory(branch.factory()), ",\n");
        } else {
          absl::StrAppend(&out, std::string(indent + 2, ' '),
                          FormatFactory(branch.factory()), ",\n");
        }
      }
      absl::StrAppend(&out, std::string(indent, ' '), ")");
      break;
    }
    case grpc::channelz::v2::Promise::kMapPromise: {
      const auto& map = promise.map_promise();
      absl::StrAppend(&out, "Map(\n", std::string(indent + 2, ' '));
      PromiseFormatterImpl(map.promise(), out, indent + 2);
      absl::StrAppend(&out, ",\n", std::string(indent + 2, ' '),
                      FormatFactory(map.map_fn()), "\n",
                      std::string(indent, ' '), ")");
      break;
    }
    case grpc::channelz::v2::Promise::kForEachPromise: {
      const auto& for_each = promise.for_each_promise();
      absl::StrAppend(&out, "ForEach(\n", std::string(indent + 2, ' '),
                      FormatFactory(for_each.reader_factory()), ", ",
                      FormatFactory(for_each.action_factory()));
      if (for_each.has_reader_promise()) {
        absl::StrAppend(&out, ",\n", std::string(indent + 2, ' '));
        PromiseFormatterImpl(for_each.reader_promise(), out, indent + 2);
      } else if (for_each.has_action_promise()) {
        absl::StrAppend(&out, ",\n", std::string(indent + 2, ' '));
        PromiseFormatterImpl(for_each.action_promise(), out, indent + 2);
      }
      absl::StrAppend(&out, "\n", std::string(indent, ' '), ")");
      break;
    }
    case grpc::channelz::v2::Promise::kIfPromise: {
      const auto& if_p = promise.if_promise();
      absl::StrAppend(&out, "If(", if_p.condition() ? "true" : "false", ", ",
                      FormatFactory(if_p.true_factory()), ", ",
                      FormatFactory(if_p.false_factory()), ",\n",
                      std::string(indent + 2, ' '));
      PromiseFormatterImpl(if_p.promise(), out, indent + 2);
      absl::StrAppend(&out, "\n", std::string(indent, ' '), ")");
      break;
    }
    case grpc::channelz::v2::Promise::kLoopPromise: {
      const auto& loop = promise.loop_promise();
      std::string loop_factory_str = loop.loop_factory();
      std::string formatted_loop_factory;
      if (absl::StrContains(loop_factory_str, "RepeatedPromiseFactory")) {
        size_t pos = loop_factory_str.find("(lambda at ");
        if (pos != std::string::npos) {
          size_t end_pos = pos;
          while ((end_pos = loop_factory_str.find(')', end_pos + 1)) !=
                 std::string::npos) {
            if (end_pos > 0 && loop_factory_str[end_pos - 1] >= '0' &&
                loop_factory_str[end_pos - 1] <= '9') {
              formatted_loop_factory = FormatFactory(
                  loop_factory_str.substr(pos, end_pos - pos + 1));
              break;
            }
          }
        }
      }
      if (formatted_loop_factory.empty()) {
        formatted_loop_factory = FormatFactory(loop_factory_str);
      }
      absl::StrAppend(&out, "Loop(\n", std::string(indent + 2, ' '),
                      formatted_loop_factory, ",\n",
                      std::string(indent + 2, ' '));
      PromiseFormatterImpl(loop.promise(), out, indent + 2);
      absl::StrAppend(&out, loop.yield() ? ", yield" : "", "\n",
                      std::string(indent, ' '), ")");
      break;
    }
    case grpc::channelz::v2::Promise::kRacePromise: {
      const auto& race = promise.race_promise();
      absl::StrAppend(&out, "Race(\n");
      for (const auto& child : race.children()) {
        absl::StrAppend(&out, std::string(indent + 2, ' '));
        PromiseFormatterImpl(child, out, indent + 2);
        absl::StrAppend(&out, ",\n");
      }
      absl::StrAppend(&out, std::string(indent, ' '), ")");
      break;
    }
    case grpc::channelz::v2::Promise::kCustomPromise: {
      const auto& custom = promise.custom_promise();
      bool multiline = custom.properties().properties_size() > 1;
      if (custom.properties().properties().empty()) {
        absl::StrAppend(&out, custom.type());
        break;
      }
      if (!multiline) {
        const auto& prop = custom.properties().properties(0);
        std::string value = FormatValue(prop.value());
        if (absl::StrContains(value, '\n') ||
            custom.type().length() + prop.key().length() + value.length() + 2 >
                60) {
          multiline = true;
        } else {
          absl::StrAppend(&out, custom.type(), " ", prop.key(), ":", value);
        }
      }
      if (multiline) {
        absl::StrAppend(&out, custom.type(), " {\n");
        for (const auto& prop : custom.properties().properties()) {
          absl::StrAppend(&out, std::string(indent + 4, ' '), prop.key(), ": ",
                          FormatValue(prop.value()), "\n");
        }
        absl::StrAppend(&out, std::string(indent + 2, ' '), "}");
      }
      break;
    }
    case grpc::channelz::v2::Promise::kUnknownPromise: {
      std::string formatted = FormatFactory(promise.unknown_promise());
      if (formatted == promise.unknown_promise()) {
        absl::StrAppend(&out, "Unknown(", promise.unknown_promise(), ")");
      } else {
        absl::StrAppend(&out, formatted);
      }
      break;
    }
    case grpc::channelz::v2::Promise::PROMISE_NOT_SET:
      absl::StrAppend(&out, "PromiseNotSet");
      break;
  }
}

bool PromiseFormatter(Environment&, google::protobuf::Any value,
                      layout::Element& element) {
  grpc::channelz::v2::Promise promise;
  if (!value.UnpackTo(&promise)) return false;
  element.AppendText(layout::Intent::kCode, grpc_zviz::Format(promise));
  return true;
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
      formatters.emplace("type.googleapis.com/grpc.channelz.v2.Promise",
                         PromiseFormatter);
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

std::string Format(const grpc::channelz::v2::Promise& promise) {
  std::string out;
  PromiseFormatterImpl(promise, out, 0);
  return out;
}

}  // namespace grpc_zviz
