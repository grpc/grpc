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

#ifndef GRPC_CORE_LIB_CHANNEL_CHANNELZ_REGISTRY_H
#define GRPC_CORE_LIB_CHANNEL_CHANNELZ_REGISTRY_H

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/timer.h"

#include <stdint.h>

namespace grpc_core {
namespace channelz {

namespace testing {
class ChannelzRegistryPeer;
}

// singleton registry object to track all objects that are needed to support
// channelz bookkeeping. All objects share globally distributed uuids.
class ChannelzRegistry {
 public:
  // To be called in grpc_init()
  static void Init();

  // To be called in grpc_shutdown();
  static void Shutdown();

  template <typename NodeType, typename... Args>
  static RefCountedPtr<NodeType> CreateNode(Args&&... args) {
    return (*registry_)->CreateNode<NodeType>(std::forward<Args>(args)...);
  }

  static RefCountedPtr<BaseNode> Get(intptr_t uuid) {
    return (*registry_)->Get(uuid);
  }

  // Returns the allocated JSON string that represents the proto
  // GetTopChannelsResponse as per channelz.proto.
  static char* GetTopChannels(intptr_t start_channel_id) {
    return (*registry_)->GetTopChannels(start_channel_id);
  }

  // Returns the allocated JSON string that represents the proto
  // GetServersResponse as per channelz.proto.
  static char* GetServers(intptr_t start_server_id) {
    return (*registry_)->GetServers(start_server_id);
  }

  // Test only helper function to dump the JSON representation to std out.
  // This can aid in debugging channelz code.
  static void LogAllEntities() { (*registry_)->LogAllEntities(); }

 private:
  friend class testing::ChannelzRegistryPeer;

  class Registry : public InternallyRefCounted<Registry> {
   public:
    Registry();

    void Orphan() override;

    template <typename NodeType, typename... Args>
    RefCountedPtr<NodeType> CreateNode(Args&&... args) {
      MutexLock lock(&mu_);
      const intptr_t uuid = ++uuid_generator_;
      auto node = MakeRefCounted<NodeType>(uuid, std::forward<Args>(args)...);
      entities_.push_back(node);
      return node;
    }

    // Returns a ref to the node for uuid, or nullptr if there is no
    // node for uuid.
    RefCountedPtr<BaseNode> Get(intptr_t uuid);

    char* GetTopChannels(intptr_t start_channel_id);
    char* GetServers(intptr_t start_server_id);

    void LogAllEntities();

   private:
    friend class testing::ChannelzRegistryPeer;

    static void OnGarbageCollectionTimer(void* arg, grpc_error* error);

    void DoGarbageCollectionLocked();
    void MaybeRemoveUnusedNodeLocked(size_t idx);

    // If entities_ has over a certain threshold of empty slots, it will
    // compact the vector and move all used slots to the front.
    void MaybePerformCompactionLocked();

    // Performs binary search on entities_ to find the index with that uuid.
    // If direct_hit_needed, then will return -1 in case of absence.
    // Else, will return idx of the first uuid higher than the target.
    int FindByUuidLocked(intptr_t uuid, bool direct_hit_needed);

    // protects members
    Mutex mu_;
    // TODO(roth): Once the erase() bug has been fixed in our Map<>
    // implementation, change this to use a map so that we can eliminate
    // the need for compaction.
    InlinedVector<RefCountedPtr<BaseNode>, 20> entities_;
    int num_empty_slots_ = 0;
    intptr_t uuid_generator_ = 0;
    bool shutdown_ = false;
    grpc_timer gc_timer_;
    grpc_closure gc_closure_;
  };

  // The singleton Registry instance.
  static OrphanablePtr<Registry>* registry_;
};

}  // namespace channelz
}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNELZ_REGISTRY_H */
