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
  std::vector<RefCountedPtr<BaseNode>> nodes;
  node_map_->IterateNodes(0, std::nullopt,
                          [&nodes](RefCountedPtr<BaseNode> node) {
                            nodes.push_back(node);
                            return true;
                          });
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

void ChannelzRegistry::LegacyNodeMap::IterateNodes(
    intptr_t start_node, std::optional<BaseNode::EntityType> entity_type,
    absl::FunctionRef<bool(RefCountedPtr<BaseNode> node)> callback) {
  MutexLock lock(&mu_);
  for (auto it = node_map_.lower_bound(start_node); it != node_map_.end();
       ++it) {
    BaseNode* node = it->second;
    if (entity_type.has_value() && node->type() != entity_type) continue;
    auto node_ref = node->RefIfNonZero();
    if (node_ref == nullptr) continue;
    if (!callback(std::move(node_ref))) break;
  }
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
  const size_t index_shard_index = IndexShardIndex(node->uuid_);
  NodeIndex& index_shard = node_index_[index_shard_index];
  MutexLock index_lock(&index_shard.mu);
  index_shard.map.erase(node->uuid_ >> kIndexShardShiftBits);
}

void ChannelzRegistry::ShardedNodeMap::IterateNodes(
    intptr_t start_node, std::optional<BaseNode::EntityType> entity_type,
    absl::FunctionRef<bool(RefCountedPtr<BaseNode> node)> callback) {
  NumberNurseryNodes();
  intptr_t end = uuid_generator_.load(std::memory_order_relaxed);
  for (intptr_t uuid = start_node; uuid < end; ++uuid) {
    const size_t index_shard_index = IndexShardIndex(uuid);
    NodeIndex& index_shard = node_index_[index_shard_index];
    MutexLock index_lock(&index_shard.mu);
    auto it = index_shard.map.find(uuid >> kIndexShardShiftBits);
    if (it == index_shard.map.end()) continue;
    BaseNode* node = it->second;
    if (entity_type.has_value() && node->type() != entity_type) continue;
    auto node_ref = node->RefIfNonZero();
    if (node_ref == nullptr) continue;
    if (!callback(std::move(node_ref))) break;
  }
}

void ChannelzRegistry::ShardedNodeMap::AddNodeToHead(BaseNode* node,
                                                     BaseNode*& head) {
  if (head == nullptr) {
    head = node;
    node->prev_ = node->next_ = node;
  } else {
    node->prev_ = head->prev_;
    node->next_ = head;
    node->next_->prev_ = node;
    node->prev_->next_ = node;
  }
}

void ChannelzRegistry::ShardedNodeMap::RemoveNodeFromHead(BaseNode* node,
                                                          BaseNode*& head) {
  if (node->next_ == node) {
    CHECK_EQ(head, node);
    head = nullptr;
  } else {
    node->prev_->next_ = node->next_;
    node->next_->prev_ = node->prev_;
    if (head == node) head = node->next_;
  }
}

void ChannelzRegistry::ShardedNodeMap::NumberNurseryNodes() {
  for (size_t i = 0; i < kNodeShards; ++i) {
    BaseNodeList& node_shard = node_list_[i];
    MutexLock lock(&node_shard.mu);
    BaseNode* nursery = std::exchange(node_shard.nursery, nullptr);
    while (nursery != nullptr) {
      BaseNode* n = nursery->next_;
      RemoveNodeFromHead(n, nursery);
      AddNodeToHead(n, node_shard.numbered);
      n->uuid_ = uuid_generator_.fetch_add(1);
      const size_t index_shard_index = IndexShardIndex(n->uuid_);
      NodeIndex& index_shard = node_index_[index_shard_index];
      MutexLock index_lock(&index_shard.mu);
      index_shard.map.emplace(n->uuid_ >> kIndexShardShiftBits, n);
    }
  }
}

RefCountedPtr<BaseNode> ChannelzRegistry::ShardedNodeMap::GetNode(
    intptr_t uuid) {
  const size_t index_shard_index = IndexShardIndex(uuid);
  NodeIndex& index_shard = node_index_[index_shard_index];
  MutexLock lock(&index_shard.mu);
  auto it = index_shard.map.find(uuid >> kIndexShardShiftBits);
  if (it == index_shard.map.end()) return nullptr;
  BaseNode* node = it->second;
  return node->RefIfNonZero();
}

intptr_t ChannelzRegistry::ShardedNodeMap::NumberNode(BaseNode* node) {
  const size_t node_shard_index = NodeShardIndex(node);
  BaseNodeList& node_shard = node_list_[node_shard_index];
  MutexLock lock(&node_shard.mu);
  intptr_t uuid = node->uuid_.load(std::memory_order_relaxed);
  if (uuid != -1) return uuid;
  uuid = uuid_generator_.fetch_add(1);
  node->uuid_ = uuid;
  RemoveNodeFromHead(node, node_shard.nursery);
  AddNodeToHead(node, node_shard.numbered);
  const size_t index_shard_index = IndexShardIndex(uuid);
  NodeIndex& index_shard = node_index_[index_shard_index];
  MutexLock index_lock(&index_shard.mu);
  index_shard.map.emplace(uuid >> kIndexShardShiftBits, node);
  return uuid;
}

size_t ChannelzRegistry::ShardedNodeMap::NodeShardIndex(BaseNode* node) {
  return absl::HashOf(static_cast<void*>(node)) % kNodeShards;
}

size_t ChannelzRegistry::ShardedNodeMap::IndexShardIndex(intptr_t uuid) {
  return uuid & (kIndexShards - 1);
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
