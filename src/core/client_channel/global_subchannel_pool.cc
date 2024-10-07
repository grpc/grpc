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

#include "src/core/client_channel/global_subchannel_pool.h"

#include <grpc/support/port_platform.h>

#include <utility>

#include "src/core/client_channel/subchannel.h"

namespace grpc_core {

RefCountedPtr<GlobalSubchannelPool> GlobalSubchannelPool::instance() {
  static GlobalSubchannelPool* p = new GlobalSubchannelPool();
  return p->RefAsSubclass<GlobalSubchannelPool>();
}

RefCountedPtr<Subchannel> GlobalSubchannelPool::RegisterSubchannel(
    const SubchannelKey& key, RefCountedPtr<Subchannel> constructed) {
  MutexLock lock(&mu_);
  auto it = subchannel_map_.find(key);
  if (it != subchannel_map_.end()) {
    RefCountedPtr<Subchannel> existing = it->second->RefIfNonZero();
    if (existing != nullptr) return existing;
  }
  subchannel_map_[key] = constructed.get();
  return constructed;
}

void GlobalSubchannelPool::UnregisterSubchannel(const SubchannelKey& key,
                                                Subchannel* subchannel) {
  MutexLock lock(&mu_);
  auto it = subchannel_map_.find(key);
  // delete only if key hasn't been re-registered to a different subchannel
  // between strong-unreffing and unregistration of subchannel.
  if (it != subchannel_map_.end() && it->second == subchannel) {
    subchannel_map_.erase(it);
  }
}

RefCountedPtr<Subchannel> GlobalSubchannelPool::FindSubchannel(
    const SubchannelKey& key) {
  MutexLock lock(&mu_);
  auto it = subchannel_map_.find(key);
  if (it == subchannel_map_.end()) return nullptr;
  return it->second->RefIfNonZero();
}

}  // namespace grpc_core
