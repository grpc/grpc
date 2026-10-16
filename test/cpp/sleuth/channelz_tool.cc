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

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "src/core/channelz/zviz/entity.h"
#include "src/core/channelz/zviz/environment.h"
#include "src/core/channelz/zviz/format_entity_list.h"
#include "src/core/channelz/zviz/layout.h"
#include "src/core/channelz/zviz/layout_text.h"
#include "src/core/channelz/zviz/trace.h"
#include "test/cpp/sleuth/client.h"
#include "test/cpp/sleuth/tool.h"
#include "test/cpp/sleuth/tool_options.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace grpc_sleuth {

namespace {
class SleuthEnvironment : public grpc_zviz::Environment {
 public:
  explicit SleuthEnvironment(
      const std::vector<grpc::channelz::v2::Entity>& entities) {
    for (const auto& entity : entities) {
      entities_[entity.id()] = entity;
    }
  }

  std::string EntityLinkTarget(int64_t entity_id) override {
    return absl::StrCat("#", entity_id);
  }

  absl::StatusOr<grpc::channelz::v2::Entity> GetEntity(
      int64_t entity_id) override {
    auto it = entities_.find(entity_id);
    if (it == entities_.end()) {
      return absl::NotFoundError(absl::StrCat("Entity not found: ", entity_id));
    }
    return it->second;
  }

 private:
  std::map<int64_t, grpc::channelz::v2::Entity> entities_;
};

absl::StatusOr<std::vector<grpc_zviz::EntityTableColumn>> ParseColumns(
    absl::string_view columns) {
  std::vector<grpc_zviz::EntityTableColumn> result;
  for (const auto& column : absl::StrSplit(columns, ',')) {
    std::vector<std::string> segments = absl::StrSplit(column, '@');
    switch (segments.size()) {
      case 1:
        result.push_back(
            grpc_zviz::EntityTableColumn{segments[0], segments[0]});
        break;
      case 2:
        result.push_back(
            grpc_zviz::EntityTableColumn{segments[0], segments[1]});
        break;
      default:
        return absl::InvalidArgumentError(
            absl::StrCat("Invalid column spec: ", column));
    }
  }
  return result;
}
}  // namespace

SLEUTH_TOOL(dump_channelz, "target=... [destination=...]",
            "Dumps all channelz data in human-readable text format; if "
            "destination is not specified, dumps to stdout.") {
  auto target = args.TryGetFlag<std::string>("target");
  if (!target.ok()) return target.status();
  if (args.TryGetFlag<std::string>("destination").ok()) {
    return absl::UnimplementedError("Destination not implemented yet");
  }
  std::optional<std::string> channel_creds_type;
  auto channel_creds_type_arg =
      args.TryGetFlag<std::string>("channel_creds_type");
  if (channel_creds_type_arg.ok()) {
    channel_creds_type = *channel_creds_type_arg;
  }
  auto channelz_protocol = args.TryGetFlag<std::string>("channelz_protocol");
  auto response =
      Client(*target, ToolClientOptions(
                          channelz_protocol.ok() ? *channelz_protocol : "h2",
                          channel_creds_type))
          .QueryAllChannelzEntities();
  if (!response.ok()) return response.status();

  SleuthEnvironment env(*response);
  grpc_zviz::layout::TextElement root;
  for (const auto& entity : *response) {
    grpc_zviz::Format(env, entity, root);
  }
  print_fn(root.Render());

  return absl::OkStatus();
}

SLEUTH_TOOL(ls, "target=... [entity_kind=...]",
            "Lists all entities of the given kind.") {
  auto target = args.TryGetFlag<std::string>("target");
  if (!target.ok()) return target.status();
  absl::StatusOr<std::vector<grpc::channelz::v2::Entity>> response;
  auto columns_str = args.TryGetFlag<std::string>("columns");
  auto columns = ParseColumns(
      columns_str.ok() ? *columns_str
                       : "ID@id,Kind@kind,Name@v1_compatibility.name");
  if (!columns.ok()) return columns.status();
  std::optional<std::string> channel_creds_type;
  auto channel_creds_type_arg =
      args.TryGetFlag<std::string>("channel_creds_type");
  if (channel_creds_type_arg.ok()) {
    channel_creds_type = *channel_creds_type_arg;
  }
  auto channelz_protocol = args.TryGetFlag<std::string>("channelz_protocol");
  Client client(*target, ToolClientOptions(
                             channelz_protocol.ok() ? *channelz_protocol : "h2",
                             channel_creds_type));
  auto entity_kind = args.TryGetFlag<std::string>("entity_kind");
  if (!entity_kind.ok()) {
    response = client.QueryAllChannelzEntities();
  } else {
    response = client.QueryAllChannelzEntitiesOfKind(*entity_kind);
  }
  if (!response.ok()) return response.status();
  grpc_zviz::layout::TextElement root;
  SleuthEnvironment env(*response);
  grpc_zviz::FormatEntityList(env, *response, *columns, root);
  print_fn(root.Render());
  return absl::OkStatus();
}

SLEUTH_TOOL(ztrace, "target=... entity_id=... [trace_name=...]",
            "Dumps a ztrace. If trace_name is not specified, defaults to "
            "'transport_frames'.") {
  auto target = args.TryGetFlag<std::string>("target");
  if (!target.ok()) return target.status();
  auto entity_id = args.TryGetFlag<int64_t>("entity_id");
  if (!entity_id.ok()) return entity_id.status();
  auto trace_name = args.TryGetFlag<std::string>("trace_name");
  std::optional<std::string> channel_creds_type;
  auto channel_creds_type_arg =
      args.TryGetFlag<std::string>("channel_creds_type");
  if (channel_creds_type_arg.ok()) {
    channel_creds_type = *channel_creds_type_arg;
  }
  auto channelz_protocol = args.TryGetFlag<std::string>("channelz_protocol");
  auto client = Client(
      *target,
      ToolClientOptions(channelz_protocol.ok() ? *channelz_protocol : "h2",
                        channel_creds_type));
  SleuthEnvironment env({});
  return client.QueryTrace(
      *entity_id, trace_name.ok() ? *trace_name : "transport_frames",
      [&](size_t missed, const auto& events) {
        grpc_zviz::layout::TextElement root;
        root.AppendText(grpc_zviz::layout::Intent::kNote,
                        absl::StrCat(missed, " events not displayed"));
        auto& table = root.AppendTable(grpc_zviz::layout::TableIntent::kTrace);
        table.AppendColumn().AppendText(grpc_zviz::layout::Intent::kKey,
                                        "Timestamp");
        table.AppendColumn().AppendText(grpc_zviz::layout::Intent::kValue,
                                        "Details");
        table.NewRow();
        for (const auto& event : events) {
          grpc_zviz::Format(env, *event, table);
          table.NewRow();
        }
        print_fn(root.Render());
      });
}

}  // namespace grpc_sleuth
