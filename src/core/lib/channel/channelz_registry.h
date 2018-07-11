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

// singleton registry object to track all objects that are needed to support
// channelz bookkeeping. All objects share globally distributed uuids.
class ChannelzRegistry {
 public:
  // To be called in grpc_init()
  static void Init();

  // To be callen in grpc_shutdown();
  static void Shutdown();

  // globally registers a channelz Object. Returns its unique uuid
  template <typename Object>
  static intptr_t Register(Object* object) {
    return Default()->InternalRegister(object);
  }

  // globally unregisters the object that is associated to uuid.
  static void Unregister(intptr_t uuid) { Default()->InternalUnregister(uuid); }

  // if object with uuid has previously been registered, returns the
  // Object associated with that uuid. Else returns nullptr.
  template <typename Object>
  static Object* Get(intptr_t uuid) {
    return Default()->InternalGet<Object>(uuid);
  }

 private:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE

  ChannelzRegistry();
  ~ChannelzRegistry();

  // Returned the singleton instance of ChannelzRegistry;
  static ChannelzRegistry* Default();

  // globally registers a channelz Object. Returns its unique uuid
  template <typename Object>
  intptr_t InternalRegister(Object* object) {
    gpr_mu_lock(&mu_);
    entities_.push_back(static_cast<void*>(object));
    intptr_t uuid = entities_.size();
    gpr_mu_unlock(&mu_);
    return uuid;
  }

  // globally unregisters the object that is associated to uuid.
  void InternalUnregister(intptr_t uuid);

  // if object with uuid has previously been registered, returns the
  // Object associated with that uuid. Else returns nullptr.
  template <typename Object>
  Object* InternalGet(intptr_t uuid) {
    gpr_mu_lock(&mu_);
    if (uuid < 1 || uuid > static_cast<intptr_t>(entities_.size())) {
      gpr_mu_unlock(&mu_);
      return nullptr;
    }
    Object* ret = static_cast<Object*>(entities_[uuid - 1]);
    gpr_mu_unlock(&mu_);
    return ret;
  }

  // private members

  // protects entities_ and uuid_
  gpr_mu mu_;
  InlinedVector<void*, 20> entities_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNELZ_REGISTRY_H */
