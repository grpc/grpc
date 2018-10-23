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

#include "src/core/lib/avl/avl.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/tls.h"

// a map of subchannel_key --> subchannel, used for detecting connections
// to the same destination in order to share them
static grpc_avl g_subchannel_index;

static gpr_mu g_mu;

static gpr_refcount g_refcount;

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

static void scv_avl_destroy(void* p, void* user_data) {
  GRPC_SUBCHANNEL_WEAK_UNREF((grpc_subchannel*)p, "subchannel_index");
}

static void* scv_avl_copy(void* p, void* unused) {
  GRPC_SUBCHANNEL_WEAK_REF((grpc_subchannel*)p, "subchannel_index");
  return p;
}

static const grpc_avl_vtable subchannel_avl_vtable = {
    sck_avl_destroy,  // destroy_key
    sck_avl_copy,     // copy_key
    sck_avl_compare,  // compare_keys
    scv_avl_destroy,  // destroy_value
    scv_avl_copy      // copy_value
};

void grpc_subchannel_index_init(void) {
  g_subchannel_index = grpc_avl_create(&subchannel_avl_vtable);
  gpr_mu_init(&g_mu);
  gpr_ref_init(&g_refcount, 1);
}

void grpc_subchannel_index_shutdown(void) {
  // TODO(juanlishen): This refcounting mechanism may lead to memory leackage.
  // To solve that, we should force polling to flush any pending callbacks, then
  // shutdown safely.
  grpc_subchannel_index_unref();
}

void grpc_subchannel_index_unref(void) {
  if (gpr_unref(&g_refcount)) {
    gpr_mu_destroy(&g_mu);
    grpc_avl_unref(g_subchannel_index, grpc_core::ExecCtx::Get());
  }
}

void grpc_subchannel_index_ref(void) { gpr_ref_non_zero(&g_refcount); }

grpc_subchannel* grpc_subchannel_index_find(grpc_subchannel_key* key) {
  // Lock, and take a reference to the subchannel index.
  // We don't need to do the search under a lock as avl's are immutable.
  gpr_mu_lock(&g_mu);
  grpc_avl index = grpc_avl_ref(g_subchannel_index, grpc_core::ExecCtx::Get());
  gpr_mu_unlock(&g_mu);

  grpc_subchannel* c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(
      (grpc_subchannel*)grpc_avl_get(index, key, grpc_core::ExecCtx::Get()),
      "index_find");
  grpc_avl_unref(index, grpc_core::ExecCtx::Get());

  return c;
}

grpc_subchannel* grpc_subchannel_index_register(grpc_subchannel_key* key,
                                                grpc_subchannel* constructed) {
  grpc_subchannel* c = nullptr;
  bool need_to_unref_constructed = false;

  while (c == nullptr) {
    need_to_unref_constructed = false;

    // Compare and swap loop:
    // - take a reference to the current index
    gpr_mu_lock(&g_mu);
    grpc_avl index =
        grpc_avl_ref(g_subchannel_index, grpc_core::ExecCtx::Get());
    gpr_mu_unlock(&g_mu);

    // - Check to see if a subchannel already exists
    c = static_cast<grpc_subchannel*>(
        grpc_avl_get(index, key, grpc_core::ExecCtx::Get()));
    if (c != nullptr) {
      c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(c, "index_register");
    }
    if (c != nullptr) {
      // yes -> we're done
      need_to_unref_constructed = true;
    } else {
      // no -> update the avl and compare/swap
      grpc_avl updated =
          grpc_avl_add(grpc_avl_ref(index, grpc_core::ExecCtx::Get()),
                       subchannel_key_copy(key),
                       GRPC_SUBCHANNEL_WEAK_REF(constructed, "index_register"),
                       grpc_core::ExecCtx::Get());

      // it may happen (but it's expected to be unlikely)
      // that some other thread has changed the index:
      // compare/swap here to check that, and retry as necessary
      gpr_mu_lock(&g_mu);
      if (index.root == g_subchannel_index.root) {
        GPR_SWAP(grpc_avl, updated, g_subchannel_index);
        c = constructed;
      }
      gpr_mu_unlock(&g_mu);

      grpc_avl_unref(updated, grpc_core::ExecCtx::Get());
    }
    grpc_avl_unref(index, grpc_core::ExecCtx::Get());
  }

  if (need_to_unref_constructed) {
    GRPC_SUBCHANNEL_UNREF(constructed, "index_register");
  }

  return c;
}

void grpc_subchannel_index_unregister(grpc_subchannel_key* key,
                                      grpc_subchannel* constructed) {
  bool done = false;
  while (!done) {
    // Compare and swap loop:
    // - take a reference to the current index
    gpr_mu_lock(&g_mu);
    grpc_avl index =
        grpc_avl_ref(g_subchannel_index, grpc_core::ExecCtx::Get());
    gpr_mu_unlock(&g_mu);

    // Check to see if this key still refers to the previously
    // registered subchannel
    grpc_subchannel* c = static_cast<grpc_subchannel*>(
        grpc_avl_get(index, key, grpc_core::ExecCtx::Get()));
    if (c != constructed) {
      grpc_avl_unref(index, grpc_core::ExecCtx::Get());
      break;
    }

    // compare and swap the update (some other thread may have
    // mutated the index behind us)
    grpc_avl updated =
        grpc_avl_remove(grpc_avl_ref(index, grpc_core::ExecCtx::Get()), key,
                        grpc_core::ExecCtx::Get());

    gpr_mu_lock(&g_mu);
    if (index.root == g_subchannel_index.root) {
      GPR_SWAP(grpc_avl, updated, g_subchannel_index);
      done = true;
    }
    gpr_mu_unlock(&g_mu);

    grpc_avl_unref(updated, grpc_core::ExecCtx::Get());
    grpc_avl_unref(index, grpc_core::ExecCtx::Get());
  }
}

void grpc_subchannel_index_test_only_set_force_creation(bool force_creation) {
  g_force_creation = force_creation;
}
