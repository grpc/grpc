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
#include "src/core/lib/gprpp/sync.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include <cstring>

namespace grpc_core {
namespace channelz {
namespace {

// singleton instance of the registry.
ChannelzRegistry* g_channelz_registry = nullptr;

const int kPaginationLimit = 100;

}  // anonymous namespace

void ChannelzRegistry::Init() { g_channelz_registry = New<ChannelzRegistry>(); }

void ChannelzRegistry::Shutdown() { Delete(g_channelz_registry); }

ChannelzRegistry* ChannelzRegistry::Default() {
  GPR_DEBUG_ASSERT(g_channelz_registry != nullptr);
  return g_channelz_registry;
}

ChannelzRegistry::ChannelzRegistry() { gpr_mu_init(&mu_); }

ChannelzRegistry::~ChannelzRegistry() { gpr_mu_destroy(&mu_); }

void ChannelzRegistry::InternalRegister(BaseNode* node) {
  MutexLock lock(&mu_);
  entities_.push_back(node);
  node->uuid_ = ++uuid_generator_;
}

void ChannelzRegistry::MaybePerformCompactionLocked() {
  constexpr double kEmptinessTheshold = 1. / 3;
  double emptiness_ratio =
      double(num_empty_slots_) / double(entities_.capacity());
  if (emptiness_ratio > kEmptinessTheshold) {
    int front = 0;
    for (size_t i = 0; i < entities_.size(); ++i) {
      if (entities_[i] != nullptr) {
        entities_[front++] = entities_[i];
      }
    }
    for (int i = 0; i < num_empty_slots_; ++i) {
      entities_.pop_back();
    }
    num_empty_slots_ = 0;
  }
}

int ChannelzRegistry::FindByUuidLocked(intptr_t target_uuid,
                                       bool direct_hit_needed) {
  int left = 0;
  int right = int(entities_.size() - 1);
  while (left <= right) {
    int true_middle = left + (right - left) / 2;
    int first_non_null = true_middle;
    while (first_non_null < right && entities_[first_non_null] == nullptr) {
      first_non_null++;
    }
    if (entities_[first_non_null] == nullptr) {
      right = true_middle - 1;
      continue;
    }
    intptr_t uuid = entities_[first_non_null]->uuid();
    if (uuid == target_uuid) {
      return int(first_non_null);
    }
    if (uuid < target_uuid) {
      left = first_non_null + 1;
    } else {
      right = true_middle - 1;
    }
  }
  return direct_hit_needed ? -1 : left;
}

void ChannelzRegistry::InternalUnregister(intptr_t uuid) {
  GPR_ASSERT(uuid >= 1);
  MutexLock lock(&mu_);
  GPR_ASSERT(uuid <= uuid_generator_);
  int idx = FindByUuidLocked(uuid, true);
  GPR_ASSERT(idx >= 0);
  entities_[idx] = nullptr;
  num_empty_slots_++;
  MaybePerformCompactionLocked();
}

BaseNode* ChannelzRegistry::InternalGet(intptr_t uuid) {
  MutexLock lock(&mu_);
  if (uuid < 1 || uuid > uuid_generator_) {
    return nullptr;
  }
  int idx = FindByUuidLocked(uuid, true);
  return idx < 0 ? nullptr : entities_[idx];
}

char* ChannelzRegistry::InternalGetTopChannels(intptr_t start_channel_id) {
  MutexLock lock(&mu_);
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* json_iterator = nullptr;
  InlinedVector<BaseNode*, 10> top_level_channels;
  bool reached_pagination_limit = false;
  int start_idx = GPR_MAX(FindByUuidLocked(start_channel_id, false), 0);
  for (size_t i = start_idx; i < entities_.size(); ++i) {
    if (entities_[i] != nullptr &&
        entities_[i]->type() ==
            grpc_core::channelz::BaseNode::EntityType::kTopLevelChannel &&
        entities_[i]->uuid() >= start_channel_id) {
      // check if we are over pagination limit to determine if we need to set
      // the "end" element. If we don't go through this block, we know that
      // when the loop terminates, we have <= to kPaginationLimit.
      if (top_level_channels.size() == kPaginationLimit) {
        reached_pagination_limit = true;
        break;
      }
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
  if (!reached_pagination_limit) {
    grpc_json_create_child(nullptr, json, "end", nullptr, GRPC_JSON_TRUE,
                           false);
  }
  char* json_str = grpc_json_dump_to_string(top_level_json, 0);
  grpc_json_destroy(top_level_json);
  return json_str;
}

char* ChannelzRegistry::InternalGetServers(intptr_t start_server_id) {
  MutexLock lock(&mu_);
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* json_iterator = nullptr;
  InlinedVector<BaseNode*, 10> servers;
  bool reached_pagination_limit = false;
  int start_idx = GPR_MAX(FindByUuidLocked(start_server_id, false), 0);
  for (size_t i = start_idx; i < entities_.size(); ++i) {
    if (entities_[i] != nullptr &&
        entities_[i]->type() ==
            grpc_core::channelz::BaseNode::EntityType::kServer &&
        entities_[i]->uuid() >= start_server_id) {
      // check if we are over pagination limit to determine if we need to set
      // the "end" element. If we don't go through this block, we know that
      // when the loop terminates, we have <= to kPaginationLimit.
      if (servers.size() == kPaginationLimit) {
        reached_pagination_limit = true;
        break;
      }
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
  if (!reached_pagination_limit) {
    grpc_json_create_child(nullptr, json, "end", nullptr, GRPC_JSON_TRUE,
                           false);
  }
  char* json_str = grpc_json_dump_to_string(top_level_json, 0);
  grpc_json_destroy(top_level_json);
  return json_str;
}

void ChannelzRegistry::InternalLogAllEntities() {
  MutexLock lock(&mu_);
  for (size_t i = 0; i < entities_.size(); ++i) {
    if (entities_[i] != nullptr) {
      char* json = entities_[i]->RenderJsonString();
      gpr_log(GPR_INFO, "%s", json);
      gpr_free(json);
    }
  }
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

char* grpc_channelz_get_server(intptr_t server_id) {
  grpc_core::channelz::BaseNode* server_node =
      grpc_core::channelz::ChannelzRegistry::Get(server_id);
  if (server_node == nullptr ||
      server_node->type() !=
          grpc_core::channelz::BaseNode::EntityType::kServer) {
    return nullptr;
  }
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* channel_json = server_node->RenderJson();
  channel_json->key = "server";
  grpc_json_link_child(json, channel_json, nullptr);
  char* json_str = grpc_json_dump_to_string(top_level_json, 0);
  grpc_json_destroy(top_level_json);
  return json_str;
}

char* grpc_channelz_get_server_sockets(intptr_t server_id,
                                       intptr_t start_socket_id,
                                       intptr_t max_results) {
  grpc_core::channelz::BaseNode* base_node =
      grpc_core::channelz::ChannelzRegistry::Get(server_id);
  if (base_node == nullptr ||
      base_node->type() != grpc_core::channelz::BaseNode::EntityType::kServer) {
    return nullptr;
  }
  // This cast is ok since we have just checked to make sure base_node is
  // actually a server node
  grpc_core::channelz::ServerNode* server_node =
      static_cast<grpc_core::channelz::ServerNode*>(base_node);
  return server_node->RenderServerSockets(start_socket_id, max_results);
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
