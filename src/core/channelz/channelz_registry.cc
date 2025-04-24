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
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
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
#include "src/core/util/shared_bit_gen.h"
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

std::vector<RefCountedPtr<BaseNode>>
ChannelzRegistry::InternalGetAllEntities() {
  return std::get<0>(node_map_->QueryNodes(
      0, [](const BaseNode*) { return true; },
      std::numeric_limits<size_t>::max()));
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

std::unique_ptr<ChannelzRegistry::NodeMapInterface>
ChannelzRegistry::MakeNodeMap() {
  if (IsShardChannelzIndexEnabled()) {
    return std::make_unique<ShardedNodeMap>();
  } else {
    return std::make_unique<LegacyNodeMap>();
  }
}

///////////////////////////////////////////////////////////////////////////////
// LegacyNodeMap

void ChannelzRegistry::LegacyNodeMap::Register(BaseNode* node) {
  MutexLock lock(&mu_);
  node->uuid_ = uuid_generator_;
  ++uuid_generator_;
  node_map_[node->uuid_] = node;
}

void ChannelzRegistry::LegacyNodeMap::Unregister(BaseNode* node) {
  const intptr_t uuid = node->uuid_;
  CHECK_GE(uuid, 1);
  MutexLock lock(&mu_);
  CHECK(uuid <= uuid_generator_);
  node_map_.erase(uuid);
}

std::tuple<std::vector<RefCountedPtr<BaseNode>>, bool>
ChannelzRegistry::LegacyNodeMap::QueryNodes(
    intptr_t start_node, absl::FunctionRef<bool(const BaseNode*)> filter,
    size_t max_results) {
  RefCountedPtr<BaseNode> node_after_end;
  std::vector<RefCountedPtr<BaseNode>> result;
  MutexLock lock(&mu_);
  for (auto it = node_map_.lower_bound(start_node); it != node_map_.end();
       ++it) {
    BaseNode* node = it->second;
    if (!filter(node)) continue;
    auto node_ref = node->RefIfNonZero();
    if (node_ref == nullptr) continue;
    if (result.size() == max_results) {
      node_after_end = node_ref;
      break;
    }
    result.emplace_back(node_ref);
  }
  return std::tuple(std::move(result), node_after_end == nullptr);
}

RefCountedPtr<BaseNode> ChannelzRegistry::LegacyNodeMap::GetNode(
    intptr_t uuid) {
  MutexLock lock(&mu_);
  if (uuid < 1 || uuid > uuid_generator_) return nullptr;
  auto it = node_map_.find(uuid);
  if (it == node_map_.end()) return nullptr;
  // Found node.  Return only if its refcount is not zero (i.e., when we
  // know that there is no other thread about to destroy it).
  BaseNode* node = it->second;
  return node->RefIfNonZero();
}

///////////////////////////////////////////////////////////////////////////////
// ShardedNodeMap

void ChannelzRegistry::ShardedNodeMap::Register(BaseNode* node) {
  DCHECK_EQ(node->uuid_, -1);
  const size_t node_shard_index = NodeShardIndex(node);
  BaseNodeList& node_shard = node_list_[node_shard_index];
  MutexLock lock(&node_shard.mu);
  AddNodeToHead(node, node_shard.nursery);
}

void ChannelzRegistry::ShardedNodeMap::Unregister(BaseNode* node) {
  const size_t node_shard_index = NodeShardIndex(node);
  BaseNodeList& node_shard = node_list_[node_shard_index];
  node_shard.mu.Lock();
  const bool id_allocated = node->uuid_.load(std::memory_order_relaxed) != -1;
  BaseNode*& head = id_allocated ? node_shard.numbered : node_shard.nursery;
  RemoveNodeFromHead(node, head);
  node_shard.mu.Unlock();
  if (node->uuid_ == -1) return;
  MutexLock index_lock(&index_mu_);
  index_.erase(node->uuid_);
}

std::tuple<std::vector<RefCountedPtr<BaseNode>>, bool>
ChannelzRegistry::ShardedNodeMap::QueryNodes(
    intptr_t start_node, absl::FunctionRef<bool(const BaseNode*)> filter,
    size_t max_results) {
  // Mitigate drain hotspotting by randomizing the drain order each query.
  std::vector<size_t> nursery_visitation_order;
  for (size_t i = 0; i < kNodeShards; ++i) {
    nursery_visitation_order.push_back(i);
  }
  absl::c_shuffle(nursery_visitation_order, SharedBitGen());
  RefCountedPtr<BaseNode> node_after_end;
  std::vector<RefCountedPtr<BaseNode>> result;
  MutexLock index_lock(&index_mu_);
  for (auto it = index_.lower_bound(start_node); it != index_.end(); ++it) {
    BaseNode* node = it->second;
    if (!filter(node)) continue;
    auto node_ref = node->RefIfNonZero();
    if (node_ref == nullptr) continue;
    if (result.size() == max_results) {
      node_after_end = std::move(node_ref);
      return std::tuple(std::move(result), false);
    }
    result.emplace_back(std::move(node_ref));
  }
  for (auto nursery_index : nursery_visitation_order) {
    BaseNodeList& node_shard = node_list_[nursery_index];
    MutexLock shard_lock(&node_shard.mu);
    if (node_shard.nursery == nullptr) continue;
    BaseNode* n = node_shard.nursery;
    while (n != nullptr) {
      if (!filter(n)) {
        n = n->next_;
        continue;
      }
      auto node_ref = n->RefIfNonZero();
      if (node_ref == nullptr) {
        n = n->next_;
        continue;
      }
      BaseNode* next = n->next_;
      RemoveNodeFromHead(n, node_shard.nursery);
      AddNodeToHead(n, node_shard.numbered);
      n->uuid_ = uuid_generator_;
      ++uuid_generator_;
      index_.emplace(n->uuid_, n);
      if (n->uuid_ >= start_node) {
        if (result.size() == max_results) {
          node_after_end = std::move(node_ref);
          return std::tuple(std::move(result), false);
        }
        result.emplace_back(std::move(node_ref));
      }
      n = next;
    }
  }
  CHECK(node_after_end == nullptr);
  return std::tuple(std::move(result), true);
}

void ChannelzRegistry::ShardedNodeMap::AddNodeToHead(BaseNode* node,
                                                     BaseNode*& head) {
  if (head == nullptr) {
    head = node;
    node->prev_ = node->next_ = nullptr;
    return;
  }
  DCHECK_EQ(head->prev_, nullptr);
  node->next_ = head;
  node->prev_ = nullptr;
  head->prev_ = node;
  head = node;
}

void ChannelzRegistry::ShardedNodeMap::RemoveNodeFromHead(BaseNode* node,
                                                          BaseNode*& head) {
  if (node == head) {
    DCHECK_EQ(node->prev_, nullptr);
    head = node->next_;
    if (head != nullptr) head->prev_ = nullptr;
    return;
  }
  node->prev_->next_ = node->next_;
  if (node->next_ != nullptr) node->next_->prev_ = node->prev_;
}

RefCountedPtr<BaseNode> ChannelzRegistry::ShardedNodeMap::GetNode(
    intptr_t uuid) {
  MutexLock index_lock(&index_mu_);
  auto it = index_.find(uuid);
  if (it == index_.end()) return nullptr;
  BaseNode* node = it->second;
  return node->RefIfNonZero();
}

intptr_t ChannelzRegistry::ShardedNodeMap::NumberNode(BaseNode* node) {
  const size_t node_shard_index = NodeShardIndex(node);
  BaseNodeList& node_shard = node_list_[node_shard_index];
  MutexLock index_lock(&index_mu_);
  MutexLock lock(&node_shard.mu);
  intptr_t uuid = node->uuid_.load(std::memory_order_relaxed);
  if (uuid != -1) return uuid;
  uuid = uuid_generator_;
  ++uuid_generator_;
  node->uuid_ = uuid;
  RemoveNodeFromHead(node, node_shard.nursery);
  AddNodeToHead(node, node_shard.numbered);
  index_.emplace(uuid, node);
  return uuid;
}

size_t ChannelzRegistry::ShardedNodeMap::NodeShardIndex(BaseNode* node) {
  return absl::HashOf(static_cast<void*>(node)) % kNodeShards;
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
