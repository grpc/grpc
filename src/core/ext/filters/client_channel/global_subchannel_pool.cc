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

#include "src/core/ext/filters/client_channel/global_subchannel_pool.h"

#include "src/core/ext/filters/client_channel/subchannel.h"

namespace grpc_core {

#define GRPC_REGISTER_SUBCHANNEL_CALM_DOWN_AFTER_ATTEMPTS 100
#define GRPC_REGISTER_SUBCHANNEL_CALM_DOWN_MICROS 10

void GlobalSubchannelPool::Init() {
  instance_ = new RefCountedPtr<GlobalSubchannelPool>(
      MakeRefCounted<GlobalSubchannelPool>());
}

void GlobalSubchannelPool::Shutdown() {
  // To ensure Init() was called before.
  GPR_ASSERT(instance_ != nullptr);
  // To ensure Shutdown() was not called before.
  GPR_ASSERT(*instance_ != nullptr);
  instance_->reset();
  delete instance_;
}

RefCountedPtr<GlobalSubchannelPool> GlobalSubchannelPool::instance() {
  GPR_ASSERT(instance_ != nullptr);
  GPR_ASSERT(*instance_ != nullptr);
  return *instance_;
}

std::unique_ptr<SubchannelRef> GlobalSubchannelPool::RegisterSubchannel(
    const SubchannelKey& key, RefCountedPtr<Subchannel> constructed) {
  MutexLock lock(&mu_);
  auto p = subchannel_map_.find(key);
  if (p != subchannel_map_.end()) {
    // The subchannel already exists. Try to reuse it.
    RefCountedPtr<Subchannel> c = p->second->RefIfNonZero();
    if (c != nullptr) {
      return absl::make_unique<GlobalSubchannelPoolSubchannelRef>(
          Ref(), std::move(c), key);
    }
  }
  gpr_log(GPR_DEBUG, "Add new subchannel to pool, target address: %s",
          constructed->GetTargetAddress());
  subchannel_map_[key] = constructed->WeakRef();
  return absl::make_unique<GlobalSubchannelPoolSubchannelRef>(
      Ref(), std::move(constructed), key);
}

RefCountedPtr<GlobalSubchannelPool>* GlobalSubchannelPool::instance_ = nullptr;

GlobalSubchannelPool::GlobalSubchannelPoolSubchannelRef::
    GlobalSubchannelPoolSubchannelRef(
        RefCountedPtr<GlobalSubchannelPool> parent,
        RefCountedPtr<Subchannel> subchannel, const SubchannelKey& key)
    : parent_(std::move(parent)), subchannel_(subchannel), key_(key) {}

GlobalSubchannelPool::GlobalSubchannelPoolSubchannelRef::
    ~GlobalSubchannelPoolSubchannelRef() {
  MutexLock lock(&parent_->mu_);
  Subchannel* c = subchannel_.get();
  subchannel_.reset();  // release strong ref, pool still holds a weak ref
  if (c->RefIfNonZero() == nullptr) {
    // nobody else using this subchannel, delete it from the pool
    GPR_ASSERT(parent_->subchannel_map_.erase(key_) == 1);
  }
}

}  // namespace grpc_core
