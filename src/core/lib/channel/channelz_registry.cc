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

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channelz_registry.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include <grpc/grpc.h>
#include <grpc/support/json.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_writer.h"

namespace grpc_core {
namespace channelz {
namespace {

const int kPaginationLimit = 100;

}  // anonymous namespace

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
  return node->RefIfNonZero();
}

std::string ChannelzRegistry::InternalGetTopChannels(
    intptr_t start_channel_id) {
  std::vector<RefCountedPtr<BaseNode>> top_level_channels;
  RefCountedPtr<BaseNode> node_after_pagination_limit;
  {
    MutexLock lock(&mu_);
    for (auto it = node_map_.lower_bound(start_channel_id);
         it != node_map_.end(); ++it) {
      BaseNode* node = it->second;
      RefCountedPtr<BaseNode> node_ref;
      if (node->type() == BaseNode::EntityType::kTopLevelChannel &&
          (node_ref = node->RefIfNonZero()) != nullptr) {
        // Check if we are over pagination limit to determine if we need to set
        // the "end" element. If we don't go through this block, we know that
        // when the loop terminates, we have <= to kPaginationLimit.
        // Note that because we have already increased this node's
        // refcount, we need to decrease it, but we can't unref while
        // holding the lock, because this may lead to a deadlock.
        if (top_level_channels.size() == kPaginationLimit) {
          node_after_pagination_limit = std::move(node_ref);
          break;
        }
        top_level_channels.emplace_back(std::move(node_ref));
      }
    }
  }
  Json::Object object;
  if (!top_level_channels.empty()) {
    // Create list of channels.
    Json::Array array;
    for (size_t i = 0; i < top_level_channels.size(); ++i) {
      array.emplace_back(top_level_channels[i]->RenderJson());
    }
    object["channel"] = Json::FromArray(std::move(array));
  }
  if (node_after_pagination_limit == nullptr) {
    object["end"] = Json::FromBool(true);
  }
  return JsonDump(Json::FromObject(std::move(object)));
}

std::string ChannelzRegistry::InternalGetServers(intptr_t start_server_id) {
  std::vector<RefCountedPtr<BaseNode>> servers;
  RefCountedPtr<BaseNode> node_after_pagination_limit;
  {
    MutexLock lock(&mu_);
    for (auto it = node_map_.lower_bound(start_server_id);
         it != node_map_.end(); ++it) {
      BaseNode* node = it->second;
      RefCountedPtr<BaseNode> node_ref;
      if (node->type() == BaseNode::EntityType::kServer &&
          (node_ref = node->RefIfNonZero()) != nullptr) {
        // Check if we are over pagination limit to determine if we need to set
        // the "end" element. If we don't go through this block, we know that
        // when the loop terminates, we have <= to kPaginationLimit.
        // Note that because we have already increased this node's
        // refcount, we need to decrease it, but we can't unref while
        // holding the lock, because this may lead to a deadlock.
        if (servers.size() == kPaginationLimit) {
          node_after_pagination_limit = std::move(node_ref);
          break;
        }
        servers.emplace_back(std::move(node_ref));
      }
    }
  }
  Json::Object object;
  if (!servers.empty()) {
    // Create list of servers.
    Json::Array array;
    for (size_t i = 0; i < servers.size(); ++i) {
      array.emplace_back(servers[i]->RenderJson());
    }
    object["server"] = Json::FromArray(std::move(array));
  }
  if (node_after_pagination_limit == nullptr) {
    object["end"] = Json::FromBool(true);
  }
  return JsonDump(Json::FromObject(std::move(object)));
}

void ChannelzRegistry::InternalLogAllEntities() {
  std::vector<RefCountedPtr<BaseNode>> nodes;
  {
    MutexLock lock(&mu_);
    for (auto& p : node_map_) {
      RefCountedPtr<BaseNode> node = p.second->RefIfNonZero();
      if (node != nullptr) {
        nodes.emplace_back(std::move(node));
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
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  return gpr_strdup(
      grpc_core::channelz::ChannelzRegistry::GetTopChannels(start_channel_id)
          .c_str());
}

char* grpc_channelz_get_servers(intptr_t start_server_id) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  return gpr_strdup(
      grpc_core::channelz::ChannelzRegistry::GetServers(start_server_id)
          .c_str());
}

char* grpc_channelz_get_server(intptr_t server_id) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
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
  return gpr_strdup(grpc_core::JsonDump(json).c_str());
}

char* grpc_channelz_get_server_sockets(intptr_t server_id,
                                       intptr_t start_socket_id,
                                       intptr_t max_results) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
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
  return gpr_strdup(
      server_node->RenderServerSockets(start_socket_id, max_results).c_str());
}

char* grpc_channelz_get_channel(intptr_t channel_id) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
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
  return gpr_strdup(grpc_core::JsonDump(json).c_str());
}

char* grpc_channelz_get_subchannel(intptr_t subchannel_id) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
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
  return gpr_strdup(grpc_core::JsonDump(json).c_str());
}

char* grpc_channelz_get_socket(intptr_t socket_id) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_core::RefCountedPtr<grpc_core::channelz::BaseNode> socket_node =
      grpc_core::channelz::ChannelzRegistry::Get(socket_id);
  if (socket_node == nullptr ||
      socket_node->type() !=
          grpc_core::channelz::BaseNode::EntityType::kSocket) {
    return nullptr;
  }
  grpc_core::Json json = grpc_core::Json::FromObject({
      {"socket", socket_node->RenderJson()},
  });
  return gpr_strdup(grpc_core::JsonDump(json).c_str());
}
