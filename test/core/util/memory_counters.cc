/*
 *
 * Copyright 2016 gRPC authors.
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

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/surface/init.h"
#include "test/core/util/memory_counters.h"

static struct grpc_memory_counters g_memory_counters;
static gpr_allocation_functions g_old_allocs;

static void* guard_malloc(size_t size);
static void* guard_realloc(void* vptr, size_t size);
static void guard_free(void* vptr);

#ifdef GPR_LOW_LEVEL_COUNTERS
/* hide these from the microbenchmark atomic stats */
#define NO_BARRIER_FETCH_ADD(x, sz) \
  __atomic_fetch_add((x), (sz), __ATOMIC_RELAXED)
#define NO_BARRIER_LOAD(x) __atomic_load_n((x), __ATOMIC_RELAXED)
#else
#define NO_BARRIER_FETCH_ADD(x, sz) gpr_atm_no_barrier_fetch_add(x, sz)
#define NO_BARRIER_LOAD(x) gpr_atm_no_barrier_load(x)
#endif

static void* guard_malloc(size_t size) {
  if (!size) return nullptr;
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_absolute, (gpr_atm)size);
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_relative, (gpr_atm)size);
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_allocs_absolute, (gpr_atm)1);
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_allocs_relative, (gpr_atm)1);
  void* ptr = g_old_allocs.malloc_fn(
      GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size)) + size);
  *static_cast<size_t*>(ptr) = size;
  return static_cast<char*>(ptr) + GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size));
}

static void* guard_realloc(void* vptr, size_t size) {
  if (vptr == nullptr) {
    return guard_malloc(size);
  }
  if (size == 0) {
    guard_free(vptr);
    return nullptr;
  }
  void* ptr =
      static_cast<char*>(vptr) - GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size));
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_absolute, (gpr_atm)size);
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_relative,
                       -*static_cast<gpr_atm*>(ptr));
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_relative, (gpr_atm)size);
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_allocs_absolute, (gpr_atm)1);
  ptr = g_old_allocs.realloc_fn(
      ptr, GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size)) + size);
  *static_cast<size_t*>(ptr) = size;
  return static_cast<char*>(ptr) + GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size));
}

static void guard_free(void* vptr) {
  if (vptr == nullptr) return;
  void* ptr =
      static_cast<char*>(vptr) - GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size_t));
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_relative,
                       -*static_cast<gpr_atm*>(ptr));
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_allocs_relative, -(gpr_atm)1);
  g_old_allocs.free_fn(ptr);
}

struct gpr_allocation_functions g_guard_allocs = {guard_malloc, nullptr,
                                                  guard_realloc, guard_free};

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
      NO_BARRIER_LOAD(&g_memory_counters.total_size_relative);
  counters.total_size_absolute =
      NO_BARRIER_LOAD(&g_memory_counters.total_size_absolute);
  counters.total_allocs_relative =
      NO_BARRIER_LOAD(&g_memory_counters.total_allocs_relative);
  counters.total_allocs_absolute =
      NO_BARRIER_LOAD(&g_memory_counters.total_allocs_absolute);
  return counters;
}

namespace grpc_core {
namespace testing {

LeakDetector::LeakDetector(bool enable) : enabled_(enable) {
  if (enabled_) {
    grpc_memory_counters_init();
  }
}

LeakDetector::~LeakDetector() {
  // Wait for grpc_shutdown() to finish its async work.
  grpc_maybe_wait_for_async_shutdown();
  if (enabled_) {
    struct grpc_memory_counters counters = grpc_memory_counters_snapshot();
    if (counters.total_size_relative != 0) {
      gpr_log(GPR_ERROR, "Leaking %" PRIuPTR " bytes",
              static_cast<uintptr_t>(counters.total_size_relative));
      GPR_ASSERT(0);
    }
    grpc_memory_counters_destroy();
  }
}

}  // namespace testing
}  // namespace grpc_core
