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

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/mutex_lock.h"

constexpr grpc_millis kDefaultSweepIntervalMs = 1000;

namespace grpc_core {

class GlobalSubchannelPool::Sweeper : public InternallyRefCounted<Sweeper> {
 public:
  Sweeper(GlobalSubchannelPool* subchannel_pool =
              GlobalSubchannelPool::instance_raw())
      : subchannel_pool_(subchannel_pool) {
    gpr_mu_init(&mu_);
    char* sweep_interval_env =
        gpr_getenv("GRPC_SUBCHANNEL_INDEX_SWEEP_INTERVAL_MS");
    if (sweep_interval_env != nullptr) {
      int sweep_interval_ms = gpr_parse_nonnegative_int(sweep_interval_env);
      if (sweep_interval_ms == -1) {
        gpr_log(GPR_ERROR,
                "Invalid GRPC_SUBCHANNEL_INDEX_SWEEP_INTERVAL_MS: %s, default "
                "value %d will be used.",
                sweep_interval_env, static_cast<int>(sweep_interval_ms_));
      } else {
        sweep_interval_ms_ = static_cast<grpc_millis>(sweep_interval_ms);
      }
      gpr_free(sweep_interval_env);
    }
    GRPC_CLOSURE_INIT(&sweep_unused_subchannels_, SweepUnusedSubchannels, this,
                      grpc_schedule_on_exec_ctx);
    ScheduleNextSweep();
  }

  ~Sweeper() { gpr_mu_destroy(&mu_); }

  void Orphan() override {
    gpr_atm_no_barrier_store(&shutdown_, 1);
    MutexLock lock(&mu_);
    grpc_timer_cancel(&sweeper_timer_);
  }

 private:
  void ScheduleNextSweep() {
    const grpc_millis next_sweep_time =
        ::grpc_core::ExecCtx::Get()->Now() + sweep_interval_ms_;
    MutexLock lock(&mu_);
    grpc_timer_init(&sweeper_timer_, next_sweep_time,
                    &sweep_unused_subchannels_);
  }

  static void FindUnusedSubchannelsLocked(
      grpc_avl_node* avl_node,
      grpc_core::InlinedVector<Subchannel*, kUnusedSubchannelsInlinedSize>*
          unused_subchannels) {
    if (avl_node == nullptr) return;
    Subchannel* c = static_cast<Subchannel*>(avl_node->value);
    if (c->LastStrongRef()) unused_subchannels->emplace_back(c);
    FindUnusedSubchannelsLocked(avl_node->left, unused_subchannels);
    FindUnusedSubchannelsLocked(avl_node->right, unused_subchannels);
  }

  static void SweepUnusedSubchannels(void* arg, grpc_error* error) {
    Sweeper* sweeper = static_cast<Sweeper*>(arg);
    if (gpr_atm_no_barrier_load(&sweeper->shutdown_)) {
      Delete(sweeper);
      return;
    }
    GlobalSubchannelPool* subchannel_pool = sweeper->subchannel_pool_;
    grpc_core::InlinedVector<Subchannel*, kUnusedSubchannelsInlinedSize>
        unused_subchannels;
    // We use two-phase cleanup because modification during traversal is unsafe
    // for an AVL tree.
    {
      MutexLock lock(&subchannel_pool->mu_);
      FindUnusedSubchannelsLocked(subchannel_pool->subchannel_map_.root,
                                  &unused_subchannels);
    }
    subchannel_pool->UnregisterUnusedSubchannels(unused_subchannels);
    sweeper->ScheduleNextSweep();
  }

  grpc_millis sweep_interval_ms_ = kDefaultSweepIntervalMs;
  grpc_timer sweeper_timer_;
  grpc_closure sweep_unused_subchannels_;
  GlobalSubchannelPool* subchannel_pool_;
  gpr_atm shutdown_ = false;
  gpr_mu mu_;
};

namespace {
struct UserData {
  bool shutdown;
  grpc_pollset_set* pollset_set;
};
}  // namespace

GlobalSubchannelPool::GlobalSubchannelPool() {
  grpc_core::ExecCtx exec_ctx;
  subchannel_map_ = grpc_avl_create(&subchannel_avl_vtable_);
  gpr_mu_init(&mu_);
  // Start backup polling as long as the poll strategy is not specified "none".
  char* s = gpr_getenv("GRPC_POLL_STRATEGY");
  if (s == nullptr || strcmp(s, "none") != 0) {
    pollset_set_ = grpc_pollset_set_create();
    grpc_client_channel_start_backup_polling(pollset_set_);
  }
  gpr_free(s);
  // Set up the subchannel sweeper.
  sweeper_ = MakeOrphanable<Sweeper>(this);
}

GlobalSubchannelPool::~GlobalSubchannelPool() {
  if (pollset_set_ != nullptr) {
    grpc_client_channel_stop_backup_polling(pollset_set_);
    grpc_pollset_set_destroy(pollset_set_);
  }
  UserData user_data = {true, pollset_set_};
  grpc_avl_unref(subchannel_map_, &user_data);
  gpr_mu_destroy(&mu_);
}

void GlobalSubchannelPool::Init() {
  instance_ = New<RefCountedPtr<GlobalSubchannelPool>>(
      MakeRefCounted<GlobalSubchannelPool>());
}

void GlobalSubchannelPool::Shutdown() {
  // To ensure Init() was called before.
  GPR_ASSERT(instance_ != nullptr);
  // To ensure Shutdown() was not called before.
  GPR_ASSERT(*instance_ != nullptr);
  instance_->reset();
  // Some subchannels might have been unregistered and disconnected during
  // shutdown time. We should flush the closures before we wait for the iomgr
  // objects to be freed.
  grpc_core::ExecCtx::Get()->Flush();
  Delete(instance_);
}

RefCountedPtr<GlobalSubchannelPool> GlobalSubchannelPool::instance() {
  GPR_ASSERT(instance_ != nullptr);
  GPR_ASSERT(*instance_ != nullptr);
  return *instance_;
}

GlobalSubchannelPool* GlobalSubchannelPool::instance_raw() {
  GPR_ASSERT(instance_ != nullptr);
  GPR_ASSERT(*instance_ != nullptr);
  return (*instance_).get();
}

Subchannel* GlobalSubchannelPool::RegisterSubchannel(SubchannelKey* key,
                                                     Subchannel* constructed) {
  Subchannel* c = nullptr;
  UserData user_data = {false, nullptr};
  // Compare and swap (CAS) loop:
  while (c == nullptr) {
    // Ref the shared map to have a local copy.
    grpc_avl old_map;
    {
      MutexLock lock(&mu_);
      old_map = grpc_avl_ref(subchannel_map_, &user_data);
    }
    // Check to see if a subchannel already exists.
    c = static_cast<Subchannel*>(grpc_avl_get(old_map, key, &user_data));
    if (c != nullptr) {
      // The subchannel already exists. Reuse it.
      GRPC_SUBCHANNEL_REF(c, "index_register_reuse");
      GRPC_SUBCHANNEL_UNREF(constructed, "index_register_found_existing");
      // Exit the CAS loop without modifying the shared map.
    } else {
      // There hasn't been such subchannel. Add one.
      // Note that we should ref the old map first because grpc_avl_add() will
      // unref it while we still need to access it later.
      grpc_avl new_map = grpc_avl_add(
          grpc_avl_ref(old_map, &user_data), New<SubchannelKey>(*key),
          GRPC_SUBCHANNEL_REF(constructed, "index_register_new"), &user_data);
      // Try to publish the change to the shared map. It may happen (but
      // unlikely) that some other thread has changed the shared map, so compare
      // to make sure it's unchanged before swapping. Retry if it's changed.
      {
        MutexLock lock(&mu_);
        if (old_map.root == subchannel_map_.root) {
          GPR_SWAP(grpc_avl, new_map, subchannel_map_);
          c = constructed;
          grpc_pollset_set_add_pollset_set(c->pollset_set(), pollset_set_);
        }
      }
      grpc_avl_unref(new_map, &user_data);
    }
    grpc_avl_unref(old_map, &user_data);
  }
  return c;
}

Subchannel* GlobalSubchannelPool::FindSubchannel(SubchannelKey* key) {
  UserData user_data = {false, nullptr};
  // Lock, and take a reference to the subchannel map.
  // We don't need to do the search under a lock as AVL's are immutable.
  grpc_avl index;
  {
    MutexLock lock(&mu_);
    index = grpc_avl_ref(subchannel_map_, &user_data);
  }
  Subchannel* c =
      static_cast<Subchannel*>(grpc_avl_get(index, key, &user_data));
  if (c != nullptr) GRPC_SUBCHANNEL_REF(c, "index_find");
  grpc_avl_unref(index, &user_data);
  return c;
}

void GlobalSubchannelPool::TestOnlyStopSweep() {
  // For cancelling timer.
  grpc_core::ExecCtx exec_ctx;
  (*instance_)->sweeper_.reset();
}

void GlobalSubchannelPool::TestOnlyStartSweep() {
  grpc_core::ExecCtx exec_ctx;
  (*instance_)->sweeper_ = MakeOrphanable<Sweeper>();
}

void GlobalSubchannelPool::UnregisterUnusedSubchannels(
    const grpc_core::InlinedVector<grpc_core::Subchannel*, 4>&
        unused_subchannels) {
  UserData user_data = {false, nullptr};
  for (size_t i = 0; i < unused_subchannels.size(); ++i) {
    Subchannel* c = unused_subchannels[i];
    SubchannelKey* key = c->key();
    bool done = false;
    // Compare and swap (CAS) loop:
    while (!done) {
      // Ref the shared map to have a local copy.
      grpc_avl old_map;
      {
        MutexLock lock(&mu_);
        old_map = grpc_avl_ref(subchannel_map_, &user_data);
      }
      // Double check this subchannel is unused.
      if (c->LastStrongRef()) {
        // Remove the subchannel.
        // Note that we should ref the old map first because grpc_avl_remove()
        // will unref it while we still need to access it later.
        grpc_avl new_map =
            grpc_avl_remove(grpc_avl_ref(old_map, &user_data), key, &user_data);
        // Try to publish the change to the shared map. It may happen (but
        // unlikely) that some other thread has changed the shared map, so
        // compare to make sure it's unchanged before swapping. Retry if it's
        // changed.
        {
          MutexLock lock(&mu_);
          if (old_map.root == subchannel_map_.root) {
            GPR_SWAP(grpc_avl, new_map, subchannel_map_);
            grpc_pollset_set_del_pollset_set(c->pollset_set(), pollset_set_);
            done = true;
          }
        }
        grpc_avl_unref(new_map, &user_data);
      } else {
        done = true;
      }
      grpc_avl_unref(old_map, &user_data);
    }
  }
}

RefCountedPtr<GlobalSubchannelPool>* GlobalSubchannelPool::instance_ = nullptr;

namespace {

void sck_avl_destroy(void* p, void* user_data) {
  SubchannelKey* key = static_cast<SubchannelKey*>(p);
  Delete(key);
}

void* sck_avl_copy(void* p, void* unused) {
  const SubchannelKey* key = static_cast<const SubchannelKey*>(p);
  auto* new_key = New<SubchannelKey>(*key);
  return static_cast<void*>(new_key);
}

long sck_avl_compare(void* a, void* b, void* unused) {
  const SubchannelKey* key_a = static_cast<const SubchannelKey*>(a);
  const SubchannelKey* key_b = static_cast<const SubchannelKey*>(b);
  return key_a->Cmp(*key_b);
}

void scv_avl_destroy(void* p, void* user_data) {
  Subchannel* c = static_cast<Subchannel*>(p);
  GRPC_SUBCHANNEL_UNREF(c, "subchannel_index_scv_avl_destroy");
  UserData* ud = static_cast<UserData*>(user_data);
  if (ud->shutdown) {
    grpc_pollset_set_del_pollset_set(c->pollset_set(), ud->pollset_set);
  }
}

void* scv_avl_copy(void* p, void* unused) {
  Subchannel* c = static_cast<Subchannel*>(p);
  GRPC_SUBCHANNEL_REF(c, "subchannel_index_scv_avl_copy");
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
