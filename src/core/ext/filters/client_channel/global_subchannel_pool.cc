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

std::unique_ptr<SubchannelRef> GlobalSubchannelPool::RegisterSubchannel(const SubchannelKey &key,
                                                     Subchannel* constructed) {
  MutexLock lock(&mu_);
  auto p = subchannel_map_.find(key);
  if (p != subchannel_map_.end()) {
    // The subchannel already exists. Try to reuse it.
    Subchannel* c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(p->second, "subchannel_register+reuse");
    if (c != nullptr) {
      GRPC_SUBCHANNEL_UNREF(constructed, "subchannel_register+found_existing");
      return absl::make_unique<GlobalSubchannelPoolSubchannelRef>(Ref(), c, key);
    }
  }
  gpr_log(GPR_DEBUG, "Add new subchannel to pool, target address: %s", constructed->GetTargetAddress());
  subchannel_map_[key] = GRPC_SUBCHANNEL_WEAK_REF(constructed, "subchannel_register+new");
  return absl::make_unique<GlobalSubchannelPoolSubchannelRef>(Ref(), constructed, key);
}

RefCountedPtr<GlobalSubchannelPool>* GlobalSubchannelPool::instance_ = nullptr;

long GlobalSubchannelPool::TestOnlyGlobalSubchannelPoolSize() {
  GlobalSubchannelPool* g = GlobalSubchannelPool::instance_->get();
  MutexLock lock(&g->mu_);
  return g->subchannel_map_.size();
}

GlobalSubchannelPool::GlobalSubchannelPoolSubchannelRef::GlobalSubchannelPoolSubchannelRef(
    RefCountedPtr<GlobalSubchannelPool> parent, Subchannel* subchannel, const SubchannelKey &key)
  : parent_(std::move(parent)), subchannel_(subchannel), key_(key) {
}

GlobalSubchannelPool::GlobalSubchannelPoolSubchannelRef::~GlobalSubchannelPoolSubchannelRef() {
  // TODO: remove inner brackets after making subchannel DualRefCounted
  MutexLock lock(&parent_->mu_);
  GRPC_SUBCHANNEL_UNREF(subchannel_, "GlobalSubchannelPoolSubchannelRef+destroyed");
  // The subchannel already exists. Try to reuse it.
  if (GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(subchannel_, "GlobalSubchannelPoolSubchannelRef+check_for_remaining_refs") != nullptr) {
    // there are still other SubchannelRef's using this subchannel
    GRPC_SUBCHANNEL_UNREF(subchannel_, "GlobalSubchannelPoolSubchannelRef+check_for_remaining_refs");
    return;
  }
  GPR_ASSERT(parent_->subchannel_map_.erase(key_) == 1);
  GRPC_SUBCHANNEL_WEAK_UNREF(subchannel_, "GlobalSubchannelPoolSubchannelRef+destroyed");
}

}  // namespace grpc_core
