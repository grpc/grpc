//
//
// Copyright 2018 gRPC authors.
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

#include "src/core/client_channel/local_subchannel_pool.h"

#include <utility>

#include <grpc/support/log.h>

#include "src/core/client_channel/subchannel.h"

namespace grpc_core {

RefCountedPtr<Subchannel> LocalSubchannelPool::RegisterSubchannel(
    const SubchannelKey& key, RefCountedPtr<Subchannel> constructed) {
  auto it = subchannel_map_.find(key);
  // Because this pool is only accessed under the client channel's work
  // serializer, and because FindSubchannel is checked before invoking
  // RegisterSubchannel, no such subchannel should exist in the map.
  GPR_ASSERT(it == subchannel_map_.end());
  subchannel_map_[key] = constructed.get();
  return constructed;
}

void LocalSubchannelPool::UnregisterSubchannel(const SubchannelKey& key,
                                               Subchannel* subchannel) {
  auto it = subchannel_map_.find(key);
  // Because this subchannel pool is accessed only under the client
  // channel's work serializer, any subchannel created by RegisterSubchannel
  // will be deleted from the map in UnregisterSubchannel.
  GPR_ASSERT(it != subchannel_map_.end());
  GPR_ASSERT(it->second == subchannel);
  subchannel_map_.erase(it);
}

RefCountedPtr<Subchannel> LocalSubchannelPool::FindSubchannel(
    const SubchannelKey& key) {
  auto it = subchannel_map_.find(key);
  if (it == subchannel_map_.end()) return nullptr;
  return it->second->Ref();
}

}  // namespace grpc_core
