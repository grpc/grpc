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

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.h"

#include <inttypes.h>

#include <atomic>
#include <memory>
#include <utility>

#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpc/support/log.h>

#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/thread_local.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/work_queue/basic_work_queue.h"
#include "src/core/lib/event_engine/work_queue/work_queue.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/gprpp/time.h"

// ## Thread Pool Fork-handling
//
// Thread-safety needs special attention with regard to fork() calls. The
// Forkable system employs a pre- and post- fork callback system that does not
// guarantee any ordering of execution. On fork() events, the thread pool does
// the following:
//
// On pre-fork:
// * the WorkStealingThreadPool triggers all threads to exit,
// * all queued work is saved, and
// * all threads will are down, including the Lifeguard thread.
//
// On post-fork:
//  * all threads are restarted, including the Lifeguard thread, and
//  * all previously-saved work is enqueued for execution.
//
// However, the queue may may get into trouble if one thread is attempting to
// restart the thread pool while another thread is shutting it down. For that
// reason, Quiesce and Start must be thread-safe, and Quiesce must wait for the
// pool to be in a fully started state before it is allowed to continue.
// Consider this potential ordering of events between Start and Quiesce:
//
//     ┌──────────┐
//     │ Thread 1 │
//     └────┬─────┘  ┌──────────┐
//          │        │ Thread 2 │
//          ▼        └────┬─────┘
//        Start()         │
//          │             ▼
//          │        Quiesce()
//          │        Wait for worker threads to exit
//          │        Wait for the lifeguard thread to exit
//          ▼
//        Start the Lifeguard thread
//        Start the worker threads
//
// Thread 2 will find no worker threads, and it will then want to wait on a
// non-existent Lifeguard thread to finish. Trying a simple
// `lifeguard_thread_.Join()` leads to memory access errors. This implementation
// uses Notifications to coordinate startup and shutdown states.

namespace grpc_event_engine {
namespace experimental {

namespace {
// Maximum amount of time an extra thread is allowed to idle before being
// reclaimed.
constexpr grpc_core::Duration kIdleThreadLimit =
    grpc_core::Duration::Seconds(20);
// Rate at which "Waiting for ..." logs should be printed while quiescing.
constexpr size_t kBlockingQuiesceLogRateSeconds = 3;
// Minumum time between thread creations.
constexpr grpc_core::Duration kTimeBetweenThrottledThreadStarts =
    grpc_core::Duration::Seconds(1);
// Minimum time a worker thread should sleep between checking for new work. Used
// in backoff calculations to reduce vigilance when the pool is calm.
constexpr grpc_core::Duration kWorkerThreadMinSleepBetweenChecks{
    grpc_core::Duration::Milliseconds(15)};
// Maximum time a worker thread should sleep between checking for new work.
constexpr grpc_core::Duration kWorkerThreadMaxSleepBetweenChecks{
    grpc_core::Duration::Seconds(3)};
// Minimum time the lifeguard thread should sleep between checks. Used in
// backoff calculations to reduce vigilance when the pool is calm.
constexpr grpc_core::Duration kLifeguardMinSleepBetweenChecks{
    grpc_core::Duration::Milliseconds(15)};
// Maximum time the lifeguard thread should sleep between checking for new work.
constexpr grpc_core::Duration kLifeguardMaxSleepBetweenChecks{
    grpc_core::Duration::Seconds(1)};
}  // namespace

thread_local WorkQueue* g_local_queue = nullptr;

// -------- WorkStealingThreadPool --------

WorkStealingThreadPool::WorkStealingThreadPool(size_t reserve_threads)
    : pool_{std::make_shared<WorkStealingThreadPoolImpl>(reserve_threads)} {
  pool_->Start();
}

void WorkStealingThreadPool::Quiesce() { pool_->Quiesce(); }

WorkStealingThreadPool::~WorkStealingThreadPool() {
  GPR_ASSERT(pool_->IsQuiesced());
}

void WorkStealingThreadPool::Run(absl::AnyInvocable<void()> callback) {
  Run(SelfDeletingClosure::Create(std::move(callback)));
}

void WorkStealingThreadPool::Run(EventEngine::Closure* closure) {
  pool_->Run(closure);
}

// -------- WorkStealingThreadPool::TheftRegistry --------

void WorkStealingThreadPool::TheftRegistry::Enroll(WorkQueue* queue) {
  grpc_core::MutexLock lock(&mu_);
  queues_.emplace(queue);
}

void WorkStealingThreadPool::TheftRegistry::Unenroll(WorkQueue* queue) {
  grpc_core::MutexLock lock(&mu_);
  queues_.erase(queue);
}

EventEngine::Closure* WorkStealingThreadPool::TheftRegistry::StealOne() {
  grpc_core::MutexLock lock(&mu_);
  EventEngine::Closure* closure;
  for (auto* queue : queues_) {
    closure = queue->PopMostRecent();
    if (closure != nullptr) return closure;
  }
  return nullptr;
}

void WorkStealingThreadPool::PrepareFork() { pool_->PrepareFork(); }

void WorkStealingThreadPool::PostforkParent() { pool_->Postfork(); }

void WorkStealingThreadPool::PostforkChild() { pool_->Postfork(); }

// -------- WorkStealingThreadPool::WorkStealingThreadPoolImpl --------

WorkStealingThreadPool::WorkStealingThreadPoolImpl::WorkStealingThreadPoolImpl(
    size_t reserve_threads)
    : reserve_threads_(reserve_threads), lifeguard_(this) {}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Start() {
  for (size_t i = 0; i < reserve_threads_; i++) {
    StartThread();
  }
  lifeguard_.Start();
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Run(
    EventEngine::Closure* closure) {
  GPR_DEBUG_ASSERT(quiesced_.load(std::memory_order_relaxed) == false);
  if (g_local_queue != nullptr) {
    g_local_queue->Add(closure);
  } else {
    queue_.Add(closure);
  }
  // Signal a worker in any case, even if work was added to a local queue. This
  // improves performance on 32-core streaming benchmarks with small payloads.
  work_signal_.Signal();
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::StartThread() {
  last_started_thread_.store(
      grpc_core::Timestamp::Now().milliseconds_after_process_epoch(),
      std::memory_order_relaxed);
  grpc_core::Thread(
      "event_engine",
      [](void* arg) {
        ThreadState* worker = static_cast<ThreadState*>(arg);
        worker->ThreadBody();
        delete worker;
      },
      new ThreadState(shared_from_this()), nullptr,
      grpc_core::Thread::Options().set_tracked(false).set_joinable(false))
      .Start();
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Quiesce() {
  SetShutdown(true);
  // Wait until all threads have exited.
  // Note that if this is a threadpool thread then we won't exit this thread
  // until all other threads have exited, so we need to wait for just one thread
  // running instead of zero.
  bool is_threadpool_thread = g_local_queue != nullptr;
  work_signal()->SignalAll();
  living_thread_count_.BlockUntilThreadCount(is_threadpool_thread ? 1 : 0,
                                             "shutting down");
  GPR_ASSERT(queue_.Empty());
  quiesced_.store(true, std::memory_order_relaxed);
  lifeguard_.BlockUntilShutdownAndReset();
}

bool WorkStealingThreadPool::WorkStealingThreadPoolImpl::SetThrottled(
    bool throttled) {
  return throttled_.exchange(throttled, std::memory_order_relaxed);
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::SetShutdown(
    bool is_shutdown) {
  auto was_shutdown = shutdown_.exchange(is_shutdown);
  GPR_ASSERT(is_shutdown != was_shutdown);
  work_signal_.SignalAll();
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::SetForking(
    bool is_forking) {
  auto was_forking = forking_.exchange(is_forking);
  GPR_ASSERT(is_forking != was_forking);
}

bool WorkStealingThreadPool::WorkStealingThreadPoolImpl::IsForking() {
  return forking_.load(std::memory_order_relaxed);
}

bool WorkStealingThreadPool::WorkStealingThreadPoolImpl::IsShutdown() {
  return shutdown_.load(std::memory_order_relaxed);
}

bool WorkStealingThreadPool::WorkStealingThreadPoolImpl::IsQuiesced() {
  return quiesced_.load(std::memory_order_relaxed);
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::PrepareFork() {
  SetForking(true);
  work_signal_.SignalAll();
  living_thread_count_.BlockUntilThreadCount(0, "forking");
  lifeguard_.BlockUntilShutdownAndReset();
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Postfork() {
  SetForking(false);
  Start();
}

// -------- WorkStealingThreadPool::WorkStealingThreadPoolImpl::Lifeguard -----

WorkStealingThreadPool::WorkStealingThreadPoolImpl::Lifeguard::Lifeguard(
    WorkStealingThreadPoolImpl* pool)
    : pool_(pool),
      backoff_(grpc_core::BackOff::Options()
                   .set_initial_backoff(kLifeguardMinSleepBetweenChecks)
                   .set_max_backoff(kLifeguardMaxSleepBetweenChecks)
                   .set_multiplier(1.3)),
      lifeguard_should_shut_down_(std::make_unique<grpc_core::Notification>()),
      lifeguard_is_shut_down_(std::make_unique<grpc_core::Notification>()) {}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Lifeguard::Start() {
  // lifeguard_running_ is set early to avoid a quiesce race while the
  // lifeguard is still starting up.
  lifeguard_running_.store(true);
  grpc_core::Thread(
      "lifeguard",
      [](void* arg) {
        auto* lifeguard = static_cast<Lifeguard*>(arg);
        lifeguard->LifeguardMain();
      },
      this, nullptr,
      grpc_core::Thread::Options().set_tracked(false).set_joinable(false))
      .Start();
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Lifeguard::
    LifeguardMain() {
  while (true) {
    if (pool_->IsForking()) break;
    // If the pool is shut down, loop quickly until quiesced. Otherwise,
    // reduce the check rate if the pool is idle.
    if (pool_->IsShutdown()) {
      if (pool_->IsQuiesced()) break;
    } else {
      lifeguard_should_shut_down_->WaitForNotificationWithTimeout(
          absl::Milliseconds(
              (backoff_.NextAttemptTime() - grpc_core::Timestamp::Now())
                  .millis()));
    }
    MaybeStartNewThread();
  }
  lifeguard_running_.store(false, std::memory_order_relaxed);
  lifeguard_is_shut_down_->Notify();
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Lifeguard::
    BlockUntilShutdownAndReset() {
  lifeguard_should_shut_down_->Notify();
  while (lifeguard_running_.load(std::memory_order_relaxed)) {
    GRPC_LOG_EVERY_N_SEC_DELAYED(kBlockingQuiesceLogRateSeconds, GPR_DEBUG,
                                 "%s",
                                 "Waiting for lifeguard thread to shut down");
    lifeguard_is_shut_down_->WaitForNotification();
  }
  // Do an additional wait in case this method races with LifeguardMain's
  // shutdown. This should return immediately if the lifeguard is already shut
  // down.
  lifeguard_is_shut_down_->WaitForNotification();
  backoff_.Reset();
  lifeguard_should_shut_down_ = std::make_unique<grpc_core::Notification>();
  lifeguard_is_shut_down_ = std::make_unique<grpc_core::Notification>();
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Lifeguard::
    MaybeStartNewThread() {
  // No new threads are started when forking.
  // No new work is done when forking needs to begin.
  if (pool_->forking_.load()) return;
  const auto living_thread_count = pool_->living_thread_count()->count();
  // Wake an idle worker thread if there's global work to be had.
  if (pool_->busy_thread_count()->count() < living_thread_count) {
    if (!pool_->queue_.Empty()) {
      pool_->work_signal()->Signal();
      backoff_.Reset();
    }
    // Idle threads will eventually wake up for an attempt at work stealing.
    return;
  }
  // No new threads if in the throttled state.
  // However, all workers are busy, so the Lifeguard should be more
  // vigilant about checking whether a new thread must be started.
  if (grpc_core::Timestamp::Now() -
          grpc_core::Timestamp::FromMillisecondsAfterProcessEpoch(
              pool_->last_started_thread_) <
      kTimeBetweenThrottledThreadStarts) {
    backoff_.Reset();
    return;
  }
  // All workers are busy and the pool is not throttled. Start a new thread.
  // TODO(hork): new threads may spawn when there is no work in the global
  // queue, nor any work to steal. Add more sophisticated logic about when to
  // start a thread.
  GRPC_EVENT_ENGINE_TRACE(
      "Starting new ThreadPool thread due to backlog (total threads: %" PRIuPTR
      ")",
      living_thread_count + 1);
  pool_->StartThread();
  // Tell the lifeguard to monitor the pool more closely.
  backoff_.Reset();
}

// -------- WorkStealingThreadPool::ThreadState --------

WorkStealingThreadPool::ThreadState::ThreadState(
    std::shared_ptr<WorkStealingThreadPoolImpl> pool)
    : pool_(std::move(pool)),
      auto_thread_counter_(
          pool_->living_thread_count()->MakeAutoThreadCounter()),
      backoff_(grpc_core::BackOff::Options()
                   .set_initial_backoff(kWorkerThreadMinSleepBetweenChecks)
                   .set_max_backoff(kWorkerThreadMaxSleepBetweenChecks)
                   .set_multiplier(1.3)),
      busy_count_idx_(pool_->busy_thread_count()->NextIndex()) {}

void WorkStealingThreadPool::ThreadState::ThreadBody() {
  g_local_queue = new BasicWorkQueue();
  pool_->theft_registry()->Enroll(g_local_queue);
  ThreadLocal::SetIsEventEngineThread(true);
  while (Step()) {
    // loop until the thread should no longer run
  }
  // cleanup
  if (pool_->IsForking()) {
    // TODO(hork): consider WorkQueue::AddAll(WorkQueue*)
    EventEngine::Closure* closure;
    while (!g_local_queue->Empty()) {
      closure = g_local_queue->PopMostRecent();
      if (closure != nullptr) {
        pool_->queue()->Add(closure);
      }
    }
  } else if (pool_->IsShutdown()) {
    FinishDraining();
  }
  GPR_ASSERT(g_local_queue->Empty());
  pool_->theft_registry()->Unenroll(g_local_queue);
  delete g_local_queue;
}

void WorkStealingThreadPool::ThreadState::SleepIfRunning() {
  if (pool_->IsForking()) return;
  absl::SleepFor(
      absl::Milliseconds(kTimeBetweenThrottledThreadStarts.millis()));
}

bool WorkStealingThreadPool::ThreadState::Step() {
  if (pool_->IsForking()) return false;
  auto* closure = g_local_queue->PopMostRecent();
  // If local work is available, run it.
  if (closure != nullptr) {
    auto busy =
        pool_->busy_thread_count()->MakeAutoThreadCounter(busy_count_idx_);
    closure->Run();
    return true;
  }
  // Thread shutdown exit condition (ignoring fork). All must be true:
  // * shutdown was called
  // * the local queue is empty
  // * the global queue is empty
  // * the steal pool returns nullptr
  bool should_run_again = false;
  grpc_core::Timestamp start_time{grpc_core::Timestamp::Now()};
  // Wait until work is available or until shut down.
  while (!pool_->IsForking()) {
    // Pull from the global queue next
    // TODO(hork): consider an empty check for performance wins. Depends on the
    // queue implementation, the BasicWorkQueue takes two locks when you do an
    // empty check then pop.
    closure = pool_->queue()->PopMostRecent();
    if (closure != nullptr) {
      should_run_again = true;
      break;
    };
    // Try stealing if the queue is empty
    closure = pool_->theft_registry()->StealOne();
    if (closure != nullptr) {
      should_run_again = true;
      break;
    }
    // No closures were retrieved from anywhere.
    // Quit the thread if the pool has been shut down.
    if (pool_->IsShutdown()) break;
    bool timed_out = pool_->work_signal()->WaitWithTimeout(
        backoff_.NextAttemptTime() - grpc_core::Timestamp::Now());
    if (pool_->IsForking() || pool_->IsShutdown()) break;
    // Quit a thread if the pool has more than it requires, and this thread
    // has been idle long enough.
    if (timed_out &&
        pool_->living_thread_count()->count() > pool_->reserve_threads() &&
        grpc_core::Timestamp::Now() - start_time > kIdleThreadLimit) {
      return false;
    }
  }
  if (pool_->IsForking()) {
    // save the closure since we aren't going to execute it.
    if (closure != nullptr) g_local_queue->Add(closure);
    return false;
  }
  if (closure != nullptr) {
    auto busy =
        pool_->busy_thread_count()->MakeAutoThreadCounter(busy_count_idx_);
    closure->Run();
  }
  backoff_.Reset();
  return should_run_again;
}

void WorkStealingThreadPool::ThreadState::FinishDraining() {
  // The thread is definitionally busy while draining
  auto busy =
      pool_->busy_thread_count()->MakeAutoThreadCounter(busy_count_idx_);
  // If a fork occurs at any point during shutdown, quit draining. The post-fork
  // threads will finish draining the global queue.
  while (!pool_->IsForking()) {
    if (!g_local_queue->Empty()) {
      auto* closure = g_local_queue->PopMostRecent();
      if (closure != nullptr) {
        closure->Run();
      }
      continue;
    }
    if (!pool_->queue()->Empty()) {
      auto* closure = pool_->queue()->PopMostRecent();
      if (closure != nullptr) {
        closure->Run();
      }
      continue;
    }
    break;
  }
}

// -------- WorkStealingThreadPool::WorkSignal --------

void WorkStealingThreadPool::WorkSignal::Signal() {
  grpc_core::MutexLock lock(&mu_);
  cv_.Signal();
}

void WorkStealingThreadPool::WorkSignal::SignalAll() {
  grpc_core::MutexLock lock(&mu_);
  cv_.SignalAll();
}

bool WorkStealingThreadPool::WorkSignal::WaitWithTimeout(
    grpc_core::Duration time) {
  grpc_core::MutexLock lock(&mu_);
  return cv_.WaitWithTimeout(&mu_, absl::Milliseconds(time.millis()));
}

}  // namespace experimental
}  // namespace grpc_event_engine
