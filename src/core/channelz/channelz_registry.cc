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
#include "src/core/config/config_vars.h"
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
  switch (json.type()) {
    case Json::Type::kArray: {
      Json::Array out;
      for (const auto& node : json.array()) {
        out.emplace_back(RemoveAdditionalInfo(node));
      }
      return Json::FromArray(std::move(out));
    } break;
    case Json::Type::kObject: {
      Json::Object out;
      for (const auto& [key, value] : json.object()) {
        if (key == "additionalInfo") continue;
        out[key] = RemoveAdditionalInfo(value);
      }
      return Json::FromObject(std::move(out));
    } break;
    default:
      return json;
  }
}

// TODO(ctiller): Temporary hack to remove fields that are objectionable to the
// protobuf parser (because we've not published them in protobuf yet).
char* ApplyHacks(const std::string& json_str) {
  return gpr_strdup(StripAdditionalInfoFromJson(json_str).c_str());
}
}  // namespace

std::string StripAdditionalInfoFromJson(absl::string_view json_str) {
  auto json = JsonParse(json_str);
  if (!json.ok()) return gpr_strdup(std::string(json_str).c_str());
  return JsonDump(RemoveAdditionalInfo(*json));
}

ChannelzRegistry* ChannelzRegistry::Default() {
  static ChannelzRegistry* singleton = new ChannelzRegistry();
  return singleton;
}

std::vector<WeakRefCountedPtr<BaseNode>>
ChannelzRegistry::InternalGetAllEntities() {
  return std::get<0>(QueryNodes(
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

void ChannelzRegistry::InternalRegister(BaseNode* node) {
  DCHECK_EQ(node->uuid_, -1);
  const size_t node_shard_index = NodeShardIndex(node);
  NodeShard& node_shard = node_shards_[node_shard_index];
  MutexLock lock(&node_shard.mu);
  node_shard.nursery.AddToHead(node);
}

void ChannelzRegistry::InternalUnregister(BaseNode* node) {
  const size_t node_shard_index = NodeShardIndex(node);
  NodeShard& node_shard = node_shards_[node_shard_index];
  node_shard.mu.Lock();
  CHECK_EQ(node->orphaned_index_, 0u);
  intptr_t uuid = node->uuid_.load(std::memory_order_relaxed);
  NodeList& remove_list = uuid == -1 ? node_shard.nursery : node_shard.numbered;
  remove_list.Remove(node);
  if (max_orphaned_per_shard_ == 0) {
    // We are not tracking orphaned nodes... remove from the index
    // if necessary, then exit out.
    node_shard.mu.Unlock();
    if (uuid != -1) {
      MutexLock lock(&index_mu_);
      index_.erase(uuid);
    }
    return;
  }
  NodeList& add_list =
      uuid != -1 ? node_shard.orphaned_numbered : node_shard.orphaned;
  // Ref counting: once a node becomes orphaned we add a single weak ref to it.
  // We hold that ref until it gets garbage collected later.
  node->WeakRef().release();
  node->orphaned_index_ = node_shard.next_orphan_index;
  CHECK_GT(node->orphaned_index_, 0u);
  ++node_shard.next_orphan_index;
  add_list.AddToHead(node);
  if (node_shard.TotalOrphaned() <= max_orphaned_per_shard_) {
    // Below recycling thresholds: just exit out
    node_shard.mu.Unlock();
    return;
  }
  CHECK_EQ(node_shard.TotalOrphaned(), max_orphaned_per_shard_ + 1);
  NodeList* gc_list;
  // choose the oldest node to evict, regardless of numbered or not
  if (node_shard.orphaned.tail == nullptr) {
    CHECK_NE(node_shard.orphaned_numbered.tail, nullptr);
    gc_list = &node_shard.orphaned_numbered;
  } else if (node_shard.orphaned_numbered.tail == nullptr) {
    gc_list = &node_shard.orphaned;
  } else if (node_shard.orphaned.tail->orphaned_index_ <
             node_shard.orphaned_numbered.tail->orphaned_index_) {
    gc_list = &node_shard.orphaned;
  } else {
    gc_list = &node_shard.orphaned_numbered;
  }
  auto* n = gc_list->tail;
  CHECK_GT(n->orphaned_index_, 0u);
  gc_list->Remove(n);
  // Note: we capture the reference to n previously added here, and release
  // it when this smart pointer is destroyed, outside of any locks.
  WeakRefCountedPtr<BaseNode> gcd_node(n);
  node_shard.mu.Unlock();
  if (gc_list == &node_shard.orphaned_numbered) {
    MutexLock lock(&index_mu_);
    intptr_t uuid = n->uuid_.load(std::memory_order_relaxed);
    index_.erase(uuid);
  }
}

void ChannelzRegistry::LoadConfig() {
  const auto max_orphaned = ConfigVars::Get().ChannelzMaxOrphanedNodes();
  if (max_orphaned == 0) {
    max_orphaned_per_shard_ = 0;
  } else {
    max_orphaned_per_shard_ = std::max<int>(max_orphaned / kNodeShards, 1);
  }
}

std::tuple<std::vector<WeakRefCountedPtr<BaseNode>>, bool>
ChannelzRegistry::QueryNodes(
    intptr_t start_node, absl::FunctionRef<bool(const BaseNode*)> discriminator,
    size_t max_results) {
  // Mitigate drain hotspotting by randomizing the drain order each query.
  std::vector<size_t> nursery_visitation_order;
  for (size_t i = 0; i < kNodeShards; ++i) {
    nursery_visitation_order.push_back(i);
  }
  absl::c_shuffle(nursery_visitation_order, SharedBitGen());
  // In the iteration below, even once we have max_results nodes, we need
  // to find the next node in order to know if we've hit the end.  If we get
  // through the loop without returning, then we return end=true.  But if we
  // find a node to add after we already have max_results nodes, then we
  // return with end=false before exiting the loop.  However, in the latter
  // case, we will have already increased the ref count of the next node,
  // so we need to unref it, but we can't do that while holding the lock.
  // So instead, we store it in node_after_end, which will be unreffed
  // after releasing the lock.
  WeakRefCountedPtr<BaseNode> node_after_end;
  std::vector<WeakRefCountedPtr<BaseNode>> result;
  MutexLock index_lock(&index_mu_);
  for (auto it = index_.lower_bound(start_node); it != index_.end(); ++it) {
    BaseNode* node = it->second;
    if (!discriminator(node)) continue;
    auto node_ref = node->WeakRefIfNonZero();
    if (node_ref == nullptr) continue;
    if (result.size() == max_results) {
      node_after_end = std::move(node_ref);
      return std::tuple(std::move(result), false);
    }
    result.emplace_back(std::move(node_ref));
  }
  for (auto nursery_index : nursery_visitation_order) {
    NodeShard& node_shard = node_shards_[nursery_index];
    MutexLock shard_lock(&node_shard.mu);
    for (auto [nursery, numbered] :
         {std::pair(&node_shard.nursery, &node_shard.numbered),
          std::pair(&node_shard.orphaned, &node_shard.orphaned_numbered)}) {
      if (nursery->head == nullptr) continue;
      BaseNode* n = nursery->head;
      while (n != nullptr) {
        if (!discriminator(n)) {
          n = n->next_;
          continue;
        }
        auto node_ref = n->WeakRefIfNonZero();
        if (node_ref == nullptr) {
          n = n->next_;
          continue;
        }
        BaseNode* next = n->next_;
        nursery->Remove(n);
        numbered->AddToHead(n);
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
  }
  CHECK(node_after_end == nullptr);
  return std::tuple(std::move(result), true);
}

WeakRefCountedPtr<BaseNode> ChannelzRegistry::InternalGet(intptr_t uuid) {
  MutexLock index_lock(&index_mu_);
  auto it = index_.find(uuid);
  if (it == index_.end()) return nullptr;
  BaseNode* node = it->second;
  return node->WeakRefIfNonZero();
}

intptr_t ChannelzRegistry::InternalNumberNode(BaseNode* node) {
  // node must be strongly owned still
  node->AssertStronglyOwned();
  const size_t node_shard_index = NodeShardIndex(node);
  NodeShard& node_shard = node_shards_[node_shard_index];
  MutexLock index_lock(&index_mu_);
  MutexLock lock(&node_shard.mu);
  intptr_t uuid = node->uuid_.load(std::memory_order_relaxed);
  if (uuid != -1) return uuid;
  uuid = uuid_generator_;
  ++uuid_generator_;
  node->uuid_ = uuid;
  if (node->orphaned_index_ > 0) {
    node_shard.orphaned.Remove(node);
    node_shard.orphaned_numbered.AddToHead(node);
  } else {
    node_shard.nursery.Remove(node);
    node_shard.numbered.AddToHead(node);
  }
  index_.emplace(uuid, node);
  return uuid;
}

bool ChannelzRegistry::NodeList::Holds(BaseNode* node) const {
  BaseNode* n = head;
  while (n != nullptr) {
    if (n == node) return true;
    n = n->next_;
  }
  return false;
}

void ChannelzRegistry::NodeList::AddToHead(BaseNode* node) {
  DCHECK(!Holds(node));
  ++count;
  if (head != nullptr) head->prev_ = node;
  node->next_ = head;
  node->prev_ = nullptr;
  head = node;
  if (tail == nullptr) tail = node;
  DCHECK(Holds(node));
}

void ChannelzRegistry::NodeList::Remove(BaseNode* node) {
  DCHECK(Holds(node));
  DCHECK_GT(count, 0u);
  --count;
  if (node->prev_ == nullptr) {
    head = node->next_;
    if (head == nullptr) {
      DCHECK_EQ(count, 0u);
      tail = nullptr;
      DCHECK(!Holds(node));
      return;
    }
  } else {
    node->prev_->next_ = node->next_;
  }
  if (node->next_ == nullptr) {
    tail = node->prev_;
  } else {
    node->next_->prev_ = node->prev_;
  }
  DCHECK(!Holds(node));
}
void ChannelzRegistry::TestOnlyReset() {
  auto* p = Default();
  p->uuid_generator_ = 1;
  p->LoadConfig();
  std::vector<WeakRefCountedPtr<BaseNode>> free_nodes;
  for (size_t i = 0; i < kNodeShards; i++) {
    MutexLock lock(&p->node_shards_[i].mu);
    CHECK(p->node_shards_[i].nursery.head == nullptr);
    CHECK(p->node_shards_[i].numbered.head == nullptr);
    while (p->node_shards_[i].orphaned.head != nullptr) {
      free_nodes.emplace_back(p->node_shards_[i].orphaned.head);
      p->node_shards_[i].orphaned.Remove(p->node_shards_[i].orphaned.head);
    }
    while (p->node_shards_[i].orphaned_numbered.head != nullptr) {
      free_nodes.emplace_back(p->node_shards_[i].orphaned_numbered.head);
      p->node_shards_[i].orphaned_numbered.Remove(
          p->node_shards_[i].orphaned_numbered.head);
    }
  }
  std::vector<NodeShard> replace_node_shards(kNodeShards);
  replace_node_shards.swap(p->node_shards_);
  MutexLock lock(&p->index_mu_);
  p->index_.clear();
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
  grpc_core::WeakRefCountedPtr<grpc_core::channelz::BaseNode> server_node =
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
  grpc_core::WeakRefCountedPtr<grpc_core::channelz::BaseNode> base_node =
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
  grpc_core::WeakRefCountedPtr<grpc_core::channelz::BaseNode> channel_node =
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
  grpc_core::WeakRefCountedPtr<grpc_core::channelz::BaseNode> subchannel_node =
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
  grpc_core::WeakRefCountedPtr<grpc_core::channelz::BaseNode> socket_node =
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
