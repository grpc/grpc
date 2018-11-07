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

#include <grpc/support/port_platform.h>

#include "src/core/lib/gpr/arena.h"

#include <string.h>
#include <new>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gprpp/memory.h"

namespace {
enum init_strategy {
  NO_INIT,        // Do not initialize the arena blocks.
  ZERO_INIT,      // Initialize arena blocks with 0.
  NON_ZERO_INIT,  // Initialize arena blocks with a non-zero value.
};

gpr_once g_init_strategy_once = GPR_ONCE_INIT;
init_strategy g_init_strategy = NO_INIT;
}  // namespace

static void set_strategy_from_env() {
  char* str = gpr_getenv("GRPC_ARENA_INIT_STRATEGY");
  if (str == nullptr) {
    g_init_strategy = NO_INIT;
  } else if (strcmp(str, "zero_init") == 0) {
    g_init_strategy = ZERO_INIT;
  } else if (strcmp(str, "non_zero_init") == 0) {
    g_init_strategy = NON_ZERO_INIT;
  } else {
    g_init_strategy = NO_INIT;
  }
  gpr_free(str);
}

static void* gpr_arena_alloc_maybe_init(size_t size) {
  void* mem = gpr_malloc_aligned(size, GPR_MAX_ALIGNMENT);
  gpr_once_init(&g_init_strategy_once, set_strategy_from_env);
  if (GPR_UNLIKELY(g_init_strategy != NO_INIT)) {
    if (g_init_strategy == ZERO_INIT) {
      memset(mem, 0, size);
    } else {  // NON_ZERO_INIT.
      memset(mem, 0xFE, size);
    }
  }
  return mem;
}

void gpr_arena_init() {
  gpr_once_init(&g_init_strategy_once, set_strategy_from_env);
}

// Uncomment this to use a simple arena that simply allocates the
// requested amount of memory for each call to gpr_arena_alloc().  This
// effectively eliminates the efficiency gain of using an arena, but it
// may be useful for debugging purposes.
//#define SIMPLE_ARENA_FOR_DEBUGGING
#ifdef SIMPLE_ARENA_FOR_DEBUGGING

struct gpr_arena {
  gpr_arena() { gpr_mu_init(&mu); }
  ~gpr_arena() {
    gpr_mu_destroy(&mu);
    for (size_t i = 0; i < num_ptrs; ++i) {
      gpr_free_aligned(ptrs[i]);
    }
    gpr_free(ptrs);
  }

  gpr_mu mu;
  void** ptrs = nullptr;
  size_t num_ptrs = 0;
};

gpr_arena* gpr_arena_create(size_t ignored_initial_size) {
  return grpc_core::New<gpr_arena>();
}

size_t gpr_arena_destroy(gpr_arena* arena) {
  grpc_core::Delete(arena);
  return 1;  // Value doesn't matter, since it won't be used.
}

void* gpr_arena_alloc(gpr_arena* arena, size_t size) {
  gpr_mu_lock(&arena->mu);
  arena->ptrs =
      (void**)gpr_realloc(arena->ptrs, sizeof(void*) * (arena->num_ptrs + 1));
  void* retval = arena->ptrs[arena->num_ptrs++] =
      gpr_arena_alloc_maybe_init(size);
  gpr_mu_unlock(&arena->mu);
  return retval;
}

#else  // SIMPLE_ARENA_FOR_DEBUGGING

// TODO(roth): We currently assume that all callers need alignment of 16
// bytes, which may be wrong in some cases.  As part of converting the
// arena API to C++, we should consider replacing gpr_arena_alloc() with a
// template that takes the type of the value being allocated, which
// would allow us to use the alignment actually needed by the caller.

typedef struct zone {
  zone* next = nullptr;
} zone;

struct gpr_arena {
  gpr_arena(size_t initial_size)
      : initial_zone_size(initial_size), last_zone(&initial_zone) {
    gpr_mu_init(&arena_growth_mutex);
  }
  ~gpr_arena() {
    gpr_mu_destroy(&arena_growth_mutex);
    zone* z = initial_zone.next;
    while (z) {
      zone* next_z = z->next;
      z->~zone();
      gpr_free_aligned(z);
      z = next_z;
    }
  }

  // Keep track of the total used size. We use this in our call sizing
  // historesis.
  gpr_atm total_used = 0;
  size_t initial_zone_size;
  zone initial_zone;
  zone* last_zone;
  gpr_mu arena_growth_mutex;
};

gpr_arena* gpr_arena_create(size_t initial_size) {
  initial_size = GPR_ROUND_UP_TO_ALIGNMENT_SIZE(initial_size);
  return new (gpr_arena_alloc_maybe_init(
      GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(gpr_arena)) + initial_size))
      gpr_arena(initial_size);
}

size_t gpr_arena_destroy(gpr_arena* arena) {
  const gpr_atm size = gpr_atm_no_barrier_load(&arena->total_used);
  arena->~gpr_arena();
  gpr_free_aligned(arena);
  return static_cast<size_t>(size);
}

void* gpr_arena_alloc(gpr_arena* arena, size_t size) {
  size = GPR_ROUND_UP_TO_ALIGNMENT_SIZE(size);
  size_t begin = gpr_atm_no_barrier_fetch_add(&arena->total_used, size);
  if (begin + size <= arena->initial_zone_size) {
    return reinterpret_cast<char*>(arena) +
           GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(gpr_arena)) + begin;
  } else {
    // If the allocation isn't able to end in the initial zone, create a new
    // zone for this allocation, and any unused space in the initial zone is
    // wasted. This overflowing and wasting is uncommon because of our arena
    // sizing historesis (that is, most calls should have a large enough initial
    // zone and will not need to grow the arena).
    gpr_mu_lock(&arena->arena_growth_mutex);
    zone* z = new (gpr_arena_alloc_maybe_init(
        GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(zone)) + size)) zone();
    arena->last_zone->next = z;
    arena->last_zone = z;
    gpr_mu_unlock(&arena->arena_growth_mutex);
    return reinterpret_cast<char*>(z) +
           GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(zone));
  }
}

#endif  // SIMPLE_ARENA_FOR_DEBUGGING
