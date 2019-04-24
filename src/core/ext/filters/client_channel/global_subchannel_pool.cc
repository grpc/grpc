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

constexpr grpc_millis kDefaultSweepIntervalMs = 1000;

namespace grpc_core {

class GlobalSubchannelPool::Sweeper : public InternallyRefCounted<Sweeper> {
 public:
  Sweeper(GlobalSubchannelPool* subchannel_pool =
              GlobalSubchannelPool::instance_raw())
      : subchannel_pool_(subchannel_pool) {
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
    Ref().release();  // Ref for sweep callback.
    ScheduleNextSweep();
  }

  void Orphan() override {
    {
      MutexLock lock(&mu_);
      shutdown_ = true;
      grpc_timer_cancel(&next_sweep_timer_);
    }
    Unref();  // Drop initial ref.
  }

 private:
  void ScheduleNextSweep() {
    // The next sweep is scheduled relative to the time that the current run
    // ends. This ensures that we will wait for a long enough period before the
    // next sweep, so that we don't burden the CPU too much when sweeping takes
    // a long time. The trade-off here is that if a sweep takes too long, unused
    // subchannels may actually stick around longer than the configured sweep
    // interval.
    const grpc_millis next_sweep_time =
        ExecCtx::Get()->Now() + sweep_interval_ms_;
    MutexLock lock(&mu_);
    grpc_timer_init(&next_sweep_timer_, next_sweep_time,
                    &sweep_unused_subchannels_);
  }

  static void FindUnusedSubchannelsLocked(
      grpc_avl_node* avl_node, UnusedSubchanels* unused_subchannels) {
    if (avl_node == nullptr) return;
    Subchannel* c = static_cast<Subchannel*>(avl_node->value);
    if (c->LastStrongRef()) unused_subchannels->emplace_back(c);
    FindUnusedSubchannelsLocked(avl_node->left, unused_subchannels);
    FindUnusedSubchannelsLocked(avl_node->right, unused_subchannels);
  }

  static void SweepUnusedSubchannels(void* arg, grpc_error* error) {
    Sweeper* sweeper = static_cast<Sweeper*>(arg);
    ReleasableMutexLock lock(&sweeper->mu_);
    if (sweeper->shutdown_ || error != GRPC_ERROR_NONE) {
      lock.Unlock();
      sweeper->Unref();
      return;
    }
    lock.Unlock();
    GlobalSubchannelPool* subchannel_pool = sweeper->subchannel_pool_;
    UnusedSubchanels unused_subchannels;
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

  GlobalSubchannelPool* subchannel_pool_;
  grpc_closure sweep_unused_subchannels_;
  grpc_millis sweep_interval_ms_ = kDefaultSweepIntervalMs;
  Mutex mu_;  // Protect the data members below.
  grpc_timer next_sweep_timer_;
  bool shutdown_ = false;
};

GlobalSubchannelPool::GlobalSubchannelPool() {
  subchannel_map_ = grpc_avl_create(&subchannel_avl_vtable_);
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
  grpc_avl_unref(subchannel_map_, pollset_set_);
  if (pollset_set_ != nullptr) {
    grpc_client_channel_stop_backup_polling(pollset_set_);
    grpc_pollset_set_destroy(pollset_set_);
  }
}

void GlobalSubchannelPool::Init() {
  ExecCtx exec_ctx;
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
  ExecCtx::Get()->Flush();
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
  // Compare and swap (CAS) loop:
  while (c == nullptr) {
    // Ref the shared map to have a local copy.
    grpc_avl old_map;
    {
      MutexLock lock(&mu_);
      old_map = grpc_avl_ref(subchannel_map_, nullptr);
    }
    // Check to see if a subchannel already exists.
    c = static_cast<Subchannel*>(grpc_avl_get(old_map, key, nullptr));
    if (c != nullptr) {
      // The subchannel already exists. Try to reuse it.
      c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(c, "subchannel_register+reuse");
      if (c != nullptr) {
        GRPC_SUBCHANNEL_UNREF(constructed,
                              "subchannel_register+found_existing");
        // Exit the CAS loop without modifying the shared map.
      }  // Else, reuse failed, so retry CAS loop.
    } else {
      // There hasn't been such subchannel. Add one.
      // Note that we should ref the old map first because grpc_avl_add() will
      // unref it while we still need to access it later.
      grpc_avl new_map = grpc_avl_add(
          grpc_avl_ref(old_map, nullptr), New<SubchannelKey>(*key),
          GRPC_SUBCHANNEL_REF(constructed, "subchannel_register+new"), nullptr);
      // Try to publish the change to the shared map. It may happen (but
      // unlikely) that some other thread has changed the shared map, so compare
      // to make sure it's unchanged before swapping. Retry if it's changed.
      {
        MutexLock lock(&mu_);
        if (old_map.root == subchannel_map_.root) {
          GPR_SWAP(grpc_avl, new_map, subchannel_map_);
          c = constructed;
        }
      }
      if (c != nullptr) {
        grpc_pollset_set_add_pollset_set(c->pollset_set(), pollset_set_);
      }
      grpc_avl_unref(new_map, nullptr);
    }
    grpc_avl_unref(old_map, nullptr);
  }
  return c;
}

Subchannel* GlobalSubchannelPool::FindSubchannel(SubchannelKey* key) {
  // Lock, and take a reference to the subchannel map.
  // We don't need to do the search under a lock as AVL's are immutable.
  grpc_avl index;
  {
    MutexLock lock(&mu_);
    index = grpc_avl_ref(subchannel_map_, nullptr);
  }
  Subchannel* c = static_cast<Subchannel*>(grpc_avl_get(index, key, nullptr));
  if (c != nullptr) c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(c, "found_from_pool");
  grpc_avl_unref(index, nullptr);
  return c;
}

void GlobalSubchannelPool::TestOnlyStopSweep() {
  ExecCtx exec_ctx;  // For cancelling timer.
  (*instance_)->sweeper_.reset();
}

void GlobalSubchannelPool::TestOnlyStartSweep() {
  ExecCtx exec_ctx;
  (*instance_)->sweeper_ = MakeOrphanable<Sweeper>();
}

void GlobalSubchannelPool::UnregisterUnusedSubchannels(
    const UnusedSubchanels& unused_subchannels) {
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
        old_map = grpc_avl_ref(subchannel_map_, nullptr);
      }
      // Double check this subchannel is unused. Note that even if a race still
      // happens, the penalty is that we lose a chance to reuse this subchannel,
      // which is fine.
      if (c->LastStrongRef()) {
        // Remove the subchannel.
        // Note that we should ref the old map first because grpc_avl_remove()
        // will unref it while we still need to access it later.
        grpc_avl new_map =
            grpc_avl_remove(grpc_avl_ref(old_map, nullptr), key, nullptr);
        // Try to publish the change to the shared map. It may happen (but
        // unlikely) that some other thread has changed the shared map, so
        // compare to make sure it's unchanged before swapping. Retry if it's
        // changed.
        {
          MutexLock lock(&mu_);
          if (old_map.root == subchannel_map_.root) {
            GPR_SWAP(grpc_avl, new_map, subchannel_map_);
            done = true;
          }
        }
        if (done) {
          grpc_pollset_set_del_pollset_set(c->pollset_set(), pollset_set_);
        }
        grpc_avl_unref(new_map, nullptr);
      } else {
        done = true;
      }
      grpc_avl_unref(old_map, nullptr);
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
  GRPC_SUBCHANNEL_UNREF(c, "global_subchannel_pool");
  grpc_pollset_set* pollset_set = static_cast<grpc_pollset_set*>(user_data);
  if (pollset_set != nullptr) {
    grpc_pollset_set_del_pollset_set(c->pollset_set(), pollset_set);
  }
}

void* scv_avl_copy(void* p, void* unused) {
  Subchannel* c = static_cast<Subchannel*>(p);
  GRPC_SUBCHANNEL_REF(c, "global_subchannel_pool");
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
