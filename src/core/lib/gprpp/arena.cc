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

#include "src/core/lib/gprpp/arena.h"

#include <string.h>
#include <new>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/gprpp/memory.h"

template <size_t alignment>
static void* gpr_arena_malloc(size_t size) {
  return gpr_malloc_aligned(size, alignment);
}

namespace grpc_core {

Arena::~Arena() {
  Zone* z = last_zone_;
  while (z) {
    Zone* prev_z = z->prev;
    z->~Zone();
    gpr_free_aligned(z);
    z = prev_z;
  }
}

// TODO(roth): We currently assume that all callers need alignment of 16
// bytes, which may be wrong in some cases.  As part of converting the
// arena API to C++, we should consider replacing gpr_arena_alloc() with a
// template that takes the type of the value being allocated, which
// would allow us to use the alignment actually needed by the caller.
Arena* Arena::Create(size_t initial_size) {
  static constexpr size_t base_size =
      GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(Arena));
  initial_size = GPR_ROUND_UP_TO_ALIGNMENT_SIZE(initial_size);
  size_t alloc_size = base_size + initial_size;
  static constexpr size_t alignment =
      (GPR_CACHELINE_SIZE > GPR_MAX_ALIGNMENT &&
       GPR_CACHELINE_SIZE % GPR_MAX_ALIGNMENT == 0)
          ? GPR_CACHELINE_SIZE
          : GPR_MAX_ALIGNMENT;
  return new (gpr_arena_malloc<alignment>(alloc_size)) Arena(initial_size);
}

size_t Arena::Destroy() {
  size_t size = total_used_.Load(MemoryOrder::RELAXED);
  this->~Arena();
  gpr_free_aligned(this);
  return size;
}

void* Arena::AllocZone(size_t size) {
  // If the allocation isn't able to end in the initial zone, create a new
  // zone for this allocation, and any unused space in the initial zone is
  // wasted. This overflowing and wasting is uncommon because of our arena
  // sizing hysteresis (that is, most calls should have a large enough initial
  // zone and will not need to grow the arena).
  static constexpr size_t zone_base_size =
      GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(Zone));
  size_t alloc_size = zone_base_size + size;
  Zone* z = new (gpr_arena_malloc<GPR_MAX_ALIGNMENT>(alloc_size)) Zone();
  {
    gpr_spinlock_lock(&arena_growth_spinlock_);
    z->prev = last_zone_;
    last_zone_ = z;
    gpr_spinlock_unlock(&arena_growth_spinlock_);
  }
  return reinterpret_cast<char*>(z) + zone_base_size;
}

}  // namespace grpc_core
