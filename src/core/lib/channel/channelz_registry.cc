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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/sync.h"
#include "third_party/json/src/json.hpp"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

using nlohmann::json;

namespace grpc_core {
namespace channelz {
namespace {

// singleton instance of the registry.
ChannelzRegistry* g_channelz_registry = nullptr;

const int kPaginationLimit = 100;

}  // anonymous namespace

void ChannelzRegistry::Init() { g_channelz_registry = new ChannelzRegistry(); }

void ChannelzRegistry::Shutdown() { delete g_channelz_registry; }

ChannelzRegistry* ChannelzRegistry::Default() {
  GPR_DEBUG_ASSERT(g_channelz_registry != nullptr);
  return g_channelz_registry;
}

void ChannelzRegistry::InternalRegister(BaseNode* node) {
  MutexLock lock(&mu_);
  node->uuid_ = ++uuid_generator_;
  node_map_[node->uuid_] = node;
}

void ChannelzRegistry::InternalUnregister(intptr_t uuid) {
  GPR_ASSERT(uuid >= 1);
  MutexLock lock(&mu_);
  GPR_ASSERT(uuid <= uuid_generator_);
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
  if (!node->RefIfNonZero()) return nullptr;
  return RefCountedPtr<BaseNode>(node);
}

std::string ChannelzRegistry::InternalGetTopChannels(
    intptr_t start_channel_id) {
  // Get the list of channels up to the pagination limit.
  InlinedVector<RefCountedPtr<BaseNode>, 10> top_level_channels;
  RefCountedPtr<BaseNode> node_after_pagination_limit;
  {
    MutexLock lock(&mu_);
    for (auto it = node_map_.lower_bound(start_channel_id);
         it != node_map_.end(); ++it) {
      BaseNode* node = it->second;
      if (node->type() == BaseNode::EntityType::kTopLevelChannel &&
          node->RefIfNonZero()) {
        // Check if we are over pagination limit to determine if we need to set
        // the "end" element. If we don't go through this block, we know that
        // when the loop terminates, we have <= to kPaginationLimit.
        // Note that because we have already increased this node's
        // refcount, we need to decrease it, but we can't unref while
        // holding the lock, because this may lead to a deadlock.
        if (top_level_channels.size() == kPaginationLimit) {
          node_after_pagination_limit.reset(node);
          break;
        }
        top_level_channels.emplace_back(node);
      }
    }
  }
  // Construct json.
  json j;
  if (!top_level_channels.empty()) {
    j["channel"] = json::array();
    for (size_t i = 0; i < top_level_channels.size(); ++i) {
      j["channel"].push_back(top_level_channels[i]->RenderJson());
    }
  }
  if (node_after_pagination_limit == nullptr) j["end"] = true;
  return j.dump();
}

std::string ChannelzRegistry::InternalGetServers(intptr_t start_server_id) {
  // Get the list of servers up to the pagination limit.
  InlinedVector<RefCountedPtr<BaseNode>, 10> servers;
  RefCountedPtr<BaseNode> node_after_pagination_limit;
  {
    MutexLock lock(&mu_);
    for (auto it = node_map_.lower_bound(start_server_id);
         it != node_map_.end(); ++it) {
      BaseNode* node = it->second;
      if (node->type() == BaseNode::EntityType::kServer &&
          node->RefIfNonZero()) {
        // Check if we are over pagination limit to determine if we need to set
        // the "end" element. If we don't go through this block, we know that
        // when the loop terminates, we have <= to kPaginationLimit.
        // Note that because we have already increased this node's
        // refcount, we need to decrease it, but we can't unref while
        // holding the lock, because this may lead to a deadlock.
        if (servers.size() == kPaginationLimit) {
          node_after_pagination_limit.reset(node);
          break;
        }
        servers.emplace_back(node);
      }
    }
  }
  // Construct json.
  json j;
  if (!servers.empty()) {
    j["server"] = json::array();
    for (size_t i = 0; i < servers.size(); ++i) {
      j["server"].push_back(servers[i]->RenderJson());
    }
  }
  if (node_after_pagination_limit == nullptr) j["end"] = true;
  return j.dump();
}

void ChannelzRegistry::InternalLogAllEntities() {
  InlinedVector<RefCountedPtr<BaseNode>, 10> nodes;
  {
    MutexLock lock(&mu_);
    for (auto& p : node_map_) {
      BaseNode* node = p.second;
      if (node->RefIfNonZero()) {
        nodes.emplace_back(node);
      }
    }
  }
  for (size_t i = 0; i < nodes.size(); ++i) {
    std::string json = nodes[i]->RenderJsonString();
    gpr_log(GPR_INFO, "%s", json.c_str());
  }
}

}  // namespace channelz
}  // namespace grpc_core

char* grpc_channelz_get_top_channels(intptr_t start_channel_id) {
  return gpr_strdup(
      grpc_core::channelz::ChannelzRegistry::GetTopChannels(start_channel_id)
          .c_str());
}

char* grpc_channelz_get_servers(intptr_t start_server_id) {
  return gpr_strdup(
      grpc_core::channelz::ChannelzRegistry::GetServers(start_server_id)
          .c_str());
}

char* grpc_channelz_get_server(intptr_t server_id) {
  grpc_core::RefCountedPtr<grpc_core::channelz::BaseNode> server_node =
      grpc_core::channelz::ChannelzRegistry::Get(server_id);
  if (server_node == nullptr ||
      server_node->type() !=
          grpc_core::channelz::BaseNode::EntityType::kServer) {
    return nullptr;
  }
  json j = {{"server", server_node->RenderJson()}};
  return gpr_strdup(j.dump().c_str());
}

char* grpc_channelz_get_server_sockets(intptr_t server_id,
                                       intptr_t start_socket_id,
                                       intptr_t max_results) {
  grpc_core::RefCountedPtr<grpc_core::channelz::BaseNode> base_node =
      grpc_core::channelz::ChannelzRegistry::Get(server_id);
  if (base_node == nullptr ||
      base_node->type() != grpc_core::channelz::BaseNode::EntityType::kServer) {
    return nullptr;
  }
  // This cast is ok since we have just checked to make sure base_node is
  // actually a server node
  grpc_core::channelz::ServerNode* server_node =
      static_cast<grpc_core::channelz::ServerNode*>(base_node.get());
  return gpr_strdup(
      server_node->RenderServerSockets(start_socket_id, max_results).c_str());
}

char* grpc_channelz_get_channel(intptr_t channel_id) {
  grpc_core::RefCountedPtr<grpc_core::channelz::BaseNode> channel_node =
      grpc_core::channelz::ChannelzRegistry::Get(channel_id);
  if (channel_node == nullptr ||
      (channel_node->type() !=
           grpc_core::channelz::BaseNode::EntityType::kTopLevelChannel &&
       channel_node->type() !=
           grpc_core::channelz::BaseNode::EntityType::kInternalChannel)) {
    return nullptr;
  }
  json j = {{"channel", channel_node->RenderJson()}};
  return gpr_strdup(j.dump().c_str());
}

char* grpc_channelz_get_subchannel(intptr_t subchannel_id) {
  grpc_core::RefCountedPtr<grpc_core::channelz::BaseNode> subchannel_node =
      grpc_core::channelz::ChannelzRegistry::Get(subchannel_id);
  if (subchannel_node == nullptr ||
      subchannel_node->type() !=
          grpc_core::channelz::BaseNode::EntityType::kSubchannel) {
    return nullptr;
  }
  json j = {{"subchannel", subchannel_node->RenderJson()}};
  return gpr_strdup(j.dump().c_str());
}

char* grpc_channelz_get_socket(intptr_t socket_id) {
  grpc_core::RefCountedPtr<grpc_core::channelz::BaseNode> socket_node =
      grpc_core::channelz::ChannelzRegistry::Get(socket_id);
  if (socket_node == nullptr ||
      socket_node->type() !=
          grpc_core::channelz::BaseNode::EntityType::kSocket) {
    return nullptr;
  }
  json j = {{"socket", socket_node->RenderJson()}};
  return gpr_strdup(j.dump().c_str());
}
