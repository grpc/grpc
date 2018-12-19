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

namespace grpc_core {

GlobalSubchannelPool::GlobalSubchannelPool() {
  subchannel_map_ = grpc_avl_create(&subchannel_avl_vtable_);
  gpr_mu_init(&mu_);
  gpr_ref_init(&refcount_, 1);
}

void GlobalSubchannelPool::Init() {
  instance_ = grpc_core::New<GlobalSubchannelPool>();
}

void GlobalSubchannelPool::Shutdown() {
  // TODO(juanlishen): This refcounting mechanism may lead to memory leackage.
  // To solve that, we should force polling to flush any pending callbacks, then
  // shutdown safely.
  GPR_ASSERT(instance_ != nullptr);
  instance_->Unref();
}

// TODO(juanlishen): Should this be thread-safe?
GlobalSubchannelPool& GlobalSubchannelPool::instance() {
  GPR_ASSERT(instance_ != nullptr);
  return *instance_;
}

grpc_subchannel* GlobalSubchannelPool::RegisterSubchannel(
    SubchannelKey* key, grpc_subchannel* constructed) {
  grpc_subchannel* c = nullptr;
  // Compare and swap (CAS) loop:
  while (c == nullptr) {
    // Ref the shared map to have a local copy.
    gpr_mu_lock(&mu_);
    grpc_avl old_map = grpc_avl_ref(subchannel_map_, nullptr);
    gpr_mu_unlock(&mu_);
    // Check to see if a subchannel already exists.
    c = static_cast<grpc_subchannel*>(grpc_avl_get(old_map, key, nullptr));
    if (c != nullptr) {
      // The subchannel already exists. Reuse it.
      c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(c, "subchannel_register+reuse");
      GRPC_SUBCHANNEL_UNREF(constructed, "subchannel_register+found_existing");
      // Exit the CAS loop without modifying the shared map.
    } else {
      // There hasn't been such subchannel. Add one.
      // Note that we should ref the old map first because grpc_avl_add() will
      // unref it while we still need to access it later.
      grpc_avl new_map = grpc_avl_add(
          grpc_avl_ref(old_map, nullptr), grpc_core::New<SubchannelKey>(*key),
          GRPC_SUBCHANNEL_WEAK_REF(constructed, "subchannel_register+new"),
          nullptr);
      // Try to publish the change to the shared map. It may happen (but
      // unlikely) that some other thread has changed the shared map, so compare
      // to make sure it's unchanged before swapping.
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

void GlobalSubchannelPool::UnregisterSubchannel(SubchannelKey* key,
                                                grpc_subchannel* constructed) {
  bool done = false;
  // Compare and swap (CAS) loop:
  while (!done) {
    // Ref the shared map to have a local copy.
    gpr_mu_lock(&mu_);
    grpc_avl old_map = grpc_avl_ref(subchannel_map_, nullptr);
    gpr_mu_unlock(&mu_);
    // Check to see if this key still refers to the previously registered
    // subchannel.
    grpc_subchannel* c =
        static_cast<grpc_subchannel*>(grpc_avl_get(old_map, key, nullptr));
    if (c != constructed) {
      grpc_avl_unref(old_map, nullptr);
      return;
    }
    // Remove the subchannel.
    // Note that we should ref the old map first because grpc_avl_remove() will
    // unref it while we still need to access it later.
    grpc_avl new_map =
        grpc_avl_remove(grpc_avl_ref(old_map, nullptr), key, nullptr);
    // Try to publish the change to the shared map. It may happen (but
    // unlikely) that some other thread has changed the shared map, so compare
    // to make sure it's unchanged before swapping.
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

grpc_subchannel* GlobalSubchannelPool::FindSubchannel(SubchannelKey* key) {
  // Lock, and take a reference to the subchannel map.
  // We don't need to do the search under a lock as avl's are immutable.
  gpr_mu_lock(&mu_);
  grpc_avl index = grpc_avl_ref(subchannel_map_, nullptr);
  gpr_mu_unlock(&mu_);
  grpc_subchannel* c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(
      (grpc_subchannel*)grpc_avl_get(index, key, nullptr), "found_from_pool");
  grpc_avl_unref(index, nullptr);
  return c;
}

GlobalSubchannelPool* GlobalSubchannelPool::Ref() {
  gpr_ref_non_zero(&refcount_);
  return this;
}

void GlobalSubchannelPool::Unref() {
  if (gpr_unref(&refcount_)) {
    gpr_mu_destroy(&mu_);
    grpc_avl_unref(subchannel_map_, nullptr);
    grpc_core::Delete(this);
  }
}

GlobalSubchannelPool* GlobalSubchannelPool::instance_ = nullptr;

}  // namespace grpc_core
