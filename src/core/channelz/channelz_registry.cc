//
//
// Copyright 2017 gRPC authors.
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
//
//

#include "src/core/channelz/channelz_registry.h"

#include <grpc/grpc.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/channelz/channelz.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/sync.h"

namespace grpc_core {
namespace channelz {
namespace {
template <typename T>
std::string RenderArray(std::tuple<T, bool> values_and_end,
                        const std::string& key) {
  auto& [values, end] = values_and_end;
  Json::Object object;
  if (!values.empty()) {
    // Create list of channels.
    Json::Array array;
    for (size_t i = 0; i < values.size(); ++i) {
      array.emplace_back(values[i]->RenderJson());
    }
    object[key] = Json::FromArray(std::move(array));
  }
  if (end) {
    object["end"] = Json::FromBool(true);
  }
  return JsonDump(Json::FromObject(std::move(object)));
}

Json RemoveAdditionalInfo(const Json& json) {
  if (json.type() != Json::Type::kObject) return json;
  Json::Object out;
  for (const auto& [key, value] : json.object()) {
    if (key == "additionalInfo") continue;
    out[key] = RemoveAdditionalInfo(value);
  }
  return Json::FromObject(std::move(out));
}

// TODO(ctiller): Temporary hack to remove fields that are objectionable to the
// protobuf parser (because we've not published them in protobuf yet).
char* ApplyHacks(const std::string& json_str) {
  auto json = JsonParse(json_str);
  if (!json.ok()) return gpr_strdup(json_str.c_str());
  return gpr_strdup(JsonDump(RemoveAdditionalInfo(*json)).c_str());
}
}  // namespace

ChannelzRegistry* ChannelzRegistry::Default() {
  static ChannelzRegistry* singleton = new ChannelzRegistry();
  return singleton;
}

void ChannelzRegistry::InternalRegister(BaseNode* node) {
  MutexLock lock(&mu_);
  node->uuid_ = ++uuid_generator_;
  node_map_[node->uuid_] = node;
}

void ChannelzRegistry::InternalUnregister(intptr_t uuid) {
  CHECK_GE(uuid, 1);
  MutexLock lock(&mu_);
  CHECK(uuid <= uuid_generator_);
  node_map_.erase(uuid);
}

RefCountedPtr<BaseNode> ChannelzRegistry::InternalGet(intptr_t uuid) {
  MutexLock lock(&mu_);
  if (uuid < 1 || uuid > uuid_generator_) {
    return nullptr;
  }
  auto it = node_map_.find(uuid);
  if (it == node_map_.end()) return nullptr;
  // Found node.  Return only if its refcount is not zero (i.e., when we
  // know that there is no other thread about to destroy it).
  BaseNode* node = it->second;
  return node->RefIfNonZero();
}

std::vector<RefCountedPtr<BaseNode>>
ChannelzRegistry::InternalGetAllEntities() {
  std::vector<RefCountedPtr<BaseNode>> nodes;
  {
    MutexLock lock(&mu_);
    for (const auto& [_, p] : node_map_) {
      RefCountedPtr<BaseNode> node = p->RefIfNonZero();
      if (node != nullptr) {
        nodes.emplace_back(std::move(node));
      }
    }
  }
  return nodes;
}

void ChannelzRegistry::InternalLogAllEntities() {
  for (const auto& p : InternalGetAllEntities()) {
    std::string json = p->RenderJsonString();
    LOG(INFO) << json;
  }
}

std::string ChannelzRegistry::GetTopChannelsJson(intptr_t start_channel_id) {
  return RenderArray(GetTopChannels(start_channel_id), "channel");
}

std::string ChannelzRegistry::GetServersJson(intptr_t start_server_id) {
  return RenderArray(GetServers(start_server_id), "server");
}

}  // namespace channelz
}  // namespace grpc_core

char* grpc_channelz_get_top_channels(intptr_t start_channel_id) {
  grpc_core::ExecCtx exec_ctx;
  return grpc_core::channelz::ApplyHacks(
      grpc_core::channelz::ChannelzRegistry::GetTopChannelsJson(
          start_channel_id)
          .c_str());
}

char* grpc_channelz_get_servers(intptr_t start_server_id) {
  grpc_core::ExecCtx exec_ctx;
  return grpc_core::channelz::ApplyHacks(
      grpc_core::channelz::ChannelzRegistry::GetServersJson(start_server_id)
          .c_str());
}

char* grpc_channelz_get_server(intptr_t server_id) {
  grpc_core::ExecCtx exec_ctx;
  grpc_core::RefCountedPtr<grpc_core::channelz::BaseNode> server_node =
      grpc_core::channelz::ChannelzRegistry::Get(server_id);
  if (server_node == nullptr ||
      server_node->type() !=
          grpc_core::channelz::BaseNode::EntityType::kServer) {
    return nullptr;
  }
  grpc_core::Json json = grpc_core::Json::FromObject({
      {"server", server_node->RenderJson()},
  });
  return grpc_core::channelz::ApplyHacks(grpc_core::JsonDump(json).c_str());
}

char* grpc_channelz_get_server_sockets(intptr_t server_id,
                                       intptr_t start_socket_id,
                                       intptr_t max_results) {
  grpc_core::ExecCtx exec_ctx;
  // Validate inputs before handing them of to the renderer.
  grpc_core::RefCountedPtr<grpc_core::channelz::BaseNode> base_node =
      grpc_core::channelz::ChannelzRegistry::Get(server_id);
  if (base_node == nullptr ||
      base_node->type() != grpc_core::channelz::BaseNode::EntityType::kServer ||
      start_socket_id < 0 || max_results < 0) {
    return nullptr;
  }
  // This cast is ok since we have just checked to make sure base_node is
  // actually a server node.
  grpc_core::channelz::ServerNode* server_node =
      static_cast<grpc_core::channelz::ServerNode*>(base_node.get());
  return grpc_core::channelz::ApplyHacks(
      server_node->RenderServerSockets(start_socket_id, max_results).c_str());
}

char* grpc_channelz_get_channel(intptr_t channel_id) {
  grpc_core::ExecCtx exec_ctx;
  grpc_core::RefCountedPtr<grpc_core::channelz::BaseNode> channel_node =
      grpc_core::channelz::ChannelzRegistry::Get(channel_id);
  if (channel_node == nullptr ||
      (channel_node->type() !=
           grpc_core::channelz::BaseNode::EntityType::kTopLevelChannel &&
       channel_node->type() !=
           grpc_core::channelz::BaseNode::EntityType::kInternalChannel)) {
    return nullptr;
  }
  grpc_core::Json json = grpc_core::Json::FromObject({
      {"channel", channel_node->RenderJson()},
  });
  return grpc_core::channelz::ApplyHacks(grpc_core::JsonDump(json).c_str());
}

char* grpc_channelz_get_subchannel(intptr_t subchannel_id) {
  grpc_core::ExecCtx exec_ctx;
  grpc_core::RefCountedPtr<grpc_core::channelz::BaseNode> subchannel_node =
      grpc_core::channelz::ChannelzRegistry::Get(subchannel_id);
  if (subchannel_node == nullptr ||
      subchannel_node->type() !=
          grpc_core::channelz::BaseNode::EntityType::kSubchannel) {
    return nullptr;
  }
  grpc_core::Json json = grpc_core::Json::FromObject({
      {"subchannel", subchannel_node->RenderJson()},
  });
  return grpc_core::channelz::ApplyHacks(grpc_core::JsonDump(json).c_str());
}

char* grpc_channelz_get_socket(intptr_t socket_id) {
  grpc_core::ExecCtx exec_ctx;
  grpc_core::RefCountedPtr<grpc_core::channelz::BaseNode> socket_node =
      grpc_core::channelz::ChannelzRegistry::Get(socket_id);
  if (socket_node == nullptr ||
      (socket_node->type() !=
           grpc_core::channelz::BaseNode::EntityType::kSocket &&
       socket_node->type() !=
           grpc_core::channelz::BaseNode::EntityType::kListenSocket)) {
    return nullptr;
  }
  grpc_core::Json json = grpc_core::Json::FromObject({
      {"socket", socket_node->RenderJson()},
  });
  return grpc_core::channelz::ApplyHacks(grpc_core::JsonDump(json).c_str());
}
