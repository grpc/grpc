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

#include "src/core/ext/filters/client_channel/local_subchannel_pool.h"

#include "src/core/ext/filters/client_channel/subchannel.h"

namespace grpc_core {

std::unique_ptr<SubchannelRef> LocalSubchannelPool::RegisterSubchannel(
    const SubchannelKey &key, RefCountedPtr<Subchannel> constructed) {
  // Check to see if a subchannel already exists.
  auto p = subchannel_map_.find(key);
  RefCountedPtr<Subchannel> c;
  if (p != subchannel_map_.end()) {
    // The subchannel already exists. Reuse it.
    c = p->second->Ref();
  } else {
    // There hasn't been such subchannel. Add one.
    c = std::move(constructed);
    subchannel_map_[key] = c->WeakRef();
  }
  return absl::make_unique<LocalSubchannelPoolSubchannelRef>(Ref(), std::move(c), key);
}

LocalSubchannelPool::LocalSubchannelPoolSubchannelRef::LocalSubchannelPoolSubchannelRef(
    RefCountedPtr<LocalSubchannelPool> parent, RefCountedPtr<Subchannel> subchannel, const SubchannelKey &key)
    : parent_(std::move(parent)), subchannel_(subchannel), key_(key) {}

LocalSubchannelPool::LocalSubchannelPoolSubchannelRef::~LocalSubchannelPoolSubchannelRef() {
  Subchannel* c = subchannel_.get(); // release strong ref, pool still holds a weak ref
  subchannel_.reset();
  if (c->RefIfNonZero() == nullptr) {
    // nobody else using this subchannel, delete it from the pool
    GPR_ASSERT(parent_->subchannel_map_.erase(key_) == 1);
  }
}

}  // namespace grpc_core
