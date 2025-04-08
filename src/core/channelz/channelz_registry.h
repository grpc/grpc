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
  static void Unregister(intptr_t uuid) { Default()->InternalUnregister(uuid); }
  static RefCountedPtr<BaseNode> Get(intptr_t uuid) {
    return Default()->InternalGet(uuid);
  }

  static RefCountedPtr<SubchannelNode> GetSubchannel(intptr_t uuid) {
    return Default()
        ->InternalGetTyped<SubchannelNode, BaseNode::EntityType::kSubchannel>(
            uuid);
  }

  static RefCountedPtr<ChannelNode> GetChannel(intptr_t uuid) {
    auto node = Default()->InternalGet(uuid);
    if (node == nullptr) return nullptr;
    if (node->type() == BaseNode::EntityType::kTopLevelChannel) {
      return node->RefAsSubclass<ChannelNode>();
    }
    if (node->type() == BaseNode::EntityType::kInternalChannel) {
      return node->RefAsSubclass<ChannelNode>();
    }
    return nullptr;
  }

  static RefCountedPtr<ServerNode> GetServer(intptr_t uuid) {
    return Default()
        ->InternalGetTyped<ServerNode, BaseNode::EntityType::kServer>(uuid);
  }

  static RefCountedPtr<SocketNode> GetSocket(intptr_t uuid) {
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

  static std::string GetTopChannelsJson(intptr_t start_channel_id);
  static std::string GetServersJson(intptr_t start_server_id);

  // Returns the allocated JSON string that represents the proto
  // GetServersResponse as per channelz.proto.
  static auto GetServers(intptr_t start_server_id) {
    return Default()
        ->InternalGetObjects<ServerNode, BaseNode::EntityType::kServer>(
            start_server_id);
  }

  // Test only helper function to dump the JSON representation to std out.
  // This can aid in debugging channelz code.
  static void LogAllEntities() { Default()->InternalLogAllEntities(); }

  // Test only helper function to reset to initial state.
  static void TestOnlyReset() {
    auto* p = Default();
    MutexLock lock(&p->mu_);
    p->node_map_.clear();
    p->uuid_generator_ = 0;
  }

 private:
  // Returned the singleton instance of ChannelzRegistry;
  static ChannelzRegistry* Default();

  // globally registers an Entry. Returns its unique uuid
  void InternalRegister(BaseNode* node);

  // globally unregisters the object that is associated to uuid. Also does
  // sanity check that an object doesn't try to unregister the wrong type.
  void InternalUnregister(intptr_t uuid);

  // if object with uuid has previously been registered as the correct type,
  // returns the void* associated with that uuid. Else returns nullptr.
  RefCountedPtr<BaseNode> InternalGet(intptr_t uuid);

  template <typename T, BaseNode::EntityType entity_type>
  RefCountedPtr<T> InternalGetTyped(intptr_t uuid) {
    RefCountedPtr<BaseNode> node = InternalGet(uuid);
    if (node == nullptr || node->type() != entity_type) {
      return nullptr;
    }
    return node->RefAsSubclass<T>();
  }

  template <typename T, BaseNode::EntityType entity_type>
  std::tuple<std::vector<RefCountedPtr<T>>, bool> InternalGetObjects(
      intptr_t start_id) {
    const int kPaginationLimit = 100;
    std::vector<RefCountedPtr<T>> top_level_channels;
    RefCountedPtr<BaseNode> node_after_pagination_limit;
    {
      MutexLock lock(&mu_);
      for (auto it = node_map_.lower_bound(start_id); it != node_map_.end();
           ++it) {
        BaseNode* node = it->second;
        RefCountedPtr<BaseNode> node_ref;
        if (node->type() == entity_type &&
            (node_ref = node->RefIfNonZero()) != nullptr) {
          // Check if we are over pagination limit to determine if we need to
          // set the "end" element. If we don't go through this block, we know
          // that when the loop terminates, we have <= to kPaginationLimit.
          // Note that because we have already increased this node's
          // refcount, we need to decrease it, but we can't unref while
          // holding the lock, because this may lead to a deadlock.
          if (top_level_channels.size() == kPaginationLimit) {
            node_after_pagination_limit = std::move(node_ref);
            break;
          }
          top_level_channels.emplace_back(node_ref->RefAsSubclass<T>());
        }
      }
    }
    return std::tuple(std::move(top_level_channels),
                      node_after_pagination_limit == nullptr);
  }

  void InternalLogAllEntities();

  // protects members
  Mutex mu_;
  std::map<intptr_t, BaseNode*> node_map_ ABSL_GUARDED_BY(mu_);
  intptr_t uuid_generator_ ABSL_GUARDED_BY(mu_) = 0;
};

}  // namespace channelz
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CHANNELZ_CHANNELZ_REGISTRY_H
