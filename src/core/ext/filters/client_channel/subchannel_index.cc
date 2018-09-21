//
//
// Copyright 2016 gRPC authors.
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

#include "src/core/ext/filters/client_channel/subchannel_index.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/avl/avl.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tls.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer.h"

// TODO(dgq): When we C++-ify the relevant code, we need to make sure that any
// static variable in this file is trivially-destructible.

// If a subchannel only has one external ref left, which is held by the
// subchannel index, it is not used by any other external user (typically, LB
// policy). Instead of unregistering a subchannel once it's unused, the
// subchannel index will periodically sweep these unused subchannels, like a
// garbage collector. This mechanism can alleviate subchannel
// registration/unregistration churn. The subchannel can keep unchanged if it's
// re-used shortly after it's unused, which is desirable in the gRPC LB use
// case.
constexpr grpc_millis kDefaultSweepIntervalMs = 1000;
// This number was picked pseudo-randomly and could probably be tuned for
// performance reasons.
constexpr size_t kUnusedSubchannelsInlinedSize = 4;
static grpc_millis g_sweep_interval_ms = kDefaultSweepIntervalMs;
static grpc_timer g_sweeper_timer;
static grpc_closure g_sweep_unused_subchannels;
static gpr_atm g_sweep_active;

// a map of subchannel_key --> subchannel, used for detecting connections
// to the same destination in order to share them
static grpc_avl g_subchannel_index;
static gpr_mu g_mu;
static gpr_refcount g_refcount;
// For backup polling.
static grpc_pollset_set* g_pollset_set;

struct grpc_subchannel_key {
  grpc_subchannel_args args;
};

static bool g_force_creation = false;

static grpc_subchannel_key* create_key(
    const grpc_subchannel_args* args,
    grpc_channel_args* (*copy_channel_args)(const grpc_channel_args* args)) {
  grpc_subchannel_key* k =
      static_cast<grpc_subchannel_key*>(gpr_malloc(sizeof(*k)));
  k->args.filter_count = args->filter_count;
  if (k->args.filter_count > 0) {
    k->args.filters = static_cast<const grpc_channel_filter**>(
        gpr_malloc(sizeof(*k->args.filters) * k->args.filter_count));
    memcpy(reinterpret_cast<grpc_channel_filter*>(k->args.filters),
           args->filters, sizeof(*k->args.filters) * k->args.filter_count);
  } else {
    k->args.filters = nullptr;
  }
  k->args.args = copy_channel_args(args->args);
  return k;
}

grpc_subchannel_key* grpc_subchannel_key_create(
    const grpc_subchannel_args* args) {
  return create_key(args, grpc_channel_args_normalize);
}

static grpc_subchannel_key* subchannel_key_copy(grpc_subchannel_key* k) {
  return create_key(&k->args, grpc_channel_args_copy);
}

int grpc_subchannel_key_compare(const grpc_subchannel_key* a,
                                const grpc_subchannel_key* b) {
  // To pretend the keys are different, return a non-zero value.
  if (GPR_UNLIKELY(g_force_creation)) return 1;
  int c = GPR_ICMP(a->args.filter_count, b->args.filter_count);
  if (c != 0) return c;
  if (a->args.filter_count > 0) {
    c = memcmp(a->args.filters, b->args.filters,
               a->args.filter_count * sizeof(*a->args.filters));
    if (c != 0) return c;
  }
  return grpc_channel_args_compare(a->args.args, b->args.args);
}

void grpc_subchannel_key_destroy(grpc_subchannel_key* k) {
  gpr_free(reinterpret_cast<grpc_channel_args*>(k->args.filters));
  grpc_channel_args_destroy(const_cast<grpc_channel_args*>(k->args.args));
  gpr_free(k);
}

static void sck_avl_destroy(void* p, void* user_data) {
  grpc_subchannel_key_destroy(static_cast<grpc_subchannel_key*>(p));
}

static void* sck_avl_copy(void* p, void* unused) {
  return subchannel_key_copy(static_cast<grpc_subchannel_key*>(p));
}

static long sck_avl_compare(void* a, void* b, void* unused) {
  return grpc_subchannel_key_compare(static_cast<grpc_subchannel_key*>(a),
                                     static_cast<grpc_subchannel_key*>(b));
}

typedef struct user_data_t {
  grpc_core::ExecCtx* exec_ctx;
  bool unreffing_subchannel_index;
} user_data_t;

static void scv_avl_destroy(void* p, void* user_data) {
  GRPC_SUBCHANNEL_UNREF((grpc_subchannel*)p,
                        "subchannel_index_scv_avl_destroy");
  if (static_cast<user_data_t*>(user_data)->unreffing_subchannel_index) {
    grpc_pollset_set_del_pollset_set(
        grpc_subchannel_get_pollset_set((grpc_subchannel*)p), g_pollset_set);
  }
}

static void* scv_avl_copy(void* p, void* unused) {
  GRPC_SUBCHANNEL_REF((grpc_subchannel*)p, "subchannel_index_scv_avl_copy");
  return p;
}

static const grpc_avl_vtable subchannel_avl_vtable = {
    sck_avl_destroy,  // destroy_key
    sck_avl_copy,     // copy_key
    sck_avl_compare,  // compare_keys
    scv_avl_destroy,  // destroy_value
    scv_avl_copy      // copy_value
};

void grpc_subchannel_index_unref(void) {
  if (gpr_unref(&g_refcount)) {
    user_data_t user_data = {grpc_core::ExecCtx::Get(), true};
    grpc_avl_unref(g_subchannel_index, &user_data);
    gpr_mu_destroy(&g_mu);
  }
}

void grpc_subchannel_index_ref(void) { gpr_ref_non_zero(&g_refcount); }

grpc_subchannel* grpc_subchannel_index_find(grpc_subchannel_key* key) {
  // Lock, and take a reference to the subchannel index.
  // We don't need to do the search under a lock as avl's are immutable.
  user_data_t user_data = {grpc_core::ExecCtx::Get(), false};
  gpr_mu_lock(&g_mu);
  grpc_avl index = grpc_avl_ref(g_subchannel_index, &user_data);
  gpr_mu_unlock(&g_mu);
  grpc_subchannel* c = (grpc_subchannel*)grpc_avl_get(index, key, &user_data);
  if (c != nullptr) {
    GRPC_SUBCHANNEL_REF(c, "index_find");
  }
  grpc_avl_unref(index, &user_data);
  return c;
}

grpc_subchannel* grpc_subchannel_index_register(grpc_subchannel_key* key,
                                                grpc_subchannel* constructed) {
  grpc_subchannel* c = nullptr;
  user_data_t user_data = {grpc_core::ExecCtx::Get(), false};
  // Compare and swap loop:
  while (c == nullptr) {
    // Take a reference to the current index.
    gpr_mu_lock(&g_mu);
    grpc_avl index = grpc_avl_ref(g_subchannel_index, &user_data);
    gpr_mu_unlock(&g_mu);
    // Check to see if a subchannel already exists.
    c = static_cast<grpc_subchannel*>(grpc_avl_get(index, key, &user_data));
    if (c != nullptr) {
      // Already exists -> we're done.
      GRPC_SUBCHANNEL_REF(c, "index_register_reuse");
      GRPC_SUBCHANNEL_UNREF(constructed, "index_register_found_existing");
    } else {
      // Doesn't exist -> update the avl and compare/swap.
      grpc_avl updated = grpc_avl_add(
          grpc_avl_ref(index, &user_data), subchannel_key_copy(key),
          GRPC_SUBCHANNEL_REF(constructed, "index_register_new"), &user_data);
      // It may happen (but it's expected to be unlikely) that some other thread
      // has changed the index. We need to retry in such cases.
      gpr_mu_lock(&g_mu);
      if (index.root == g_subchannel_index.root) {
        GPR_SWAP(grpc_avl, updated, g_subchannel_index);
        c = constructed;
        grpc_pollset_set_add_pollset_set(grpc_subchannel_get_pollset_set(c),
                                         g_pollset_set);
      }
      gpr_mu_unlock(&g_mu);
      grpc_avl_unref(updated, &user_data);
    }
    grpc_avl_unref(index, &user_data);
  }
  return c;
}

static void find_unused_subchannels_locked(
    grpc_avl_node* avl_node,
    grpc_core::InlinedVector<grpc_subchannel*, kUnusedSubchannelsInlinedSize>*
        unused_subchannels) {
  if (avl_node == nullptr) return;
  grpc_subchannel* c = static_cast<grpc_subchannel*>(avl_node->value);
  if (grpc_subchannel_is_unused(c)) {
    unused_subchannels->emplace_back(c);
  }
  find_unused_subchannels_locked(avl_node->left, unused_subchannels);
  find_unused_subchannels_locked(avl_node->right, unused_subchannels);
}

static void unregister_unused_subchannels(
    const grpc_core::InlinedVector<
        grpc_subchannel*, kUnusedSubchannelsInlinedSize>& unused_subchannels) {
  user_data_t user_data = {grpc_core::ExecCtx::Get(), false};
  for (size_t i = 0; i < unused_subchannels.size(); ++i) {
    grpc_subchannel* c = unused_subchannels[i];
    grpc_subchannel_key* key = grpc_subchannel_get_key(c);
    bool done = false;
    // Compare and swap loop:
    while (!done) {
      // Take a reference to the current index.
      gpr_mu_lock(&g_mu);
      grpc_avl index = grpc_avl_ref(g_subchannel_index, &user_data);
      gpr_mu_unlock(&g_mu);
      if (grpc_subchannel_is_unused(c)) {
        grpc_avl updated =
            grpc_avl_remove(grpc_avl_ref(index, &user_data), key, &user_data);
        // It may happen (but it's expected to be unlikely) that some other
        // thread has changed the index. We need to retry in such cases.
        gpr_mu_lock(&g_mu);
        if (index.root == g_subchannel_index.root) {
          GPR_SWAP(grpc_avl, updated, g_subchannel_index);
          grpc_pollset_set_del_pollset_set(grpc_subchannel_get_pollset_set(c),
                                           g_pollset_set);
          done = true;
        }
        gpr_mu_unlock(&g_mu);
        grpc_avl_unref(updated, &user_data);
      } else {
        done = true;
      }
      grpc_avl_unref(index, &user_data);
    }
  }
}

static void schedule_next_sweep() {
  const grpc_millis next_sweep_time =
      ::grpc_core::ExecCtx::Get()->Now() + g_sweep_interval_ms;
  grpc_timer_init(&g_sweeper_timer, next_sweep_time,
                  &g_sweep_unused_subchannels);
}

static void sweep_unused_subchannels(void* /* arg */, grpc_error* error) {
  if (error != GRPC_ERROR_NONE || !gpr_atm_no_barrier_load(&g_sweep_active))
    return;
  grpc_core::InlinedVector<grpc_subchannel*, kUnusedSubchannelsInlinedSize>
      unused_subchannels;
  gpr_mu_lock(&g_mu);
  // We use two-phase cleanup because modification during traversal is unsafe
  // for an AVL tree.
  find_unused_subchannels_locked(g_subchannel_index.root, &unused_subchannels);
  gpr_mu_unlock(&g_mu);
  unregister_unused_subchannels(unused_subchannels);
  schedule_next_sweep();
}

void grpc_subchannel_index_init(void) {
  grpc_core::ExecCtx exec_ctx;
  g_subchannel_index = grpc_avl_create(&subchannel_avl_vtable);
  gpr_mu_init(&g_mu);
  gpr_ref_init(&g_refcount, 1);
  g_pollset_set = grpc_pollset_set_create();
  grpc_client_channel_start_backup_polling(g_pollset_set);
  // Set up the subchannel sweeper.
  char* sweep_interval_env =
      gpr_getenv("GRPC_SUBCHANNEL_INDEX_SWEEP_INTERVAL_MS");
  if (sweep_interval_env != nullptr) {
    int sweep_interval_ms = gpr_parse_nonnegative_int(sweep_interval_env);
    if (sweep_interval_ms == -1) {
      gpr_log(GPR_ERROR,
              "Invalid GRPC_SUBCHANNEL_INDEX_SWEEP_INTERVAL_MS: %s, default "
              "value %d will be used.",
              sweep_interval_env, static_cast<int>(g_sweep_interval_ms));
    } else {
      g_sweep_interval_ms = static_cast<grpc_millis>(sweep_interval_ms);
    }
    gpr_free(sweep_interval_env);
  }
  GRPC_CLOSURE_INIT(&g_sweep_unused_subchannels, sweep_unused_subchannels,
                    nullptr, grpc_schedule_on_exec_ctx);
  schedule_next_sweep();
}

void grpc_subchannel_index_shutdown(void) {
  // TODO(juanlishen): This refcounting mechanism may lead to memory leak. To
  // solve that, we should force polling to flush any pending callbacks, then
  // shut down safely.
  grpc_subchannel_index_unref();
  grpc_timer_cancel(&g_sweeper_timer);
  grpc_client_channel_stop_backup_polling(g_pollset_set);
  grpc_pollset_set_destroy(g_pollset_set);
  // Some subchannels might have been unregistered and disconnected during
  // shutdown time. We should flush the closures before we wait for the iomgr
  // objects to be freed.
  grpc_core::ExecCtx::Get()->Flush();
}

void grpc_subchannel_index_test_only_set_force_creation(bool force_creation) {
  g_force_creation = force_creation;
}

void grpc_subchannel_index_test_only_stop_sweep() {
  // For cancelling timer.
  grpc_core::ExecCtx exec_ctx;
  gpr_atm_no_barrier_store(&g_sweep_active, 0);
  grpc_timer_cancel(&g_sweeper_timer);
}

void grpc_subchannel_index_test_only_start_sweep() {
  grpc_core::ExecCtx exec_ctx;
  schedule_next_sweep();
}
