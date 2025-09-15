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

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "src/core/channelz/zviz/entity.h"
#include "src/core/channelz/zviz/layout_text.h"
#include "src/core/channelz/zviz/trace.h"
#include "test/cpp/sleuth/client.h"
#include "test/cpp/sleuth/tool.h"
#include "test/cpp/sleuth/tool_options.h"

ABSL_FLAG(std::optional<std::string>, channelz_target, std::nullopt,
          "Target to connect to for channelz");

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
}  // namespace

SLEUTH_TOOL(dump_channelz, "[destination]",
            "Dumps all channelz data in human-readable text format; if "
            "destination is not specified, dumps to stdout.") {
  if (args.size() > 1) {
    return absl::InvalidArgumentError("Too many arguments");
  }
  if (args.size() == 1) {
    return absl::UnimplementedError("Destination not implemented yet");
  }

  auto target = absl::GetFlag(FLAGS_channelz_target);
  if (!target.has_value()) {
    return absl::InvalidArgumentError("--channelz_target is required");
  }

  auto response =
      Client(*target, ToolClientOptions()).QueryAllChannelzEntities();
  if (!response.ok()) return response.status();

  SleuthEnvironment env(*response);
  grpc_zviz::layout::TextElement root;
  for (const auto& entity : *response) {
    grpc_zviz::Format(env, entity, root);
  }
  std::cout << root.Render();

  return absl::OkStatus();
}

SLEUTH_TOOL(ztrace, "entity [trace_name]",
            "Dumps a ztrace. If trace_name is not specified, defaults to "
            "'transport_frames'.") {
  if (args.size() > 2) {
    return absl::InvalidArgumentError("Too many arguments");
  }
  if (args.empty()) {
    return absl::InvalidArgumentError("No entity id provided");
  }
  int64_t entity_id;
  if (!absl::SimpleAtoi(args[0], &entity_id)) {
    return absl::InvalidArgumentError("Invalid entity id");
  }
  std::string trace_name = args.size() == 2 ? args[1] : "transport_frames";

  auto target = absl::GetFlag(FLAGS_channelz_target);
  if (!target.has_value()) {
    return absl::InvalidArgumentError("--channelz_target is required");
  }
  auto client = Client(*target, ToolClientOptions());
  SleuthEnvironment env({});
  return client.QueryTrace(
      entity_id, trace_name, [&](size_t missed, const auto& events) {
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
        std::cout << root.Render();
      });
}

}  // namespace grpc_sleuth
