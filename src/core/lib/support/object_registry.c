/*
 *
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/lib/support/object_registry.h"

#include <grpc/support/alloc.h>
#include <grpc/support/avl.h>
#include <grpc/support/log.h>

// file global lock and avl.
static gpr_mu g_mu;
static gpr_avl g_avl;
static intptr_t g_uuid = 0;

typedef struct {
  void* object;
  grpc_object_registry_type type;
} object_tracker;

// avl vtable for uuid (intptr_t) -> object_tracker
// this table is only looking, it does not own anything.
static void destroy_intptr(void* not_used) {}
static void* copy_intptr(void* key) { return key; }
static long compare_intptr(void* key1, void* key2) { return key1 > key2; }

static void destroy_tracker(void* tracker) {
  gpr_free((object_tracker*)tracker);
}

static void* copy_tracker(void* value) {
  object_tracker* old = value;
  object_tracker* new = gpr_malloc(sizeof(object_tracker));
  new->object = old->object;
  new->type = old->type;
  return new;
}
static const gpr_avl_vtable avl_vtable = {
    destroy_intptr, copy_intptr, compare_intptr, destroy_tracker, copy_tracker};

void grpc_object_registry_init() {
  gpr_mu_init(&g_mu);
  g_avl = gpr_avl_create(&avl_vtable);
}

void grpc_object_registry_shutdown() {
  gpr_avl_unref(g_avl);
  gpr_mu_destroy(&g_mu);
}

intptr_t grpc_object_registry_register_object(void* object,
                                              grpc_object_registry_type type) {
  object_tracker* tracker = gpr_malloc(sizeof(object_tracker));
  tracker->object = object;
  tracker->type = type;
  intptr_t prior = gpr_atm_no_barrier_fetch_add(&g_uuid, 1);
  gpr_mu_lock(&g_mu);
  g_avl = gpr_avl_add(g_avl, (void*)prior, tracker);
  gpr_mu_unlock(&g_mu);
  return prior;
}

void grpc_object_registry_unregister_object(intptr_t uuid) {
  gpr_mu_lock(&g_mu);
  g_avl = gpr_avl_remove(g_avl, (void*)uuid);
  gpr_mu_unlock(&g_mu);
}

grpc_object_registry_type grpc_object_registry_get_object(intptr_t uuid,
                                                          void** object) {
  GPR_ASSERT(object);
  gpr_mu_lock(&g_mu);
  object_tracker* tracker = gpr_avl_get(g_avl, (void*)uuid);
  gpr_mu_unlock(&g_mu);
  GPR_ASSERT(tracker);
  *object = tracker->object;
  return tracker->type;
}
