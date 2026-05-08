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

#include "src/core/lib/resource_quota/arena.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <new>

#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/alloc.h"
#include "absl/log/log.h"

// Forward declarations.

struct census_context;
namespace grpc_event_engine::experimental{
  class EventEngine;
}

namespace grpc_core {
  class BackendMetricProvider;
  class Call;
  class CallSpan;
  class CallTracer;
  class SecurityContext;
  class ServiceConfigCallData;
  class SubchannelCallTrackerInterface;

  struct TelemetryLabel;
  struct V3InterceptorToV2State;

  // Test-only, see arena_test.cc
  struct Foo;
}
namespace grpc_core::channelz {
  class CallNode;
}
namespace grpc_core::lb_policy_detail {
class SubchannelCallTrackerInterface;
}

namespace grpc_core {

namespace arena_detail {

// Explicit instantiation
template <typename T>
uint16_t ArenaContextTraits<T>::id() {
    static NoDestruct<uint16_t> lazy_id{BaseArenaContextTraits::MakeId()};
    return *lazy_id;
}

}

namespace {

void* ArenaStorage(size_t& initial_size) {
  size_t base_size = Arena::ArenaOverhead() +
                     GPR_ROUND_UP_TO_ALIGNMENT_SIZE(
                         arena_detail::BaseArenaContextTraits::ContextSize());
  initial_size =
      std::max(GPR_ROUND_UP_TO_ALIGNMENT_SIZE(initial_size), base_size);
  static constexpr size_t alignment =
      (GPR_CACHELINE_SIZE > GPR_MAX_ALIGNMENT &&
       GPR_CACHELINE_SIZE % GPR_MAX_ALIGNMENT == 0)
          ? GPR_CACHELINE_SIZE
          : GPR_MAX_ALIGNMENT;
  return gpr_malloc_aligned(initial_size, alignment);
}

}  // namespace

Arena::~Arena() {
  for (size_t i = 0; i < arena_detail::BaseArenaContextTraits::NumContexts();
       ++i) {
    arena_detail::BaseArenaContextTraits::Destroy(i, contexts()[i]);
  }
  DestroyManagedNewObjects();
  arena_factory_->FinalizeArena(this);
  arena_factory_->allocator().Release(
      total_allocated_.load(std::memory_order_relaxed));
  Zone* z = last_zone_;
  while (z) {
    Zone* prev_z = z->prev;
    Destruct(z);
    gpr_free_aligned(z);
    z = prev_z;
  }
}

RefCountedPtr<Arena> Arena::Create(size_t initial_size,
                                   RefCountedPtr<ArenaFactory> arena_factory) {
  void* p = ArenaStorage(initial_size);
  return RefCountedPtr<Arena>(
      new (p) Arena(initial_size, std::move(arena_factory)));
}

Arena::Arena(size_t initial_size, RefCountedPtr<ArenaFactory> arena_factory)
    : initial_zone_size_(initial_size),
      total_used_(ArenaOverhead() +
                  GPR_ROUND_UP_TO_ALIGNMENT_SIZE(
                      arena_detail::BaseArenaContextTraits::ContextSize())),
      arena_factory_(std::move(arena_factory)) {
  for (size_t i = 0; i < arena_detail::BaseArenaContextTraits::NumContexts();
       ++i) {
    contexts()[i] = nullptr;
  }
  CHECK_GE(initial_size, arena_detail::BaseArenaContextTraits::ContextSize());
  arena_factory_->allocator().Reserve(initial_size);
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

void Arena::Destroy() const {
  this->~Arena();
  gpr_free_aligned(const_cast<Arena*>(this));
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
  arena_factory_->allocator().Reserve(alloc_size);
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

MemoryAllocator DefaultMemoryAllocatorForSimpleArenaAllocator() {
  return ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
      "simple-arena-allocator");
}

RefCountedPtr<ArenaFactory> SimpleArenaAllocator(size_t initial_size,
                                                 MemoryAllocator allocator) {
  class Allocator : public ArenaFactory {
   public:
    Allocator(size_t initial_size, MemoryAllocator allocator)
        : ArenaFactory(std::move(allocator)), initial_size_(initial_size) {}

    RefCountedPtr<Arena> MakeArena() override {
      return Arena::Create(initial_size_, Ref());
    }

    void FinalizeArena(Arena*) override {
      // No-op.
    }

   private:
    size_t initial_size_;
  };
  return MakeRefCounted<Allocator>(initial_size, std::move(allocator));
}

namespace arena_detail {

template class ArenaContextTraits<struct census_context>;
template class ArenaContextTraits<grpc_event_engine::experimental::EventEngine>;
template class ArenaContextTraits<grpc_core::BackendMetricProvider>;
template class ArenaContextTraits<grpc_core::Call>;
template class ArenaContextTraits<grpc_core::CallSpan>;
template class ArenaContextTraits<grpc_core::CallTracer>;
template class ArenaContextTraits<grpc_core::SecurityContext>;
template class ArenaContextTraits<grpc_core::ServiceConfigCallData>;
template class ArenaContextTraits<grpc_core::TelemetryLabel>;
template class ArenaContextTraits<grpc_core::V3InterceptorToV2State>;
template class ArenaContextTraits<grpc_core::channelz::CallNode>;
template class ArenaContextTraits<grpc_core::lb_policy_detail::SubchannelCallTrackerInterface>;

template class ArenaContextTraits<grpc_core::Foo>;

struct ArenaContextTraitsInitializer {
  ArenaContextTraitsInitializer() {
    // Force initialization and allocation of ids.
    ArenaContextTraits<struct census_context>::id();
    ArenaContextTraits<grpc_event_engine::experimental::EventEngine>::id();
    ArenaContextTraits<grpc_core::BackendMetricProvider>::id();
    ArenaContextTraits<grpc_core::Call>::id();
    ArenaContextTraits<grpc_core::CallSpan>::id();
    ArenaContextTraits<grpc_core::CallTracer>::id();
    ArenaContextTraits<grpc_core::SecurityContext>::id();
    ArenaContextTraits<grpc_core::ServiceConfigCallData>::id();
    ArenaContextTraits<grpc_core::TelemetryLabel>::id();
    ArenaContextTraits<grpc_core::V3InterceptorToV2State>::id();
    ArenaContextTraits<grpc_core::channelz::CallNode>::id();
    ArenaContextTraits<grpc_core::lb_policy_detail::SubchannelCallTrackerInterface>::id();
    ArenaContextTraits<grpc_core::Foo>::id();
  }
};

// Must be at the bottom of this file (because MSVC init_seg() affect all subsequent
// objects).
namespace {
#if defined(__GNUC__) || defined(__clang__) 
__attribute__((init_priority(101))) ArenaContextTraitsInitializer context_traits_initializer;
#elif defined(_MSC_VER)
#pragma init_seg(lib)
ArenaContextTraitsInitializer context_traits_initializer;
#else
ArenaContextTraitsInitializer context_traits_initializer;
#endif
} // namespace

} // namespace arena_detail

}  // namespace grpc_core
