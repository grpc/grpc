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

#include <stdio.h>

static struct grpc_memory_counters g_memory_counters;
static bool g_memory_counter_enabled;

#ifdef GPR_LOW_LEVEL_COUNTERS
/* hide these from the microbenchmark atomic stats */
#define NO_BARRIER_FETCH_ADD(x, sz) \
  __atomic_fetch_add((x), (sz), __ATOMIC_RELAXED)
#define NO_BARRIER_LOAD(x) __atomic_load_n((x), __ATOMIC_RELAXED)
#else
#define NO_BARRIER_FETCH_ADD(x, sz) gpr_atm_no_barrier_fetch_add(x, sz)
#define NO_BARRIER_LOAD(x) gpr_atm_no_barrier_load(x)
#endif

// Memory counter uses --wrap=symbol feature from ld. To use this,
// `GPR_WRAP_MEMORY_COUNTER` needs to be defined. following  options should be
// passed to the compiler.
//   -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
// * Reference: https://linux.die.net/man/1/ld)
#if GPR_WRAP_MEMORY_COUNTER

extern "C" {
void* __real_malloc(size_t size);
void* __real_calloc(size_t size);
void* __real_realloc(void* ptr, size_t size);
void __real_free(void* ptr);

void* __wrap_malloc(size_t size);
void* __wrap_calloc(size_t size);
void* __wrap_realloc(void* ptr, size_t size);
void __wrap_free(void* ptr);
}

void* __wrap_malloc(size_t size) {
  if (!size) return nullptr;
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_absolute, (gpr_atm)size);
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_relative, (gpr_atm)size);
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_allocs_absolute, (gpr_atm)1);
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_allocs_relative, (gpr_atm)1);
  void* ptr =
      __real_malloc(GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size)) + size);
  *static_cast<size_t*>(ptr) = size;
  return static_cast<char*>(ptr) + GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size));
}

void* __wrap_calloc(size_t size) {
  if (!size) return nullptr;
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_absolute, (gpr_atm)size);
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_relative, (gpr_atm)size);
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_allocs_absolute, (gpr_atm)1);
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_allocs_relative, (gpr_atm)1);
  void* ptr =
      __real_calloc(GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size)) + size);
  *static_cast<size_t*>(ptr) = size;
  return static_cast<char*>(ptr) + GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size));
}

void* __wrap_realloc(void* ptr, size_t size) {
  if (ptr == nullptr) {
    return __wrap_malloc(size);
  }
  if (size == 0) {
    __wrap_free(ptr);
    return nullptr;
  }
  void* rptr =
      static_cast<char*>(ptr) - GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size));
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_absolute, (gpr_atm)size);
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_relative,
                       -*static_cast<gpr_atm*>(rptr));
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_relative, (gpr_atm)size);
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_allocs_absolute, (gpr_atm)1);
  void* new_ptr =
      __real_realloc(rptr, GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size)) + size);
  *static_cast<size_t*>(new_ptr) = size;
  return static_cast<char*>(new_ptr) +
         GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size));
}

void __wrap_free(void* ptr) {
  if (ptr == nullptr) return;
  void* rptr =
      static_cast<char*>(ptr) - GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(size_t));
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_size_relative,
                       -*static_cast<gpr_atm*>(rptr));
  NO_BARRIER_FETCH_ADD(&g_memory_counters.total_allocs_relative, -(gpr_atm)1);
  __real_free(rptr);
}

#endif  // GPR_WRAP_MEMORY_COUNTER

void grpc_memory_counters_init() {
  memset(&g_memory_counters, 0, sizeof(g_memory_counters));
  g_memory_counter_enabled = true;
}

void grpc_memory_counters_destroy() { g_memory_counter_enabled = false; }

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
