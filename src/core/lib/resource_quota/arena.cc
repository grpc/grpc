//
//
// Copyright 2017 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/resource_quota/arena.h"

#include <atomic>
#include <new>

#include <grpc/support/alloc.h>

#include "src/core/lib/gpr/alloc.h"

namespace {

void* ArenaStorage(size_t initial_size) {
  static constexpr size_t base_size =
      GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(grpc_core::Arena));
  initial_size = GPR_ROUND_UP_TO_ALIGNMENT_SIZE(initial_size);
  size_t alloc_size = base_size + initial_size;
  static constexpr size_t alignment =
      (GPR_CACHELINE_SIZE > GPR_MAX_ALIGNMENT &&
       GPR_CACHELINE_SIZE % GPR_MAX_ALIGNMENT == 0)
          ? GPR_CACHELINE_SIZE
          : GPR_MAX_ALIGNMENT;
  return gpr_malloc_aligned(alloc_size, alignment);
}

}  // namespace

namespace grpc_core {

Arena::~Arena() {
  Zone* z = last_zone_;
  while (z) {
    Zone* prev_z = z->prev;
    Destruct(z);
    gpr_free_aligned(z);
    z = prev_z;
  }
#ifdef GRPC_ARENA_TRACE_POOLED_ALLOCATIONS
  gpr_log(GPR_ERROR, "DESTRUCT_ARENA %p", this);
#endif
}

Arena* Arena::Create(size_t initial_size, MemoryAllocator* memory_allocator) {
  return new (ArenaStorage(initial_size))
      Arena(initial_size, 0, memory_allocator);
}

std::pair<Arena*, void*> Arena::CreateWithAlloc(
    size_t initial_size, size_t alloc_size, MemoryAllocator* memory_allocator) {
  static constexpr size_t base_size =
      GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(Arena));
  auto* new_arena = new (ArenaStorage(initial_size))
      Arena(initial_size, alloc_size, memory_allocator);
  void* first_alloc = reinterpret_cast<char*>(new_arena) + base_size;
  return std::make_pair(new_arena, first_alloc);
}

void Arena::DestroyManagedNewObjects() {
  ManagedNewObject* p;
  // Outer loop: clear the managed new object list.
  // We do this repeatedly in case a destructor ends up allocating something.
  while ((p = managed_new_head_.exchange(nullptr, std::memory_order_relaxed)) !=
         nullptr) {
    // Inner loop: destruct a batch of objects.
    while (p != nullptr) {
      Destruct(std::exchange(p, p->next));
    }
  }
}

void Arena::Destroy() {
  DestroyManagedNewObjects();
  memory_allocator_->Release(total_allocated_.load(std::memory_order_relaxed));
  this->~Arena();
  gpr_free_aligned(this);
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
  memory_allocator_->Reserve(alloc_size);
  total_allocated_.fetch_add(alloc_size, std::memory_order_relaxed);
  Zone* z = new (gpr_malloc_aligned(alloc_size, GPR_MAX_ALIGNMENT)) Zone();
  auto* prev = last_zone_.load(std::memory_order_relaxed);
  do {
    z->prev = prev;
  } while (!last_zone_.compare_exchange_weak(prev, z, std::memory_order_relaxed,
                                             std::memory_order_relaxed));
  return reinterpret_cast<char*>(z) + zone_base_size;
}

void Arena::ManagedNewObject::Link(std::atomic<ManagedNewObject*>* head) {
  next = head->load(std::memory_order_relaxed);
  while (!head->compare_exchange_weak(next, this, std::memory_order_acq_rel,
                                      std::memory_order_relaxed)) {
  }
}

#ifndef GRPC_ARENA_POOLED_ALLOCATIONS_USE_MALLOC
void* Arena::AllocPooled(size_t obj_size, size_t alloc_size,
                         std::atomic<FreePoolNode*>* head) {
  // ABA mitigation:
  // AllocPooled may be called by multiple threads, and to remove a node from
  // the free list we need to manipulate the next pointer, which may be done
  // differently by each thread in a naive implementation.
  // The literature contains various ways of dealing with this. Here we expect
  // to be mostly single threaded - Arena's are owned by calls and calls don't
  // do a lot of concurrent work with the pooled allocator. The place that they
  // do is allocating metadata batches for decoding HPACK headers in chttp2.
  // So we adopt an approach that is simple and fast for the single threaded
  // case, and that is also correct in the multi threaded case.

  // First, take ownership of the entire free list. At this point we know that
  // no other thread can see free nodes and will be forced to allocate.
  // We think we're mostly single threaded and so that's ok.
  FreePoolNode* p = head->exchange(nullptr, std::memory_order_acquire);
  // If there are no nodes in the free list, then go ahead and allocate from the
  // arena.
  if (p == nullptr) {
    void* r = Alloc(alloc_size);
    TracePoolAlloc(obj_size, r);
    return r;
  }
  // We had a non-empty free list... but we own the *entire* free list.
  // We only want one node, so if there are extras we'd better give them back.
  if (p->next != nullptr) {
    // We perform an exchange to do so, but if there were concurrent frees with
    // this allocation then there'll be a free list that needs to be merged with
    // ours.
    FreePoolNode* extra = head->exchange(p->next, std::memory_order_acq_rel);
    // If there was a free list concurrently created, we merge it into the
    // overall free list here by simply freeing each node in turn. This is O(n),
    // but only O(n) in the number of nodes that were freed concurrently, and
    // again: we think real world use cases are going to see this as mostly
    // single threaded.
    while (extra != nullptr) {
      FreePoolNode* next = extra->next;
      FreePooled(extra, head);
      extra = next;
    }
  }
  TracePoolAlloc(obj_size, p);
  return p;
}

void Arena::FreePooled(void* p, std::atomic<FreePoolNode*>* head) {
  // May spuriously trace a free of an already freed object - see AllocPooled
  // ABA mitigation.
  TracePoolFree(p);
  FreePoolNode* node = static_cast<FreePoolNode*>(p);
  node->next = head->load(std::memory_order_acquire);
  while (!head->compare_exchange_weak(
      node->next, node, std::memory_order_acq_rel, std::memory_order_relaxed)) {
  }
}
#endif

}  // namespace grpc_core
