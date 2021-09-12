// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_LIB_RESOURCE_QUOTA_RESOURCE_QUOTA_H
#define GRPC_CORE_LIB_RESOURCE_QUOTA_RESOURCE_QUOTA_H

#include <grpc/support/port_platform.h>

namespace grpc_core {

class Reclaimer;
class MemoryAllocator;
class MemoryAllocatorFactory;
class ThreadAllocator;

// Reclamation passes.
// When memory is tight, we start trying to claim some back from memory
// reclaimers. We do this in multiple passes: if there is a less destructive
// operation available, we do that, otherwise we do something more destructive.
enum class ReclamationPass {
  // Benign reclamation is intended for reclamation steps that are not
  // observable outside of gRPC (besides maybe causing an increase in CPU
  // usage).
  // Examples of such reclamation would be resizing buffers to fit the current
  // load needs, rather than whatever was the peak usage requirement.
  kBenign,
  // Idle reclamation is intended for reclamation steps that are observable
  // outside of gRPC, but do not cause application work to be lost.
  // Examples of such reclamation would be dropping channels that are not being
  // used.
  kIdle,
  // Destructive reclamation is our last resort, and is these reclamations are
  // allowed to drop work - such as cancelling in flight requests.
  kDestructive,
};

// Reservation request - how much memory do we want to allocate?
class MemoryRequest {
 public:
  // Request a fixed amount of memory.
  // NOLINTNEXTLINE(runtime/explicit)
  MemoryRequest(size_t n) : min_(n), max_(n) {}
  // Request a range of memory.
  MemoryRequest(size_t min, size_t max) : min_(std::min(min, max)), max_(max) {}

  // Set the block size for allocations.
  // This allows us to ensure some granularity of allocations - say enough for
  // one element of an array.
  MemoryRequest WithBlockSize(size_t block_size) const;

  size_t min() const { return min_; }
  size_t max() const { return max_; }
  size_t block_size() const { return block_size_; }

 private:
  size_t min_;
  size_t max_;
  size_t block_size_ = 1;
  Intent intent_ = Intent::kDefault;
};

class Reclaimer final : public InternallyRefCounted<Reclaimer> {
 public:
  class Token {
   public:
    Token(const Token&) = delete;
    Token& operator=(const Token&) = delete;
    Token(Token&&) = default;
    Token& operator=(Token&&) = default;

    ~Token();

   private:
    Token(RefCountedPtr<MemoryQuota> memory_quota, uint64_t id);

    RefCountedPtr<MemoryQuota> memory_quota_;
    uint64_t id_;
  };

  using ReclaimFunction = std::function<void(Token)>;
  void Arm(ReclaimFunction fn);

 private:
#ifndef NDEBUG
  std::atomic<bool> armed_{false};
#endif
  const ReclamationPass pass_;
  RefCountedPtr<MemoryQuota> memory_quota_;
};

using ReclaimerPtr = OrphanablePtr<Reclaimer>;

// MemoryAllocator grants the owner the ability to allocate memory from an
// underlying resource quota.
class MemoryAllocator final : public InternallyRefCounted<MemoryAllocator> {
 public:
  explicit MemoryAllocator(MemoryQuotaPtr memory_quota);
  ~MemoryAllocator();

  // Rebind -  Swaps the underlying quota for this allocator, taking care to
  // make sure memory allocated is moved to allocations against the new quota.
  void Rebind(MemoryQuotaPtr memory_quota)
      ABSL_LOCKS_EXCLUDED(memory_quota_mu_);

  // Reserve bytes from the quota.
  // If we enter overcommit, reclamation will begin concurrently.
  // Returns the number of bytes reserved.
  size_t Reserve(Request request) ABSL_LOCKS_EXCLUDED(memory_quota_mu_);

  // Release some bytes that were previously reserved.
  void Release(size_t n) ABSL_LOCKS_EXCLUDED(memory_quota_mu_) {
    // Add the released memory to our free bytes counter... if this increases
    // from  0 to non-zero, then we have more to do, otherwise, we're actually
    // done.
    if (free_bytes_.fetch_add(size, std::memory_order_release) != 0) return;
    MaybeRegisterReclaimer();
  }

  // Allocate a new object of type T, with constructor arguments.
  // The returned type is wrapped, and upon destruction the reserved memory will
  // be released to the allocator automatically.
  // As such, T must have a virtual destructor so we can insert the necessary
  // hook.
  template <typename T, typename... Args>
  absl::enable_if_t<std::has_virtual_destructor<T>, T*> New(Args&&... args)
      ABSL_LOCKS_EXCLUDED(memory_quota_mu_) {
    // Wrap T such that when it's destroyed, we can release memory back to the
    // allocator.
    class Wrapper final : public T {
     public:
      template <typename... Args>
      Wrapper(RefCountedPtr<MemoryAllocator> allocator, Args&&... args)
          : allocator_(std::move(allocator)), T(std::forward<Args>(args)...) {}
      ~Wrapper() override { allocator_->Release(sizeof(*this)); }

     private:
      const RefCountedPtr<MemoryAllocator> allocator_;
    };
    Reserve(sizeof(Wrapper));
    return new Wrapper(Ref(), std::forward<Args>(args)...);
  }

  // Construct a unique ptr immediately.
  template <typename T, typename... Args>
  std::unique_ptr<T> MakeUnique(Args&&... args)
      ABSL_LOCKS_EXCLUDED(memory_quota_mu_) {
    return std::unique_ptr<T>(New<T>(std::forward<Args>(args)...));
  }

 private:
  void Orphan() override;

  // Primitive reservation function.
  ReserveResult TryReserve(Request request) GRPC_MUST_USE_RESULT;
  // Replenish at least n bytes from the quota, without blocking, possibly
  // entering overcommit.
  void Replenish(size_t n) ABSL_LOCKS_EXCLUDED(memory_quota_mu_);
  // If we have not already, register a reclamation function against the quota
  // to sweep any free memory back to that quota.
  void MaybeRegisterReclaimer() ABSL_LOCKS_EXCLUDED(memory_quota_mu_);
  void MaybeRegisterReclaimerLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(memory_quota_mu_);

  // Amount of memory this allocator has cached for its own use: to avoid quota
  // contention, each MemoryAllocator can keep some memory in addition to what
  // it is immediately using, and the quota can pull it back under memory
  // pressure.
  std::atomic<size_t> free_bytes_{0};
  // Mutex guarding the backing resource quota.
  absl::Mutex memory_quota_mu_;
  // Backing resource quota.
  RefCountedPtr<MemoryQuota> memory_quota_ ABSL_GUARDED_BY(memory_quota_mu_);
  // Amount of memory taken from the quota by this allocator.
  size_t taken_bytes_ ABSL_GUARDED_BY(memory_quota_mu_) = 0;
  // Have we armed the non-empty reclaimer?
  // We do this whenever we haven't and free_bytes_ > 0.
  bool reclaimer_armed_ ABSL_GUARDED_BY(memory_quota_mu_) = false;
};

// MemoryQuota tracks the amount of memory available as part of a ResourceQuota.
class MemoryQuota final : public InternallyRefCounted<MemoryQuota> {
 public:
  MemoryQuota();

  MemoryAllocatorPtr MakeMemoryAllocator() {
    return MakeOrphanable<MemoryAllocator>(Ref());
  }

  // Resize the quota to new_size.
  void SetSize(size_t new_size);

 private:
  friend class MemoryAllocator;

  void Orphan();

  // Forcefully take some memory from the quota, potentially entering
  // overcommit.
  void Take(size_t n);
  // Finish reclamation pass.
  void FinishReclamation(uint64_t token);
  // Return some memory to the quota.
  void Return(size_t amount);
  // Given a pass, return the pipe.
  UnboundedThreadsafePipe<Reclaimer::ReclaimFunction>* GetPipe(
      ReclamationPass pass) {
    switch (pass) {
      case ReclamationPass::kBenign:
        return &benign_reclaimers_;
      case ReclamationPass::kIdle:
        return &idle_reclaimers_;
      case ReclamationPass::kDestructive:
        return &destructive_reclaimers_;
    }
    GPR_UNREACHABLE_CODE(return nullptr);
  }

  // The amount of memory that's free in this quota.
  std::atomic<ssize_t> free_bytes_{0};

  // Reclaimer queues.
  UnboundedThreadsafePipe<Reclaimer::ReclaimFunction> non_empty_reclaimers_;
  UnboundedThreadsafePipe<Reclaimer::ReclaimFunction> benign_reclaimers_;
  UnboundedThreadsafePipe<Reclaimer::ReclaimFunction> idle_reclaimers_;
  UnboundedThreadsafePipe<Reclaimer::ReclaimFunction> destructive_reclaimers_;
  // The reclaimer activity consumes reclaimers whenever we are in overcommit to
  // try and get back under memory limits.
  ActivityPtr reclaimer_activity_;
  // A barrier that blocks the reclaimer activity whilst a reclamation is in
  // progress.
  AtomicBarrier barrier_;
  // The total number of bytes in this quota.
  std::atomic<size_t> quota_size_{0};
};

}  // namespace grpc_core

#endif
