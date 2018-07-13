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
#include "src/core/lib/gprpp/inlined_vector.h"

#include <stdint.h>

namespace grpc_core {
namespace channelz {

// singleton registry object to track all objects that are needed to support
// channelz bookkeeping. All objects share globally distributed uuids.
class ChannelzRegistry {
 public:
  // To be called in grpc_init()
  static void Init();

  // To be callen in grpc_shutdown();
  static void Shutdown();

  static intptr_t RegisterChannelNode(ChannelNode* channel_node) {
    RegistryEntry entry(channel_node, EntityType::kChannelNode);
    return Default()->InternalRegisterEntry(entry);
  }
  static void UnregisterChannelNode(intptr_t uuid) {
    Default()->InternalUnregisterEntry(uuid, EntityType::kChannelNode);
  }
  static ChannelNode* GetChannelNode(intptr_t uuid) {
    void* gotten = Default()->InternalGetEntry(uuid, EntityType::kChannelNode);
    return gotten == nullptr ? nullptr : static_cast<ChannelNode*>(gotten);
  }

  // todo, protect me
  static char* GetTopChannels(intptr_t start_channel_id) {
    return Default()->InternalGetTopChannels(start_channel_id);
  }

 private:
  enum class EntityType {
    kChannelNode,
    kUnset,
  };

  struct RegistryEntry {
    RegistryEntry(void* object_in, EntityType type_in)
        : object(object_in), type(type_in) {}
    void* object;
    EntityType type;
  };

  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE

  ChannelzRegistry();
  ~ChannelzRegistry();

  // Returned the singleton instance of ChannelzRegistry;
  static ChannelzRegistry* Default();

  // globally registers an Entry. Returns its unique uuid
  intptr_t InternalRegisterEntry(const RegistryEntry& entry);

  // globally unregisters the object that is associated to uuid. Also does
  // sanity check that an object doesn't try to unregister the wrong type.
  void InternalUnregisterEntry(intptr_t uuid, EntityType type);

  // if object with uuid has previously been registered as the correct type,
  // returns the void* associated with that uuid. Else returns nullptr.
  void* InternalGetEntry(intptr_t uuid, EntityType type);

  char* InternalGetTopChannels(intptr_t start_channel_id);

  // protects entities_ and uuid_
  gpr_mu mu_;
  InlinedVector<RegistryEntry, 20> entities_;
};

}  // namespace channelz
}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNELZ_REGISTRY_H */
