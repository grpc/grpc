/*
 *
 * Copyright 2016, Google Inc.
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

#include <stdint.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>

#include "test/core/util/memory_counters.h"

static gpr_mu g_memory_mutex;
static struct grpc_memory_counters g_memory_counters;
static gpr_allocation_functions g_old_allocs;

static void *guard_malloc(size_t size);
static void *guard_realloc(void *vptr, size_t size);
static void guard_free(void *vptr);

static void *guard_malloc(size_t size) {
  size_t *ptr;
  if (!size) return NULL;
  gpr_mu_lock(&g_memory_mutex);
  g_memory_counters.total_size_absolute += size;
  g_memory_counters.total_size_relative += size;
  g_memory_counters.total_allocs_absolute++;
  g_memory_counters.total_allocs_relative++;
  gpr_mu_unlock(&g_memory_mutex);
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
  gpr_mu_lock(&g_memory_mutex);
  g_memory_counters.total_size_absolute += size;
  g_memory_counters.total_size_relative -= *ptr;
  g_memory_counters.total_size_relative += size;
  g_memory_counters.total_allocs_absolute++;
  gpr_mu_unlock(&g_memory_mutex);
  ptr = g_old_allocs.realloc_fn(ptr, size + sizeof(size));
  *ptr++ = size;
  return ptr;
}

static void guard_free(void *vptr) {
  size_t *ptr = vptr;
  if (!vptr) return;
  --ptr;
  gpr_mu_lock(&g_memory_mutex);
  g_memory_counters.total_size_relative -= *ptr;
  g_memory_counters.total_allocs_relative--;
  gpr_mu_unlock(&g_memory_mutex);
  g_old_allocs.free_fn(ptr);
}

struct gpr_allocation_functions g_guard_allocs = {guard_malloc, guard_realloc,
                                                  guard_free};

void grpc_memory_counters_init() {
  memset(&g_memory_counters, 0, sizeof(g_memory_counters));
  gpr_mu_init(&g_memory_mutex);
  g_old_allocs = gpr_get_allocation_functions();
  gpr_set_allocation_functions(g_guard_allocs);
}

void grpc_memory_counters_destroy() {
  gpr_set_allocation_functions(g_old_allocs);
  gpr_mu_destroy(&g_memory_mutex);
}

struct grpc_memory_counters grpc_memory_counters_snapshot() {
  struct grpc_memory_counters counters;
  gpr_mu_lock(&g_memory_mutex);
  counters = g_memory_counters;
  gpr_mu_unlock(&g_memory_mutex);
  return counters;
}
