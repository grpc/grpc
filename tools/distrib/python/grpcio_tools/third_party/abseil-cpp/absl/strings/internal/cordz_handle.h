// Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_STRINGS_INTERNAL_CORDZ_HANDLE_H_
#define ABSL_STRINGS_INTERNAL_CORDZ_HANDLE_H_

#include <atomic>
#include <vector>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/spinlock.h"
#include "absl/synchronization/mutex.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// This base class allows multiple types of object (CordzInfo and
// CordzSampleToken) to exist simultaneously on the delete queue (pointed to by
// global_dq_tail and traversed using dq_prev_ and dq_next_). The
// delete queue guarantees that once a profiler creates a CordzSampleToken and
// has gained visibility into a CordzInfo object, that CordzInfo object will not
// be deleted prematurely. This allows the profiler to inspect all CordzInfo
// objects that are alive without needing to hold a global lock.
class CordzHandle {
 public:
  CordzHandle() : CordzHandle(false) {}

  bool is_snapshot() const { return is_snapshot_; }

  // Returns true if this instance is safe to be deleted because it is either a
  // snapshot, which is always safe to delete, or not included in the global
  // delete queue and thus not included in any snapshot.
  // Callers are responsible for making sure this instance can not be newly
  // discovered by other threads. For example, CordzInfo instances first de-list
  // themselves from the global CordzInfo list before determining if they are
  // safe to be deleted directly.
  // If SafeToDelete returns false, callers MUST use the Delete() method to
  // safely queue CordzHandle instances for deletion.
  bool SafeToDelete() const;

  // Deletes the provided instance, or puts it on the delete queue to be deleted
  // once there are no more sample tokens (snapshot) instances potentially
  // referencing the instance. `handle` should not be null.
  static void Delete(CordzHandle* handle);

  // Returns the current entries in the delete queue in LIFO order.
  static std::vector<const CordzHandle*> DiagnosticsGetDeleteQueue();

  // Returns true if the provided handle is nullptr or guarded by this handle.
  // Since the CordzSnapshot token is itself a CordzHandle, this method will
  // allow tests to check if that token is keeping an arbitrary CordzHandle
  // alive.
  bool DiagnosticsHandleIsSafeToInspect(const CordzHandle* handle) const;

  // Returns the current entries in the delete queue, in LIFO order, that are
  // protected by this. CordzHandle objects are only placed on the delete queue
  // after CordzHandle::Delete is called with them as an argument. Only
  // CordzHandle objects that are not also CordzSnapshot objects will be
  // included in the return vector. For each of the handles in the return
  // vector, the earliest that their memory can be freed is when this
  // CordzSnapshot object is deleted.
  std::vector<const CordzHandle*> DiagnosticsGetSafeToInspectDeletedHandles();

 protected:
  explicit CordzHandle(bool is_snapshot);
  virtual ~CordzHandle();

 private:
  // Global queue data. CordzHandle stores a pointer to the global queue
  // instance to harden against ODR violations.
  struct Queue {
    constexpr explicit Queue(absl::ConstInitType)
        : mutex(absl::kConstInit,
                absl::base_internal::SCHEDULE_COOPERATIVE_AND_KERNEL) {}

    absl::base_internal::SpinLock mutex;
    std::atomic<CordzHandle*> dq_tail ABSL_GUARDED_BY(mutex){nullptr};

    // Returns true if this delete queue is empty. This method does not acquire
    // the lock, but does a 'load acquire' observation on the delete queue tail.
    // It is used inside Delete() to check for the presence of a delete queue
    // without holding the lock. The assumption is that the caller is in the
    // state of 'being deleted', and can not be newly discovered by a concurrent
    // 'being constructed' snapshot instance. Practically, this means that any
    // such discovery (`find`, 'first' or 'next', etc) must have proper 'happens
    // before / after' semantics and atomic fences.
    bool IsEmpty() const ABSL_NO_THREAD_SAFETY_ANALYSIS {
      return dq_tail.load(std::memory_order_acquire) == nullptr;
    }
  };

  void ODRCheck() const {
#ifndef NDEBUG
    ABSL_RAW_CHECK(queue_ == &global_queue_, "ODR violation in Cord");
#endif
  }

  ABSL_CONST_INIT static Queue global_queue_;
  Queue* const queue_ = &global_queue_;
  const bool is_snapshot_;

  // dq_prev_ and dq_next_ require the global queue mutex to be held.
  // Unfortunately we can't use thread annotations such that the thread safety
  // analysis understands that queue_ and global_queue_ are one and the same.
  CordzHandle* dq_prev_  = nullptr;
  CordzHandle* dq_next_ = nullptr;
};

class CordzSnapshot : public CordzHandle {
 public:
  CordzSnapshot() : CordzHandle(true) {}
};

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORDZ_HANDLE_H_
