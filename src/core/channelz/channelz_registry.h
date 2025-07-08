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

#ifndef GRPC_SRC_CORE_CHANNELZ_CHANNELZ_REGISTRY_H
#define GRPC_SRC_CORE_CHANNELZ_CHANNELZ_REGISTRY_H

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <map>
#include <string>

#include "absl/container/btree_map.h"
#include "absl/functional/function_ref.h"
#include "src/core/channelz/channelz.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"

namespace grpc_core {
namespace channelz {

// singleton registry object to track all objects that are needed to support
// channelz bookkeeping. All objects share globally distributed uuids.
class ChannelzRegistry final {
 public:
  static void Register(BaseNode* node) {
    return Default()->InternalRegister(node);
  }
  static void Unregister(BaseNode* node) {
    Default()->InternalUnregister(node);
  }
  static WeakRefCountedPtr<BaseNode> Get(intptr_t uuid) {
    return Default()->InternalGet(uuid);
  }
  static intptr_t NumberNode(BaseNode* node) {
    return Default()->InternalNumberNode(node);
  }

  static WeakRefCountedPtr<SubchannelNode> GetSubchannel(intptr_t uuid) {
    return Default()
        ->InternalGetTyped<SubchannelNode, BaseNode::EntityType::kSubchannel>(
            uuid);
  }

  static WeakRefCountedPtr<ChannelNode> GetChannel(intptr_t uuid) {
    auto node = Default()->InternalGet(uuid);
    if (node == nullptr) return nullptr;
    if (node->type() == BaseNode::EntityType::kTopLevelChannel) {
      return node->WeakRefAsSubclass<ChannelNode>();
    }
    if (node->type() == BaseNode::EntityType::kInternalChannel) {
      return node->WeakRefAsSubclass<ChannelNode>();
    }
    return nullptr;
  }

  static WeakRefCountedPtr<ServerNode> GetServer(intptr_t uuid) {
    return Default()
        ->InternalGetTyped<ServerNode, BaseNode::EntityType::kServer>(uuid);
  }

  static WeakRefCountedPtr<SocketNode> GetSocket(intptr_t uuid) {
    return Default()
        ->InternalGetTyped<SocketNode, BaseNode::EntityType::kSocket>(uuid);
  }

  // Returns the allocated JSON string that represents the proto
  // GetTopChannelsResponse as per channelz.proto.
  static auto GetTopChannels(intptr_t start_channel_id) {
    return Default()
        ->InternalGetObjects<ChannelNode,
                             BaseNode::EntityType::kTopLevelChannel>(
            start_channel_id);
  }

  static auto GetTopSockets(intptr_t start_socket_id) {
    return Default()
        ->InternalGetObjects<SocketNode, BaseNode::EntityType::kSocket>(
            start_socket_id);
  }

  static std::string GetTopChannelsJson(intptr_t start_channel_id);
  static std::string GetServersJson(intptr_t start_server_id);

  // Returns the allocated JSON string that represents the proto
  // GetServersResponse as per channelz.proto.
  static auto GetServers(intptr_t start_server_id) {
    return Default()
        ->InternalGetObjects<ServerNode, BaseNode::EntityType::kServer>(
            start_server_id);
  }

  static std::tuple<std::vector<WeakRefCountedPtr<BaseNode>>, bool>
  GetChildrenOfType(intptr_t start_node, const BaseNode* parent,
                    BaseNode::EntityType type, size_t max_results) {
    return Default()->InternalGetChildrenOfType(start_node, parent, type,
                                                max_results);
  }

  static WeakRefCountedPtr<BaseNode> GetNode(intptr_t uuid) {
    return Default()->InternalGet(uuid);
  }

  static std::tuple<std::vector<WeakRefCountedPtr<BaseNode>>, bool>
  GetNodesOfType(intptr_t start_node, BaseNode::EntityType type,
                 size_t max_results) {
    return Default()->InternalGetNodesOfType(start_node, type, max_results);
  }

  // Test only helper function to dump the JSON representation to std out.
  // This can aid in debugging channelz code.
  static void LogAllEntities() { Default()->InternalLogAllEntities(); }

  static std::vector<WeakRefCountedPtr<BaseNode>> GetAllEntities() {
    return Default()->InternalGetAllEntities();
  }

  // Test only helper function to reset to initial state.
  static void TestOnlyReset();

 private:
  ChannelzRegistry() { LoadConfig(); }

  void LoadConfig();

  // Takes a callable F: (WeakRefCountedPtr<BaseNode>) -> bool, and returns
  // a (BaseNode*) -> bool that filters unreffed objects and returns true.
  // The ref must be unreffed outside the NodeMapInterface iteration.
  template <typename F>
  static auto CollectReferences(F fn) {
    return [fn = std::move(fn)](BaseNode* n) {
      auto node = n->RefIfNonZero();
      if (node == nullptr) return true;
      return fn(std::move(node));
    };
  }

  struct NodeList {
    BaseNode* head = nullptr;
    BaseNode* tail = nullptr;
    size_t count = 0;
    bool Holds(BaseNode* node) const;
    void AddToHead(BaseNode* node);
    void Remove(BaseNode* node);
  };
  // Nodes traverse through up to four lists, depending on
  // whether they have a uuid (this is becoming numbered),
  // and whether they have been orphaned or not.
  // The lists help us find un-numbered nodes when needed for
  // queries, and the oldest orphaned node when needed for
  // garbage collection.
  // Nodes are organized into shards based on their pointer
  // address. A shard tracks the four lists of nodes
  // independently - we strive to have no cross-talk between
  // shards as these are very global objects.
  struct alignas(GPR_CACHELINE_SIZE) NodeShard {
    Mutex mu;
    // Nursery nodes have no uuid and are not orphaned.
    NodeList nursery ABSL_GUARDED_BY(mu);
    // Numbered nodes have been assigned a uuid, and are not orphaned.
    NodeList numbered ABSL_GUARDED_BY(mu);
    // Orphaned nodes have no uuid, but have been orphaned.
    NodeList orphaned ABSL_GUARDED_BY(mu);
    // Finally, orphaned numbered nodes are orphaned, and have been assigned a
    // uuid.
    NodeList orphaned_numbered ABSL_GUARDED_BY(mu);
    uint64_t next_orphan_index ABSL_GUARDED_BY(mu) = 1;
    size_t TotalOrphaned() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu) {
      return orphaned.count + orphaned_numbered.count;
    }
  };

  // Returned the singleton instance of ChannelzRegistry;
  static ChannelzRegistry* Default();

  // globally registers an Entry. Returns its unique uuid
  void InternalRegister(BaseNode* node);

  // globally unregisters the object that is associated to uuid. Also does
  // sanity check that an object doesn't try to unregister the wrong type.
  void InternalUnregister(BaseNode* node);

  intptr_t InternalNumberNode(BaseNode* node);

  // if object with uuid has previously been registered as the correct type,
  // returns the void* associated with that uuid. Else returns nullptr.
  WeakRefCountedPtr<BaseNode> InternalGet(intptr_t uuid);

  // Generic query over nodes.
  // This function takes care of all the gnarly locking, and allows high level
  // code to request a start node and maximum number of results (for pagination
  // purposes).
  // `discriminator` allows callers to choose which nodes will be returned - if
  // it returns true, the node is included in the result.
  // `discriminator` *MUST NOT* ref the node, nor call into ChannelzRegistry via
  // any code path (locks are held during the call).
  std::tuple<std::vector<WeakRefCountedPtr<BaseNode>>, bool> QueryNodes(
      intptr_t start_node,
      absl::FunctionRef<bool(const BaseNode*)> discriminator,
      size_t max_results);

  std::tuple<std::vector<WeakRefCountedPtr<BaseNode>>, bool>
  InternalGetChildrenOfType(intptr_t start_node, const BaseNode* parent,
                            BaseNode::EntityType type, size_t max_results) {
    return QueryNodes(
        start_node,
        [type, parent](const BaseNode* n) {
          return n->type() == type && n->HasParent(parent);
        },
        max_results);
  }

  std::tuple<std::vector<WeakRefCountedPtr<BaseNode>>, bool>
  InternalGetNodesOfType(intptr_t start_node, BaseNode::EntityType type,
                         size_t max_results) {
    return QueryNodes(
        start_node, [type](const BaseNode* n) { return n->type() == type; },
        max_results);
  }

  template <typename T, BaseNode::EntityType entity_type>
  WeakRefCountedPtr<T> InternalGetTyped(intptr_t uuid) {
    WeakRefCountedPtr<BaseNode> node = InternalGet(uuid);
    if (node == nullptr || node->type() != entity_type) {
      return nullptr;
    }
    return node->WeakRefAsSubclass<T>();
  }

  template <typename T, BaseNode::EntityType entity_type>
  std::tuple<std::vector<WeakRefCountedPtr<T>>, bool> InternalGetObjects(
      intptr_t start_id) {
    const int kPaginationLimit = 100;
    std::vector<WeakRefCountedPtr<T>> top_level_channels;
    const auto [nodes, end] = QueryNodes(
        start_id,
        [](const BaseNode* node) { return node->type() == entity_type; },
        kPaginationLimit);
    for (const auto& p : nodes) {
      top_level_channels.emplace_back(p->template WeakRefAsSubclass<T>());
    }
    return std::tuple(std::move(top_level_channels), end);
  }

  void InternalLogAllEntities();
  std::vector<WeakRefCountedPtr<BaseNode>> InternalGetAllEntities();

  static constexpr size_t kNodeShards = 63;
  size_t NodeShardIndex(BaseNode* node) {
    return absl::HashOf(node) % kNodeShards;
  }

  int64_t uuid_generator_{1};
  std::vector<NodeShard> node_shards_{kNodeShards};
  Mutex index_mu_;
  absl::btree_map<intptr_t, BaseNode*> index_ ABSL_GUARDED_BY(index_mu_);
  size_t max_orphaned_per_shard_;
};

// `additionalInfo` section is not yet in the protobuf format, so we
// provide a utility to strip it for compatibility.
std::string StripAdditionalInfoFromJson(absl::string_view json);

}  // namespace channelz
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CHANNELZ_CHANNELZ_REGISTRY_H
