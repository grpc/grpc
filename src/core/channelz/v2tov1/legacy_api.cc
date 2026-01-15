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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include <string>
#include <vector>

#include "src/core/channelz/channelz_registry.h"
#include "src/core/channelz/v2tov1/convert.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

// This file is a temporary compatibility layer between the v2 channelz data
// model and the v1 C-API. It should be removed when the v1 C-API is removed.

namespace grpc_core {
namespace channelz {
namespace v2tov1 {

namespace {

class RegistryEntityFetcher : public EntityFetcher {
 public:
  absl::StatusOr<std::string> GetEntity(int64_t id) override {
    auto node = ChannelzRegistry::GetNode(id);
    if (node == nullptr) {
      return absl::NotFoundError(absl::StrCat("Entity not found: ", id));
    }
    return node->SerializeEntityToString(absl::ZeroDuration());
  }

  absl::StatusOr<std::vector<std::string>> GetEntitiesWithParent(
      int64_t parent_id) override {
    auto node = ChannelzRegistry::GetNode(parent_id);
    if (node == nullptr) {
      return absl::NotFoundError(
          absl::StrCat("Parent entity not found: ", parent_id));
    }
    auto [nodes, end] = ChannelzRegistry::GetChildren(
        node.get(), 0, std::numeric_limits<size_t>::max());
    DCHECK(end);
    std::vector<std::string> children_str;
    for (const auto& child_node : nodes) {
      if (child_node == nullptr) continue;
      children_str.push_back(
          child_node->SerializeEntityToString(absl::ZeroDuration()));
    }
    return children_str;
  }
};

}  // namespace

}  // namespace v2tov1
}  // namespace channelz
}  // namespace grpc_core

using grpc_core::channelz::ChannelzRegistry;
using grpc_core::channelz::v2tov1::RegistryEntityFetcher;

char* grpc_channelz_get_top_channels(intptr_t start_channel_id) {
  grpc_core::ExecCtx exec_ctx;
  RegistryEntityFetcher fetcher;
  auto [channels, end] = ChannelzRegistry::GetTopChannels(start_channel_id);
  grpc_core::Json::Array array;
  for (const auto& channel_node : channels) {
    if (channel_node == nullptr) continue;
    auto serialized_v2 =
        channel_node->SerializeEntityToString(absl::ZeroDuration());
    auto serialized_v1 = grpc_core::channelz::v2tov1::ConvertChannel(
        serialized_v2, fetcher, true);
    if (!serialized_v1.ok()) {
      LOG(ERROR) << "Failed to convert channel: " << serialized_v1.status();
      continue;
    }
    auto json = grpc_core::JsonParse(*serialized_v1);
    if (!json.ok()) {
      LOG(ERROR) << "Failed to parse converted channel json: " << json.status();
      continue;
    }
    array.emplace_back(std::move(*json));
  }
  grpc_core::Json json = grpc_core::Json::FromObject({
      {"channel", grpc_core::Json::FromArray(std::move(array))},
      {"end", grpc_core::Json::FromBool(end)},
  });
  std::string json_str = grpc_core::JsonDump(json);
  return gpr_strdup(json_str.c_str());
}

char* grpc_channelz_get_servers(intptr_t start_server_id) {
  grpc_core::ExecCtx exec_ctx;
  RegistryEntityFetcher fetcher;
  auto [servers, end] = ChannelzRegistry::GetServers(start_server_id);
  grpc_core::Json::Array array;
  for (const auto& server_node : servers) {
    if (server_node == nullptr) continue;
    auto serialized_v2 =
        server_node->SerializeEntityToString(absl::ZeroDuration());
    auto serialized_v1 = grpc_core::channelz::v2tov1::ConvertServer(
        serialized_v2, fetcher, true);
    if (!serialized_v1.ok()) {
      LOG(ERROR) << "Failed to convert server: " << serialized_v1.status();
      continue;
    }
    auto json = grpc_core::JsonParse(*serialized_v1);
    if (!json.ok()) {
      LOG(ERROR) << "Failed to parse converted server json: " << json.status();
      continue;
    }
    array.emplace_back(std::move(*json));
  }
  grpc_core::Json json = grpc_core::Json::FromObject({
      {"server", grpc_core::Json::FromArray(std::move(array))},
      {"end", grpc_core::Json::FromBool(end)},
  });
  std::string json_str = grpc_core::JsonDump(json);
  return gpr_strdup(json_str.c_str());
}

char* grpc_channelz_get_server(intptr_t server_id) {
  grpc_core::ExecCtx exec_ctx;
  auto server_node = ChannelzRegistry::GetServer(server_id);
  if (server_node == nullptr) return nullptr;
  RegistryEntityFetcher fetcher;
  auto serialized_v2 =
      server_node->SerializeEntityToString(absl::ZeroDuration());
  auto serialized_v1 =
      grpc_core::channelz::v2tov1::ConvertServer(serialized_v2, fetcher, true);
  if (!serialized_v1.ok()) {
    LOG(ERROR) << "Failed to convert server: " << serialized_v1.status();
    return nullptr;
  }
  auto json = grpc_core::JsonParse(*serialized_v1);
  if (!json.ok()) {
    LOG(ERROR) << "Failed to parse converted server json: " << json.status();
    return nullptr;
  }
  grpc_core::Json wrapped_json = grpc_core::Json::FromObject({
      {"server", std::move(*json)},
  });
  std::string json_str = grpc_core::JsonDump(wrapped_json);
  return gpr_strdup(json_str.c_str());
}

char* grpc_channelz_get_server_sockets(intptr_t server_id,
                                       intptr_t start_socket_id,
                                       intptr_t max_results) {
  grpc_core::ExecCtx exec_ctx;
  auto server_node = ChannelzRegistry::GetServer(server_id);
  if (server_node == nullptr) return nullptr;
  size_t max =
      max_results == 0 ? std::numeric_limits<size_t>::max() : max_results;
  auto [sockets, end] = ChannelzRegistry::GetChildrenOfType(
      start_socket_id, server_node.get(),
      grpc_core::channelz::BaseNode::EntityType::kSocket, max);
  grpc_core::Json::Array array;
  for (const auto& socket_node : sockets) {
    if (socket_node == nullptr) continue;
    array.emplace_back(grpc_core::Json::FromObject({
        {"socketId",
         grpc_core::Json::FromString(absl::StrCat(socket_node->uuid()))},
        {"name", grpc_core::Json::FromString(socket_node->name())},
    }));
  }
  grpc_core::Json json = grpc_core::Json::FromObject({
      {"socketRef", grpc_core::Json::FromArray(std::move(array))},
      {"end", grpc_core::Json::FromBool(end)},
  });
  std::string json_str = grpc_core::JsonDump(json);
  return gpr_strdup(json_str.c_str());
}

char* grpc_channelz_get_channel(intptr_t channel_id) {
  grpc_core::ExecCtx exec_ctx;
  auto channel_node = ChannelzRegistry::GetChannel(channel_id);
  if (channel_node == nullptr) return nullptr;
  RegistryEntityFetcher fetcher;
  auto serialized_v2 =
      channel_node->SerializeEntityToString(absl::ZeroDuration());
  auto serialized_v1 =
      grpc_core::channelz::v2tov1::ConvertChannel(serialized_v2, fetcher, true);
  if (!serialized_v1.ok()) {
    LOG(ERROR) << "Failed to convert channel: " << serialized_v1.status();
    return nullptr;
  }
  auto json = grpc_core::JsonParse(*serialized_v1);
  if (!json.ok()) {
    LOG(ERROR) << "Failed to parse converted channel json: " << json.status();
    return nullptr;
  }
  grpc_core::Json wrapped_json = grpc_core::Json::FromObject({
      {"channel", std::move(*json)},
  });
  std::string json_str = grpc_core::JsonDump(wrapped_json);
  return gpr_strdup(json_str.c_str());
}

char* grpc_channelz_get_subchannel(intptr_t subchannel_id) {
  grpc_core::ExecCtx exec_ctx;
  auto subchannel_node = ChannelzRegistry::GetSubchannel(subchannel_id);
  if (subchannel_node == nullptr) return nullptr;
  RegistryEntityFetcher fetcher;
  auto serialized_v2 =
      subchannel_node->SerializeEntityToString(absl::ZeroDuration());
  auto serialized_v1 = grpc_core::channelz::v2tov1::ConvertSubchannel(
      serialized_v2, fetcher, true);
  if (!serialized_v1.ok()) {
    LOG(ERROR) << "Failed to convert subchannel: " << serialized_v1.status();
    return nullptr;
  }
  auto json = grpc_core::JsonParse(*serialized_v1);
  if (!json.ok()) {
    LOG(ERROR) << "Failed to parse converted subchannel json: "
               << json.status();
    return nullptr;
  }
  grpc_core::Json wrapped_json = grpc_core::Json::FromObject({
      {"subchannel", std::move(*json)},
  });
  std::string json_str = grpc_core::JsonDump(wrapped_json);
  return gpr_strdup(json_str.c_str());
}

char* grpc_channelz_get_socket(intptr_t socket_id) {
  grpc_core::ExecCtx exec_ctx;
  auto node = ChannelzRegistry::GetNode(socket_id);
  if (node == nullptr) return nullptr;
  RegistryEntityFetcher fetcher;
  auto serialized_v2 = node->SerializeEntityToString(absl::ZeroDuration());
  absl::StatusOr<std::string> serialized_v1;
  if (node->type() == grpc_core::channelz::BaseNode::EntityType::kSocket) {
    serialized_v1 = grpc_core::channelz::v2tov1::ConvertSocket(serialized_v2,
                                                               fetcher, true);
  } else if (node->type() ==
             grpc_core::channelz::BaseNode::EntityType::kListenSocket) {
    serialized_v1 = grpc_core::channelz::v2tov1::ConvertListenSocket(
        serialized_v2, fetcher, true);
  } else {
    return nullptr;
  }
  if (!serialized_v1.ok()) {
    LOG(ERROR) << "Failed to convert socket: " << serialized_v1.status();
    return nullptr;
  }
  // The old API returned a JSON object with a "socket" key.
  // The new converter returns the socket JSON directly. We need to wrap it.
  auto json = grpc_core::JsonParse(*serialized_v1);
  if (!json.ok()) {
    LOG(ERROR) << "Failed to parse converted socket json: " << json.status();
    return nullptr;
  }
  grpc_core::Json wrapped_json = grpc_core::Json::FromObject({
      {"socket", std::move(*json)},
  });
  std::string json_str = grpc_core::JsonDump(wrapped_json);
  return gpr_strdup(json_str.c_str());
}
