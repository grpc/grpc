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
  bool need_to_unref_constructed = false;
  while (c == nullptr) {
    need_to_unref_constructed = false;
    // Compare and swap loop:
    // - take a reference to the current index
    gpr_mu_lock(&mu_);
    grpc_avl index = grpc_avl_ref(subchannel_map_, nullptr);
    gpr_mu_unlock(&mu_);
    // - Check to see if a subchannel already exists
    c = static_cast<grpc_subchannel*>(grpc_avl_get(index, key, nullptr));
    if (c != nullptr) {
      c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(c, "index_register");
    }
    if (c != nullptr) {
      // yes -> we're done
      need_to_unref_constructed = true;
    } else {
      // no -> update the avl and compare/swap
      grpc_avl updated = grpc_avl_add(
          grpc_avl_ref(index, nullptr), grpc_core::New<SubchannelKey>(*key),
          GRPC_SUBCHANNEL_WEAK_REF(constructed, "index_register"), nullptr);
      // it may happen (but it's expected to be unlikely)
      // that some other thread has changed the index:
      // compare/swap here to check that, and retry as necessary
      gpr_mu_lock(&mu_);
      if (index.root == subchannel_map_.root) {
        GPR_SWAP(grpc_avl, updated, subchannel_map_);
        c = constructed;
      }
      gpr_mu_unlock(&mu_);
      grpc_avl_unref(updated, nullptr);
    }
    grpc_avl_unref(index, nullptr);
  }
  if (need_to_unref_constructed) {
    GRPC_SUBCHANNEL_UNREF(constructed, "index_register");
  }
  return c;
}

void GlobalSubchannelPool::UnregisterSubchannel(SubchannelKey* key,
                                                grpc_subchannel* constructed) {
  bool done = false;
  while (!done) {
    // Compare and swap loop:
    // - take a reference to the current index
    gpr_mu_lock(&mu_);
    grpc_avl index = grpc_avl_ref(subchannel_map_, nullptr);
    gpr_mu_unlock(&mu_);
    // Check to see if this key still refers to the previously
    // registered subchannel
    grpc_subchannel* c =
        static_cast<grpc_subchannel*>(grpc_avl_get(index, key, nullptr));
    if (c != constructed) {
      grpc_avl_unref(index, nullptr);
      break;
    }
    // compare and swap the update (some other thread may have
    // mutated the index behind us)
    grpc_avl updated =
        grpc_avl_remove(grpc_avl_ref(index, nullptr), key, nullptr);
    gpr_mu_lock(&mu_);
    if (index.root == subchannel_map_.root) {
      GPR_SWAP(grpc_avl, updated, subchannel_map_);
      done = true;
    }
    gpr_mu_unlock(&mu_);
    grpc_avl_unref(updated, nullptr);
    grpc_avl_unref(index, nullptr);
  }
}

grpc_subchannel* GlobalSubchannelPool::FindSubchannel(SubchannelKey* key) {
  // Lock, and take a reference to the subchannel map.
  // We don't need to do the search under a lock as avl's are immutable.
  gpr_mu_lock(&mu_);
  grpc_avl index = grpc_avl_ref(subchannel_map_, nullptr);
  gpr_mu_unlock(&mu_);
  grpc_subchannel* c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(
      (grpc_subchannel*)grpc_avl_get(index, key, nullptr), "index_find");
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
