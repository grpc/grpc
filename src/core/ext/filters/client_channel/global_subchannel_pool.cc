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

GlobalSubchannelPool::GlobalSubchannelPool() {
  subchannel_map_ = grpc_avl_create(&subchannel_avl_vtable_);
  gpr_mu_init(&mu_);
}

GlobalSubchannelPool::~GlobalSubchannelPool() {
  gpr_mu_destroy(&mu_);
  grpc_avl_unref(subchannel_map_, nullptr);
}

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

Subchannel* GlobalSubchannelPool::RegisterSubchannel(SubchannelKey* key,
                                                     Subchannel* constructed) {
  grpc_core::MutexLockForGprMu lock(&mu_);
  Subchannel* c = static_cast<Subchannel*>(grpc_avl_get(subchannel_map_, key, nullptr));
  if (c != nullptr) {
    c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(c, "subchannel_register+reuse");
    if (c != nullptr) {
      GRPC_SUBCHANNEL_UNREF(constructed, "subchannel_register+found_existing");
      return c;
    }
    subchannel_map_ = grpc_avl_remove(subchannel_map_, key, nullptr);
  }
  subchannel_map_ = grpc_avl_add(
      subchannel_map_,
      new SubchannelKey(*key),
      GRPC_SUBCHANNEL_WEAK_REF(constructed, "subchannel_register+new"), nullptr);
  return constructed;
}

void GlobalSubchannelPool::UnregisterSubchannel(SubchannelKey* key) {
  grpc_core::MutexLockForGprMu lock(&mu_);
  Subchannel* c = static_cast<Subchannel*>(grpc_avl_get(subchannel_map_, key, nullptr));
  if (c == nullptr) {
    return;
  }
  c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(c, "subchannel_register+reuse");
  if (c != nullptr) {
    // A different subchannel with the same key has been added to the pool
    // after our original subchannel was strong-unreffed. Let it remain in the
    // pool.
    GRPC_SUBCHANNEL_UNREF(c, "subchannel_unregister+key_became_active");
    return;
  }
  subchannel_map_ = grpc_avl_remove(subchannel_map_, key, nullptr);
}

Subchannel* GlobalSubchannelPool::FindSubchannel(SubchannelKey* key) {
  // Lock, and take a reference to the subchannel map.
  // We don't need to do the search under a lock as AVL's are immutable.
  gpr_mu_lock(&mu_);
  grpc_avl index = grpc_avl_ref(subchannel_map_, nullptr);
  gpr_mu_unlock(&mu_);
  Subchannel* c = static_cast<Subchannel*>(grpc_avl_get(index, key, nullptr));
  if (c != nullptr) c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(c, "found_from_pool");
  grpc_avl_unref(index, nullptr);
  return c;
}

RefCountedPtr<GlobalSubchannelPool>* GlobalSubchannelPool::instance_ = nullptr;

namespace {

void sck_avl_destroy(void* p, void* /*user_data*/) {
  SubchannelKey* key = static_cast<SubchannelKey*>(p);
  delete key;
}

void* sck_avl_copy(void* p, void* /*unused*/) {
  const SubchannelKey* key = static_cast<const SubchannelKey*>(p);
  auto* new_key = new SubchannelKey(*key);
  return static_cast<void*>(new_key);
}

long sck_avl_compare(void* a, void* b, void* /*unused*/) {
  const SubchannelKey* key_a = static_cast<const SubchannelKey*>(a);
  const SubchannelKey* key_b = static_cast<const SubchannelKey*>(b);
  return key_a->Cmp(*key_b);
}

void scv_avl_destroy(void* p, void* /*user_data*/) {
  GRPC_SUBCHANNEL_WEAK_UNREF((Subchannel*)p, "global_subchannel_pool");
}

void* scv_avl_copy(void* p, void* /*unused*/) {
  GRPC_SUBCHANNEL_WEAK_REF((Subchannel*)p, "global_subchannel_pool");
  return p;
}

}  // namespace

const grpc_avl_vtable GlobalSubchannelPool::subchannel_avl_vtable_ = {
    sck_avl_destroy,  // destroy_key
    sck_avl_copy,     // copy_key
    sck_avl_compare,  // compare_keys
    scv_avl_destroy,  // destroy_value
    scv_avl_copy      // copy_value
};

}  // namespace grpc_core
