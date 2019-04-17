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

// \file Arena based allocator
// Allows very fast allocation of memory, but that memory cannot be freed until
// the arena as a whole is freed
// Tracks the total memory allocated against it, so that future arenas can
// pre-allocate the right amount of memory

#ifndef GRPC_CORE_LIB_GPR_ARENA_H
#define GRPC_CORE_LIB_GPR_ARENA_H

#include <grpc/support/port_platform.h>

#include <atomic>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gpr/alloc.h"

#include <stddef.h>

typedef struct gpr_arena gpr_arena;

// Create an arena, with \a initial_size bytes in the first allocated buffer
gpr_arena* gpr_arena_create(size_t initial_size);
// Destroy an arena, returning the total number of bytes allocated
size_t gpr_arena_destroy(gpr_arena* arena);

// Uncomment this to use a simple arena that simply allocates the
// requested amount of memory for each call to gpr_arena_alloc().  This
// effectively eliminates the efficiency gain of using an arena, but it
// may be useful for debugging purposes.
//#define SIMPLE_ARENA_FOR_DEBUGGING
#ifdef SIMPLE_ARENA_FOR_DEBUGGING

void* gpr_arena_alloc(gpr_arena* arena, size_t size);

#else  // SIMPLE_ARENA_FOR_DEBUGGING

struct gpr_arena {
  struct Zone {
   Zone* prev;
  };

  gpr_arena(size_t initial_size) : initial_zone_size(initial_size) {}
  ~gpr_arena() {
    Zone* z = last_zone;
    while (z) {
      Zone* prev_z = z->prev;
      z->~Zone();
      gpr_free_aligned(z);
      z = prev_z;
    }
  }
  void* Alloc(size_t size) {
    size = GPR_ROUND_UP_TO_ALIGNMENT_SIZE(size);
    size_t begin = gpr_atm_no_barrier_fetch_add(&total_used, size);
    if (begin + size <= initial_zone_size) {
      return reinterpret_cast<char*>(this) +
             GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(gpr_arena)) + begin;
    } else {
      return AllocZone(size);
    }
  }
  size_t Used() const {
    const gpr_atm size = gpr_atm_no_barrier_load(&total_used);
    return static_cast<size_t>(size);
  }

 private:
  void* AllocZone(size_t size);

  // Keep track of the total used size. We use this in our call sizing
  // hysteresis.
  gpr_atm total_used = 0;
  size_t initial_zone_size;
  std::atomic_flag arena_growth_spinlock = ATOMIC_FLAG_INIT;
  Zone* last_zone = nullptr;
};

// Allocate \a size bytes from the arena
inline void* gpr_arena_alloc(gpr_arena* arena, size_t size) {
  return arena->Alloc(size);
}

#endif  // SIMPLE_ARENA_FOR_DEBUGGING

#endif /* GRPC_CORE_LIB_GPR_ARENA_H */
