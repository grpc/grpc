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

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gpr/alloc.h"

// Uncomment this to use a simple arena that simply allocates the
// requested amount of memory for each call to gpr_arena_alloc().  This
// effectively eliminates the efficiency gain of using an arena, but it
// may be useful for debugging purposes.
//#define SIMPLE_ARENA_FOR_DEBUGGING

#ifdef SIMPLE_ARENA_FOR_DEBUGGING

struct gpr_arena {
  gpr_mu mu;
  void** ptrs;
  size_t num_ptrs;
};

gpr_arena* gpr_arena_create(size_t ignored_initial_size) {
  gpr_arena* arena = (gpr_arena*)gpr_zalloc(sizeof(*arena));
  gpr_mu_init(&arena->mu);
  return arena;
}

size_t gpr_arena_destroy(gpr_arena* arena) {
  gpr_mu_destroy(&arena->mu);
  for (size_t i = 0; i < arena->num_ptrs; ++i) {
    gpr_free(arena->ptrs[i]);
  }
  gpr_free(arena->ptrs);
  gpr_free(arena);
  return 1;  // Value doesn't matter, since it won't be used.
}

void* gpr_arena_alloc(gpr_arena* arena, size_t size) {
  gpr_mu_lock(&arena->mu);
  arena->ptrs =
      (void**)gpr_realloc(arena->ptrs, sizeof(void*) * (arena->num_ptrs + 1));
  void* retval = arena->ptrs[arena->num_ptrs++] = gpr_zalloc(size);
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
  size_t size_begin;  // All the space we have set aside for allocations up
                      // until this zone.
  size_t size_end;  // size_end = size_begin plus all the space we set aside for
                    // allocations in zone z itself.
  zone* next;
} zone;

struct gpr_arena {
  gpr_atm size_so_far;
  zone initial_zone;
  gpr_mu arena_growth_mutex;
};

static void* zalloc_aligned(size_t size) {
  void* ptr = gpr_malloc_aligned(size, GPR_MAX_ALIGNMENT);
  memset(ptr, 0, size);
  return ptr;
}

gpr_arena* gpr_arena_create(size_t initial_size) {
  initial_size = GPR_ROUND_UP_TO_ALIGNMENT_SIZE(initial_size);
  gpr_arena* a = static_cast<gpr_arena*>(zalloc_aligned(
      GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(gpr_arena)) + initial_size));
  a->initial_zone.size_end = initial_size;
  gpr_mu_init(&a->arena_growth_mutex);
  return a;
}

size_t gpr_arena_destroy(gpr_arena* arena) {
  gpr_mu_destroy(&arena->arena_growth_mutex);
  gpr_atm size = gpr_atm_no_barrier_load(&arena->size_so_far);
  zone* z = arena->initial_zone.next;
  gpr_free_aligned(arena);
  while (z) {
    zone* next_z = z->next;
    gpr_free_aligned(z);
    z = next_z;
  }
  return static_cast<size_t>(size);
}

void* gpr_arena_alloc(gpr_arena* arena, size_t size) {
  size = GPR_ROUND_UP_TO_ALIGNMENT_SIZE(size);
  size_t previous_size_of_arena_allocations = static_cast<size_t>(
      gpr_atm_no_barrier_fetch_add(&arena->size_so_far, size));
  size_t updated_size_of_arena_allocations =
      previous_size_of_arena_allocations + size;
  zone* z = &arena->initial_zone;
  // Check to see if the allocation isn't able to end in the initial zone.
  // This statement is true only in the uncommon case because of our arena
  // sizing historesis (that is, most calls should have a large enough initial
  // zone and will not need to grow the arena).
  if (updated_size_of_arena_allocations > z->size_end) {
    // Find a zone to fit this allocation
    gpr_mu_lock(&arena->arena_growth_mutex);
    while (updated_size_of_arena_allocations > z->size_end) {
      if (z->next == nullptr) {
        // Note that we do an extra increment of size_so_far to prevent multiple
        // simultaneous callers from stepping on each other. However, this extra
        // increment means some space in the arena is wasted.
        // So whenever we need to allocate x bytes and there are x - n (where
        // n > 0) remaining in the current zone, we will waste x bytes (x - n
        // in the current zone and n in the new zone).
        previous_size_of_arena_allocations = static_cast<size_t>(
            gpr_atm_no_barrier_fetch_add(&arena->size_so_far, size));
        updated_size_of_arena_allocations =
            previous_size_of_arena_allocations + size;
        size_t next_z_size = updated_size_of_arena_allocations;
        z->next = static_cast<zone*>(zalloc_aligned(
            GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(zone)) + next_z_size));
        z->next->size_begin = z->size_end;
        z->next->size_end = z->size_end + next_z_size;
      }
      z = z->next;
    }
    gpr_mu_unlock(&arena->arena_growth_mutex);
  }
  GPR_ASSERT(previous_size_of_arena_allocations >= z->size_begin);
  GPR_ASSERT(updated_size_of_arena_allocations <= z->size_end);
  // Skip the first part of the zone, which just contains tracking information.
  // For the initial zone, this is the gpr_arena struct and for any other zone,
  // it's the zone struct.
  char* start_of_allocation_space =
      (z == &arena->initial_zone)
          ? reinterpret_cast<char*>(arena) +
                GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(gpr_arena))
          : reinterpret_cast<char*>(z) +
                GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(zone));
  // previous_size_of_arena_allocations - size_begin is how many bytes have been
  // allocated into the current zone
  return start_of_allocation_space + previous_size_of_arena_allocations -
         z->size_begin;
}

#endif  // SIMPLE_ARENA_FOR_DEBUGGING
