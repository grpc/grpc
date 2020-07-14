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
  Subchannel* c = nullptr;
  // Compare and swap (CAS) loop:
  for (int attempt_count = 0; c == nullptr; attempt_count++) {
    // Ref the shared map to have a local copy.
    gpr_mu_lock(&mu_);
    grpc_avl old_map = grpc_avl_ref(subchannel_map_, nullptr);
    gpr_mu_unlock(&mu_);
    // Check to see if a subchannel already exists.
    c = static_cast<Subchannel*>(grpc_avl_get(old_map, key, nullptr));
    if (c != nullptr) {
      // The subchannel already exists. Try to reuse it.
      c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(c, "subchannel_register+reuse");
      if (c != nullptr) {
        GRPC_SUBCHANNEL_UNREF(constructed,
                              "subchannel_register+found_existing");
        // Exit the CAS loop without modifying the shared map.
      } else {
        // Reuse of the subchannel failed, so retry CAS loop
        if (attempt_count >=
            GRPC_REGISTER_SUBCHANNEL_CALM_DOWN_AFTER_ATTEMPTS) {
          // GRPC_SUBCHANNEL_REF_FROM_WEAK_REF returning nullptr means that the
          // subchannel we got is no longer valid and it's going to be removed
          // from the AVL tree soon. Spinning here excesively here can actually
          // prevent another thread from removing the subchannel, basically
          // resulting in a live lock. See b/157516542 for more details.
          // TODO(jtattermusch): the entire ref-counting mechanism for
          // subchannels should be overhaulded, but the current workaround
          // is fine for short-term.
          // TODO(jtattermusch): gpr does not support thread yield operation,
          // so a very short wait is the best we can do.
          gpr_sleep_until(gpr_time_add(
              gpr_now(GPR_CLOCK_REALTIME),
              gpr_time_from_micros(GRPC_REGISTER_SUBCHANNEL_CALM_DOWN_MICROS,
                                   GPR_TIMESPAN)));
        }
      }
    } else {
      // There hasn't been such subchannel. Add one.
      // Note that we should ref the old map first because grpc_avl_add() will
      // unref it while we still need to access it later.
      grpc_avl new_map = grpc_avl_add(
          grpc_avl_ref(old_map, nullptr), new SubchannelKey(*key),
          GRPC_SUBCHANNEL_WEAK_REF(constructed, "subchannel_register+new"),
          nullptr);
      // Try to publish the change to the shared map. It may happen (but
      // unlikely) that some other thread has changed the shared map, so compare
      // to make sure it's unchanged before swapping. Retry if it's changed.
      gpr_mu_lock(&mu_);
      if (old_map.root == subchannel_map_.root) {
        GPR_SWAP(grpc_avl, new_map, subchannel_map_);
        c = constructed;
      }
      gpr_mu_unlock(&mu_);
      grpc_avl_unref(new_map, nullptr);
    }
    grpc_avl_unref(old_map, nullptr);
  }
  return c;
}

void GlobalSubchannelPool::UnregisterSubchannel(SubchannelKey* key) {
  bool done = false;
  // Compare and swap (CAS) loop:
  while (!done) {
    // Ref the shared map to have a local copy.
    gpr_mu_lock(&mu_);
    grpc_avl old_map = grpc_avl_ref(subchannel_map_, nullptr);
    gpr_mu_unlock(&mu_);
    // Remove the subchannel.
    // Note that we should ref the old map first because grpc_avl_remove() will
    // unref it while we still need to access it later.
    grpc_avl new_map =
        grpc_avl_remove(grpc_avl_ref(old_map, nullptr), key, nullptr);
    // Try to publish the change to the shared map. It may happen (but
    // unlikely) that some other thread has changed the shared map, so compare
    // to make sure it's unchanged before swapping. Retry if it's changed.
    gpr_mu_lock(&mu_);
    if (old_map.root == subchannel_map_.root) {
      GPR_SWAP(grpc_avl, new_map, subchannel_map_);
      done = true;
    }
    gpr_mu_unlock(&mu_);
    grpc_avl_unref(new_map, nullptr);
    grpc_avl_unref(old_map, nullptr);
  }
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
