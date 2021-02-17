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

std::unique_ptr<SubchannelRef> GlobalSubchannelPool::RegisterSubchannel(SubchannelKey* key,
                                                     Subchannel* constructed) {
  gpr_mu_lock(&mu_);
  Subchannel* c = static_cast<Subchannel*>(grpc_avl_get(subchannel_map_, key, nullptr));
  if (c != nullptr) {
    // The subchannel already exists. Try to reuse it.
    c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(c, "subchannel_register+reuse");
    if (c != nullptr) {
      gpr_mu_unlock(&mu_);
      return absl::make_unique<GlobalSubchannelPoolSubchannelRef>(Ref(), c);
    }
  }
  gpr_log(GPR_DEBUG, "Add new subchannel to pool, target address: %s", constructed->GetTargetAddress());
  subchannel_map_ = grpc_avl_add(
      subchannel_map_, new SubchannelKey(*key),
      GRPC_SUBCHANNEL_WEAK_REF(constructed, "subchannel_register+new"),
      nullptr);
  gpr_mu_unlock(&mu_);
  return absl::make_unique<GlobalSubchannelPoolSubchannelRef>(Ref(), constructed);
}

RefCountedPtr<GlobalSubchannelPool>* GlobalSubchannelPool::instance_ = nullptr;

long GlobalSubchannelPool::TestOnlyGlobalSubchannelPoolSize() {
  GlobalSubchannelPool* g = GlobalSubchannelPool::instance_->get();
  gpr_mu_lock(&g->mu_);
  long ret = grpc_avl_calculate_height(g->subchannel_map_.root);
  gpr_mu_unlock(&g->mu_);
  return ret;
}

GlobalSubchannelPool::GlobalSubchannelPoolSubchannelRef::GlobalSubchannelPoolSubchannelRef(
    RefCountedPtr<GlobalSubchannelPool> parent, Subchannel* subchannel) : parent_(std::move(parent)), subchannel_(subchannel) {
}

GlobalSubchannelPool::GlobalSubchannelPoolSubchannelRef::~GlobalSubchannelPoolSubchannelRef() {
  gpr_mu_lock(&parent_->mu_);
  SubchannelKey* key = subchannel_->key();
  GRPC_SUBCHANNEL_UNREF(subchannel_, "GlobalSubchannelPoolSubchannelRef+destroyed");
  // The subchannel already exists. Try to reuse it.
  subchannel_ = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(subchannel_, "GlobalSubchannelPoolSubchannelRef+check_for_remaining_refs");
  if (subchannel_ != nullptr) {
    // there are still other SubchannelRef's using this subchannel
    GRPC_SUBCHANNEL_UNREF(subchannel_, "GlobalSubchannelPoolSubchannelRef+check_for_remaining_refs");
    gpr_mu_unlock(&parent_->mu_);
    return;
  }
  // TODO: is this safe to access? make key_ a field of SubchannelRef instead?
  parent_->subchannel_map_ =
        grpc_avl_remove(parent_->subchannel_map_, key, nullptr);
  gpr_mu_unlock(&parent_->mu_);
}

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
