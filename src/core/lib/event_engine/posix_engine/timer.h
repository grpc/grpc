/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TIMER_H
#define GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TIMER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/posix_engine/timer_heap.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/time_averaged_stats.h"

namespace grpc_event_engine {
namespace posix_engine {

struct Timer {
  int64_t deadline;
  // kInvalidHeapIndex if not in heap.
  size_t heap_index;
  bool pending;
  struct Timer* next;
  struct Timer* prev;
  experimental::EventEngine::Closure* closure;
#ifndef NDEBUG
  struct Timer* hash_table_next;
#endif

  grpc_event_engine::experimental::EventEngine::TaskHandle task_handle;
};

// Dependency injection: allow tests and/or TimerManager to inject
// their own implementations of Now, Kick.
class TimerListHost {
 public:
  // Return the current timestamp.
  // Abstracted so that tests can be run deterministically.
  virtual grpc_core::Timestamp Now() = 0;
  // Wake up a thread to check for timers.
  virtual void Kick() = 0;

 protected:
  ~TimerListHost() = default;
};

class TimerList {
 public:
  explicit TimerList(TimerListHost* host);

  TimerList(const TimerList&) = delete;
  TimerList& operator=(const TimerList&) = delete;

  /* Initialize *timer. When expired or canceled, closure will be called with
   error set to indicate if it expired (absl::OkStatus()) or was canceled
   (absl::CancelledError()). *closure is guaranteed to be called exactly once,
   and application code should check the error to determine how it was invoked.
   The application callback is also responsible for maintaining information
   about when to free up any user-level state. Behavior is undefined for a
   deadline of grpc_core::Timestamp::InfFuture(). */
  void TimerInit(Timer* timer, grpc_core::Timestamp deadline,
                 experimental::EventEngine::Closure* closure);

  /* Note that there is no timer destroy function. This is because the
     timer is a one-time occurrence with a guarantee that the callback will
     be called exactly once, either at expiration or cancellation. Thus, all
     the internal timer event management state is destroyed just before
     that callback is invoked. If the user has additional state associated with
     the timer, the user is responsible for determining when it is safe to
     destroy that state. */

  /* Cancel an *timer.
     There are three cases:
     1. We normally cancel the timer
     2. The timer has already run
     3. We can't cancel the timer because it is "in flight".

     In all of these cases, the cancellation is still considered successful.
     They are essentially distinguished in that the timer_cb will be run
     exactly once from either the cancellation (with error
     absl::CancelledError()) or from the activation (with error
     absl::OkStatus()).

     Note carefully that the callback function MAY occur in the same callstack
     as grpc_timer_cancel. It's expected that most timers will be cancelled
     (their primary use is to implement deadlines), and so this code is
     optimized such that cancellation costs as little as possible. Making
     callbacks run inline matches this aim.

     Requires: cancel() must happen after init() on a given timer */
  bool TimerCancel(Timer* timer) GRPC_MUST_USE_RESULT;

  /* iomgr internal api for dealing with timers */

  /* Check for timers to be run, and return them.
     Return nullopt if timers could not be checked due to contention with
     another thread checking.
     Return a vector of closures that *must* be run otherwise.
     If next is non-null, TRY to update *next with the next running timer
     IF that timer occurs before *next current value.
     *next is never guaranteed to be updated on any given execution; however,
     with high probability at least one thread in the system will see an update
     at any time slice. */
  absl::optional<std::vector<experimental::EventEngine::Closure*>> TimerCheck(
      grpc_core::Timestamp* next);

 private:
  /* A "timer shard". Contains a 'heap' and a 'list' of timers. All timers with
   * deadlines earlier than 'queue_deadline_cap' are maintained in the heap and
   * others are maintained in the list (unordered). This helps to keep the
   * number of elements in the heap low.
   *
   * The 'queue_deadline_cap' gets recomputed periodically based on the timer
   * stats maintained in 'stats' and the relevant timers are then moved from the
   * 'list' to 'heap'.
   */
  struct Shard {
    Shard();

    grpc_core::Timestamp ComputeMinDeadline() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu);
    bool RefillHeap(grpc_core::Timestamp now) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu);
    Timer* PopOne(grpc_core::Timestamp now) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu);
    void PopTimers(grpc_core::Timestamp now,
                   grpc_core::Timestamp* new_min_deadline,
                   std::vector<experimental::EventEngine::Closure*>* out)
        ABSL_LOCKS_EXCLUDED(mu);

    grpc_core::Mutex mu;
    grpc_core::TimeAveragedStats stats ABSL_GUARDED_BY(mu);
    /* All and only timers with deadlines < this will be in the heap. */
    grpc_core::Timestamp queue_deadline_cap ABSL_GUARDED_BY(mu);
    /* The deadline of the next timer due in this shard. */
    grpc_core::Timestamp min_deadline ABSL_GUARDED_BY(&TimerList::mu_);
    /* Index of this timer_shard in the g_shard_queue. */
    uint32_t shard_queue_index ABSL_GUARDED_BY(&TimerList::mu_);
    /* This holds all timers with deadlines < queue_deadline_cap. Timers in this
       list have the top bit of their deadline set to 0. */
    TimerHeap heap ABSL_GUARDED_BY(mu);
    /* This holds timers whose deadline is >= queue_deadline_cap. */
    Timer list ABSL_GUARDED_BY(mu);
  };

  void SwapAdjacentShardsInQueue(uint32_t first_shard_queue_index)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void NoteDeadlineChange(Shard* shard) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  std::vector<experimental::EventEngine::Closure*> FindExpiredTimers(
      grpc_core::Timestamp now, grpc_core::Timestamp* next);

  TimerListHost* const host_;
  const size_t num_shards_;
  grpc_core::Mutex mu_;
  /* The deadline of the next timer due across all timer shards */
  std::atomic<uint64_t> min_timer_;
  /* Allow only one FindExpiredTimers at once (used as a TryLock, protects no
   * fields but ensures limits on concurrency) */
  grpc_core::Mutex checker_mu_;
  /* Array of timer shards. Whenever a timer (Timer *) is added, its address
   * is hashed to select the timer shard to add the timer to */
  const std::unique_ptr<Shard[]> shards_;
  /* Maintains a sorted list of timer shards (sorted by their min_deadline, i.e
   * the deadline of the next timer in each shard). */
  const std::unique_ptr<Shard*[]> shard_queue_ ABSL_GUARDED_BY(mu_);
};

}  // namespace posix_engine
}  // namespace grpc_event_engine

#endif /* GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TIMER_H */
