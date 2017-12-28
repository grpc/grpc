/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
static void destroy_intptr(void* not_used, void* user_data) {}
static void* copy_intptr(void* key, void* user_data) { return key; }
static long compare_intptr(void* key1, void* key2, void* user_data) {
  return key1 > key2;
}

static void destroy_tracker(void* tracker, void* user_data) {
  gpr_free((object_tracker*)tracker);
}

static void* copy_tracker(void* value, void* user_data) {
  object_tracker* old = static_cast<object_tracker*>(value);
  object_tracker* new_obj =
      static_cast<object_tracker*>(gpr_malloc(sizeof(object_tracker)));
  new_obj->object = old->object;
  new_obj->type = old->type;
  return new_obj;
}
static const gpr_avl_vtable avl_vtable = {
    destroy_intptr, copy_intptr, compare_intptr, destroy_tracker, copy_tracker};

void grpc_object_registry_init() {
  gpr_mu_init(&g_mu);
  g_avl = gpr_avl_create(&avl_vtable);
}

void grpc_object_registry_shutdown() {
  gpr_avl_unref(g_avl, nullptr);
  gpr_mu_destroy(&g_mu);
}

intptr_t grpc_object_registry_register_object(void* object,
                                              grpc_object_registry_type type) {
  object_tracker* tracker =
      static_cast<object_tracker*>(gpr_malloc(sizeof(object_tracker)));
  tracker->object = object;
  tracker->type = type;
  intptr_t prior = gpr_atm_no_barrier_fetch_add(&g_uuid, 1);
  gpr_mu_lock(&g_mu);
  g_avl = gpr_avl_add(g_avl, (void*)prior, tracker, NULL);
  gpr_mu_unlock(&g_mu);
  return prior;
}

void grpc_object_registry_unregister_object(intptr_t uuid) {
  gpr_mu_lock(&g_mu);
  g_avl = gpr_avl_remove(g_avl, (void*)uuid, nullptr);
  gpr_mu_unlock(&g_mu);
}

grpc_object_registry_type grpc_object_registry_get_object(intptr_t uuid,
                                                          void** object) {
  GPR_ASSERT(object);
  gpr_mu_lock(&g_mu);
  object_tracker* tracker =
      static_cast<object_tracker*>(gpr_avl_get(g_avl, (void*)uuid, nullptr));
  gpr_mu_unlock(&g_mu);
  if (tracker == NULL) {
    *object = NULL;
    return GRPC_OBJECT_REGISTRY_UNKNOWN;
  }
  *object = tracker->object;
  return tracker->type;
}
