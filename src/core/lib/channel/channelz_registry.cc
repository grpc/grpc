/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/mutex_lock.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include <cstring>

namespace grpc_core {
namespace channelz {
namespace {

// singleton instance of the registry.
ChannelzRegistry* g_channelz_registry = nullptr;

}  // anonymous namespace

void ChannelzRegistry::Init() { g_channelz_registry = New<ChannelzRegistry>(); }

void ChannelzRegistry::Shutdown() { Delete(g_channelz_registry); }

ChannelzRegistry* ChannelzRegistry::Default() {
  GPR_DEBUG_ASSERT(g_channelz_registry != nullptr);
  return g_channelz_registry;
}

ChannelzRegistry::ChannelzRegistry() { gpr_mu_init(&mu_); }

ChannelzRegistry::~ChannelzRegistry() { gpr_mu_destroy(&mu_); }

intptr_t ChannelzRegistry::InternalRegister(BaseNode* node) {
  MutexLock lock(&mu_);
  entities_.push_back(node);
  intptr_t uuid = entities_.size();
  return uuid;
}

void ChannelzRegistry::InternalUnregister(intptr_t uuid) {
  GPR_ASSERT(uuid >= 1);
  MutexLock lock(&mu_);
  GPR_ASSERT(static_cast<size_t>(uuid) <= entities_.size());
  entities_[uuid - 1] = nullptr;
}

BaseNode* ChannelzRegistry::InternalGet(intptr_t uuid) {
  MutexLock lock(&mu_);
  if (uuid < 1 || uuid > static_cast<intptr_t>(entities_.size())) {
    return nullptr;
  }
  return entities_[uuid - 1];
}

char* ChannelzRegistry::InternalGetTopChannels(intptr_t start_channel_id) {
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* json_iterator = nullptr;
  InlinedVector<BaseNode*, 10> top_level_channels;
  // uuids index into entities one-off (idx 0 is really uuid 1, since 0 is
  // reserved). However, we want to support requests coming in with
  // start_channel_id=0, which signifies "give me everything." Hence this
  // funky looking line below.
  size_t start_idx = start_channel_id == 0 ? 0 : start_channel_id - 1;
  for (size_t i = start_idx; i < entities_.size(); ++i) {
    if (entities_[i] != nullptr &&
        entities_[i]->type() ==
            grpc_core::channelz::BaseNode::EntityType::kTopLevelChannel) {
      top_level_channels.push_back(entities_[i]);
    }
  }
  if (!top_level_channels.empty()) {
    // create list of channels
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "channel", nullptr, GRPC_JSON_ARRAY, false);
    for (size_t i = 0; i < top_level_channels.size(); ++i) {
      grpc_json* channel_json = top_level_channels[i]->RenderJson();
      json_iterator =
          grpc_json_link_child(array_parent, channel_json, json_iterator);
    }
  }
  // For now we do not have any pagination rules. In the future we could
  // pick a constant for max_channels_sent for a GetTopChannels request.
  // Tracking: https://github.com/grpc/grpc/issues/16019.
  json_iterator = grpc_json_create_child(nullptr, json, "end", nullptr,
                                         GRPC_JSON_TRUE, false);
  char* json_str = grpc_json_dump_to_string(top_level_json, 0);
  grpc_json_destroy(top_level_json);
  return json_str;
}

char* ChannelzRegistry::InternalGetServers(intptr_t start_server_id) {
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* json_iterator = nullptr;
  InlinedVector<BaseNode*, 10> servers;
  // uuids index into entities one-off (idx 0 is really uuid 1, since 0 is
  // reserved). However, we want to support requests coming in with
  // start_server_id=0, which signifies "give me everything."
  size_t start_idx = start_server_id == 0 ? 0 : start_server_id - 1;
  for (size_t i = start_idx; i < entities_.size(); ++i) {
    if (entities_[i] != nullptr &&
        entities_[i]->type() ==
            grpc_core::channelz::BaseNode::EntityType::kServer) {
      servers.push_back(entities_[i]);
    }
  }
  if (!servers.empty()) {
    // create list of servers
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "server", nullptr, GRPC_JSON_ARRAY, false);
    for (size_t i = 0; i < servers.size(); ++i) {
      grpc_json* server_json = servers[i]->RenderJson();
      json_iterator =
          grpc_json_link_child(array_parent, server_json, json_iterator);
    }
  }
  // For now we do not have any pagination rules. In the future we could
  // pick a constant for max_channels_sent for a GetServers request.
  // Tracking: https://github.com/grpc/grpc/issues/16019.
  json_iterator = grpc_json_create_child(nullptr, json, "end", nullptr,
                                         GRPC_JSON_TRUE, false);
  char* json_str = grpc_json_dump_to_string(top_level_json, 0);
  grpc_json_destroy(top_level_json);
  return json_str;
}

}  // namespace channelz
}  // namespace grpc_core

char* grpc_channelz_get_top_channels(intptr_t start_channel_id) {
  return grpc_core::channelz::ChannelzRegistry::GetTopChannels(
      start_channel_id);
}

char* grpc_channelz_get_servers(intptr_t start_server_id) {
  return grpc_core::channelz::ChannelzRegistry::GetServers(start_server_id);
}

char* grpc_channelz_get_channel(intptr_t channel_id) {
  grpc_core::channelz::BaseNode* channel_node =
      grpc_core::channelz::ChannelzRegistry::Get(channel_id);
  if (channel_node == nullptr ||
      (channel_node->type() !=
           grpc_core::channelz::BaseNode::EntityType::kTopLevelChannel &&
       channel_node->type() !=
           grpc_core::channelz::BaseNode::EntityType::kInternalChannel)) {
    return nullptr;
  }
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* channel_json = channel_node->RenderJson();
  channel_json->key = "channel";
  grpc_json_link_child(json, channel_json, nullptr);
  char* json_str = grpc_json_dump_to_string(top_level_json, 0);
  grpc_json_destroy(top_level_json);
  return json_str;
}

char* grpc_channelz_get_subchannel(intptr_t subchannel_id) {
  grpc_core::channelz::BaseNode* subchannel_node =
      grpc_core::channelz::ChannelzRegistry::Get(subchannel_id);
  if (subchannel_node == nullptr ||
      subchannel_node->type() !=
          grpc_core::channelz::BaseNode::EntityType::kSubchannel) {
    return nullptr;
  }
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* subchannel_json = subchannel_node->RenderJson();
  subchannel_json->key = "subchannel";
  grpc_json_link_child(json, subchannel_json, nullptr);
  char* json_str = grpc_json_dump_to_string(top_level_json, 0);
  grpc_json_destroy(top_level_json);
  return json_str;
}

char* grpc_channelz_get_socket(intptr_t socket_id) {
  grpc_core::channelz::BaseNode* socket_node =
      grpc_core::channelz::ChannelzRegistry::Get(socket_id);
  if (socket_node == nullptr ||
      socket_node->type() !=
          grpc_core::channelz::BaseNode::EntityType::kSocket) {
    return nullptr;
  }
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* socket_json = socket_node->RenderJson();
  socket_json->key = "socket";
  grpc_json_link_child(json, socket_json, nullptr);
  char* json_str = grpc_json_dump_to_string(top_level_json, 0);
  grpc_json_destroy(top_level_json);
  return json_str;
}
