/*
 *
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdint.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>

#include "test/core/util/memory_counters.h"

static struct grpc_memory_counters g_memory_counters;
static gpr_allocation_functions g_old_allocs;

static void *guard_malloc(size_t size);
static void *guard_realloc(void *vptr, size_t size);
static void guard_free(void *vptr);

static void *guard_malloc(size_t size) {
  size_t *ptr;
  if (!size) return NULL;
  gpr_atm_no_barrier_fetch_add(&g_memory_counters.total_size_absolute,
                               (gpr_atm)size);
  gpr_atm_no_barrier_fetch_add(&g_memory_counters.total_size_relative,
                               (gpr_atm)size);
  gpr_atm_no_barrier_fetch_add(&g_memory_counters.total_allocs_absolute,
                               (gpr_atm)1);
  gpr_atm_no_barrier_fetch_add(&g_memory_counters.total_allocs_relative,
                               (gpr_atm)1);
  ptr = g_old_allocs.malloc_fn(size + sizeof(size));
  *ptr++ = size;
  return ptr;
}

static void *guard_realloc(void *vptr, size_t size) {
  size_t *ptr = vptr;
  if (vptr == NULL) {
    return guard_malloc(size);
  }
  if (size == 0) {
    guard_free(vptr);
    return NULL;
  }
  --ptr;
  gpr_atm_no_barrier_fetch_add(&g_memory_counters.total_size_absolute,
                               (gpr_atm)size);
  gpr_atm_no_barrier_fetch_add(&g_memory_counters.total_size_relative,
                               -(gpr_atm)*ptr);
  gpr_atm_no_barrier_fetch_add(&g_memory_counters.total_size_relative,
                               (gpr_atm)size);
  gpr_atm_no_barrier_fetch_add(&g_memory_counters.total_allocs_absolute,
                               (gpr_atm)1);
  ptr = g_old_allocs.realloc_fn(ptr, size + sizeof(size));
  *ptr++ = size;
  return ptr;
}

static void guard_free(void *vptr) {
  size_t *ptr = vptr;
  if (!vptr) return;
  --ptr;
  gpr_atm_no_barrier_fetch_add(&g_memory_counters.total_size_relative,
                               -(gpr_atm)*ptr);
  gpr_atm_no_barrier_fetch_add(&g_memory_counters.total_allocs_relative,
                               -(gpr_atm)1);
  g_old_allocs.free_fn(ptr);
}

struct gpr_allocation_functions g_guard_allocs = {guard_malloc, guard_realloc,
                                                  guard_free};

void grpc_memory_counters_init() {
  memset(&g_memory_counters, 0, sizeof(g_memory_counters));
  g_old_allocs = gpr_get_allocation_functions();
  gpr_set_allocation_functions(g_guard_allocs);
}

void grpc_memory_counters_destroy() {
  gpr_set_allocation_functions(g_old_allocs);
}

struct grpc_memory_counters grpc_memory_counters_snapshot() {
  struct grpc_memory_counters counters;
  counters.total_size_relative =
      gpr_atm_no_barrier_load(&g_memory_counters.total_size_relative);
  counters.total_size_absolute =
      gpr_atm_no_barrier_load(&g_memory_counters.total_size_absolute);
  counters.total_allocs_relative =
      gpr_atm_no_barrier_load(&g_memory_counters.total_allocs_relative);
  counters.total_allocs_absolute =
      gpr_atm_no_barrier_load(&g_memory_counters.total_allocs_absolute);
  return counters;
}
