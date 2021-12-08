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

#ifndef GRPC_CORE_LIB_RESOURCE_QUOTA_ARENA_H
#define GRPC_CORE_LIB_RESOURCE_QUOTA_ARENA_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <atomic>
#include <memory>
#include <new>
#include <utility>

#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/memory_quota.h"

namespace grpc_core {

class Arena {
 public:
  // Create an arena, with \a initial_size bytes in the first allocated buffer.
  static Arena* Create(size_t initial_size, MemoryAllocator* memory_allocator);

  // Create an arena, with \a initial_size bytes in the first allocated buffer,
  // and return both a void pointer to the returned arena and a void* with the
  // first allocation.
  static std::pair<Arena*, void*> CreateWithAlloc(
      size_t initial_size, size_t alloc_size,
      MemoryAllocator* memory_allocator);

  // Destroy an arena, returning the total number of bytes allocated.
  size_t Destroy();
  // Allocate \a size bytes from the arena.
  void* Alloc(size_t size) {
    static constexpr size_t base_size =
        GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(Arena));
    size = GPR_ROUND_UP_TO_ALIGNMENT_SIZE(size);
    size_t begin = total_used_.fetch_add(size, std::memory_order_relaxed);
    if (begin + size <= initial_zone_size_) {
      return reinterpret_cast<char*>(this) + base_size + begin;
    } else {
      return AllocZone(size);
    }
  }

  // TODO(roth): We currently assume that all callers need alignment of 16
  // bytes, which may be wrong in some cases. When we have time, we should
  // change this to instead use the alignment of the type being allocated by
  // this method.
  template <typename T, typename... Args>
  T* New(Args&&... args) {
    T* t = static_cast<T*>(Alloc(sizeof(T)));
    new (t) T(std::forward<Args>(args)...);
    return t;
  }

 private:
  struct Zone {
    Zone* prev;
  };

  // Initialize an arena.
  // Parameters:
  //   initial_size: The initial size of the whole arena in bytes. These bytes
  //   are contained within 'zone 0'. If the arena user ends up requiring more
  //   memory than the arena contains in zone 0, subsequent zones are allocated
  //   on demand and maintained in a tail-linked list.
  //
  //   initial_alloc: Optionally, construct the arena as though a call to
  //   Alloc() had already been made for initial_alloc bytes. This provides a
  //   quick optimization (avoiding an atomic fetch-add) for the common case
  //   where we wish to create an arena and then perform an immediate
  //   allocation.
  explicit Arena(size_t initial_size, size_t initial_alloc,
                 MemoryAllocator* memory_allocator)
      : total_used_(GPR_ROUND_UP_TO_ALIGNMENT_SIZE(initial_alloc)),
        initial_zone_size_(initial_size),
        memory_allocator_(memory_allocator) {}

  ~Arena();

  void* AllocZone(size_t size);

  // Keep track of the total used size. We use this in our call sizing
  // hysteresis.
  std::atomic<size_t> total_used_{0};
  std::atomic<size_t> total_allocated_{0};
  const size_t initial_zone_size_;
  // If the initial arena allocation wasn't enough, we allocate additional zones
  // in a reverse linked list. Each additional zone consists of (1) a pointer to
  // the zone added before this zone (null if this is the first additional zone)
  // and (2) the allocated memory. The arena itself maintains a pointer to the
  // last zone; the zone list is reverse-walked during arena destruction only.
  std::atomic<Zone*> last_zone_{nullptr};
  // The backing memory quota
  MemoryAllocator* const memory_allocator_;
};

// Smart pointer for arenas when the final size is not required.
struct ScopedArenaDeleter {
  void operator()(Arena* arena) { arena->Destroy(); }
};
using ScopedArenaPtr = std::unique_ptr<Arena, ScopedArenaDeleter>;
inline ScopedArenaPtr MakeScopedArena(size_t initial_size,
                                      MemoryAllocator* memory_allocator) {
  return ScopedArenaPtr(Arena::Create(initial_size, memory_allocator));
}

// Arenas form a context for activities
template <>
struct ContextType<Arena> {};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_RESOURCE_QUOTA_ARENA_H */
