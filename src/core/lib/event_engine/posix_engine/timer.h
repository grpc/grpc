//
//
// Copyright 2015 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TIMER_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TIMER_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"
#include "src/core/lib/event_engine/posix_engine/timer_heap.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/util/time_averaged_stats.h"

namespace grpc_event_engine {
namespace experimental {

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

  virtual ~TimerListHost() = default;
};

class TimerListInterface {
 public:
  virtual ~TimerListInterface() = default;
  virtual void TimerInit(Timer* timer, grpc_core::Timestamp deadline,
                         experimental::EventEngine::Closure* closure) = 0;
  GRPC_MUST_USE_RESULT virtual bool TimerCancel(Timer* timer) = 0;
  GRPC_MUST_USE_RESULT virtual bool TimerExtend(Timer* timer,
                                                grpc_core::Duration delay) = 0;
  virtual absl::optional<std::vector<experimental::EventEngine::Closure*>>
  TimerCheck(grpc_core::Timestamp* next) = 0;
};

class TimerList : public TimerListInterface {
 public:
  explicit TimerList(TimerListHost* host);

  TimerList(const TimerList&) = delete;
  TimerList& operator=(const TimerList&) = delete;

  // Initialize a Timer.
  // When expired, the closure will be run. If the timer is canceled, the
  // closure will not be run. Behavior is undefined for a deadline of
  // grpc_core::Timestamp::InfFuture().
  void TimerInit(Timer* timer, grpc_core::Timestamp deadline,
                 experimental::EventEngine::Closure* closure) override;

  // Cancel a Timer.
  // Returns false if the timer cannot be canceled. This will happen if the
  // timer has already fired, or if its closure is currently running. The
  // closure is guaranteed to run eventually if this method returns false.
  // Otherwise, this returns true, and the closure will not be run.
  GRPC_MUST_USE_RESULT bool TimerCancel(Timer* timer) override;

  GRPC_MUST_USE_RESULT bool TimerExtend(Timer* timer,
                                        grpc_core::Duration delay) override;

  // Check for timers to be run, and return them.
  // Return nullopt if timers could not be checked due to contention with
  // another thread checking.
  // Return a vector of closures that *must* be run otherwise.
  // If next is non-null, TRY to update *next with the next running timer
  // IF that timer occurs before *next current value.
  // *next is never guaranteed to be updated on any given execution; however,
  // with high probability at least one thread in the system will see an update
  // at any time slice.
  absl::optional<std::vector<experimental::EventEngine::Closure*>> TimerCheck(
      grpc_core::Timestamp* next) override;

 private:
  // A "timer shard". Contains a 'heap' and a 'list' of timers. All timers with
  // deadlines earlier than 'queue_deadline_cap' are maintained in the heap and
  // others are maintained in the list (unordered). This helps to keep the
  // number of elements in the heap low.
  //
  // The 'queue_deadline_cap' gets recomputed periodically based on the timer
  // stats maintained in 'stats' and the relevant timers are then moved from the
  // 'list' to 'heap'.
  //
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
    // All and only timers with deadlines < this will be in the heap.
    grpc_core::Timestamp queue_deadline_cap ABSL_GUARDED_BY(mu);
    // The deadline of the next timer due in this shard.
    grpc_core::Timestamp min_deadline ABSL_GUARDED_BY(&TimerList::mu_);
    // Index of this timer_shard in the g_shard_queue.
    uint32_t shard_queue_index ABSL_GUARDED_BY(&TimerList::mu_);
    // This holds all timers with deadlines < queue_deadline_cap. Timers in this
    // list have the top bit of their deadline set to 0.
    TimerHeap heap ABSL_GUARDED_BY(mu);
    // This holds timers whose deadline is >= queue_deadline_cap.
    Timer list ABSL_GUARDED_BY(mu);
  };

  void TimerInitInternalOnShard(Timer* timer, Shard* shard,
                                grpc_core::Timestamp deadline);
  void SwapAdjacentShardsInQueue(uint32_t first_shard_queue_index)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void NoteDeadlineChange(Shard* shard) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  std::vector<experimental::EventEngine::Closure*> FindExpiredTimers(
      grpc_core::Timestamp now, grpc_core::Timestamp* next);

  TimerListHost* const host_;
  const size_t num_shards_;
  grpc_core::Mutex mu_;
  // The deadline of the next timer due across all timer shards
  std::atomic<uint64_t> min_timer_;
  // Allow only one FindExpiredTimers at once (used as a TryLock, protects no
  // fields but ensures limits on concurrency)
  grpc_core::Mutex checker_mu_;
  // Array of timer shards. Whenever a timer (Timer *) is added, its address
  // is hashed to select the timer shard to add the timer to
  const std::unique_ptr<Shard[]> shards_;
  // Maintains a sorted list of timer shards (sorted by their min_deadline, i.e
  // the deadline of the next timer in each shard).
  const std::unique_ptr<Shard*[]> shard_queue_ ABSL_GUARDED_BY(mu_);
};

// A "slacked" timer list.
//
// This timer list is similar to the TimerList above, but it uses a different
// data structure to store timers.
//
// The SlackedTimerList can be used to efficiently manage timers that can accept
// some slack in their deadline. When enqueuing a timer, the timer's deadline
// is rounded to the nearest resolution boundary.
//
// When checking for expired timers, the SlackedTimerList will return all timers
// that are expired as of the current time (the resolution boundary).
//
// The SlackedTimerList is more efficient than the TimerList when there is a
// large number of timers that can accept some slack in their deadline.
//
// The SlackedTimerList is thread-safe.
//
class SlackedTimerList : public TimerListInterface {
 public:
  struct Options {
    int num_shards;
    grpc_core::Duration resolution;
  };
  SlackedTimerList(TimerListHost* host, Options options);
  void TimerInit(Timer* timer, grpc_core::Timestamp deadline,
                 experimental::EventEngine::Closure* closure) override;
  GRPC_MUST_USE_RESULT bool TimerCancel(Timer* timer) override;
  GRPC_MUST_USE_RESULT bool TimerExtend(Timer* timer,
                                        grpc_core::Duration delay) override;
  absl::optional<std::vector<experimental::EventEngine::Closure*>> TimerCheck(
      grpc_core::Timestamp* next) override;

 private:
  struct Shard {
    ~Shard();
    grpc_core::Mutex mu;
    TimerHeap active_tick_idxes ABSL_GUARDED_BY(mu);
    absl::flat_hash_map<uint64_t, Timer*> timers ABSL_GUARDED_BY(&mu);
  };

  void TimerInitInternalOnShardLocked(Timer* timer, Shard* shard)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&shard->mu);

  uint64_t ComputeTickIndex(grpc_core::Timestamp deadline);

  void NoteTimerRemovalLocked(Shard* shard, uint64_t tick_idx)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&shard->mu);

  TimerListHost* host_;
  Options options_;
  std::unique_ptr<Shard[]> shards_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TIMER_H
