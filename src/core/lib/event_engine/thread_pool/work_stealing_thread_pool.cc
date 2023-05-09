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

namespace grpc_event_engine {
namespace experimental {

namespace {
constexpr grpc_core::Duration kIdleThreadLimit =
    grpc_core::Duration::Seconds(20);
constexpr grpc_core::Duration kTimeBetweenThrottledThreadStarts =
    grpc_core::Duration::Seconds(1);
constexpr grpc_core::Duration kWorkerThreadMinSleepBetweenChecks{
    grpc_core::Duration::Milliseconds(33)};
constexpr grpc_core::Duration kWorkerThreadMaxSleepBetweenChecks{
    grpc_core::Duration::Seconds(3)};
constexpr grpc_core::Duration kLifeguardMinSleepBetweenChecks{
    grpc_core::Duration::Milliseconds(50)};
constexpr grpc_core::Duration kLifeguardMaxSleepBetweenChecks{
    grpc_core::Duration::Seconds(1)};
constexpr absl::Duration kSleepBetweenQuiesceCheck{absl::Milliseconds(10)};
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
    : reserve_threads_(reserve_threads), lifeguard_() {}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Start() {
  lifeguard_.Start(shared_from_this());
  for (size_t i = 0; i < reserve_threads_; i++) {
    StartThread();
  }
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Run(
    EventEngine::Closure* closure) {
  GPR_DEBUG_ASSERT(quiesced_.load(std::memory_order_relaxed) == false);
  if (g_local_queue != nullptr) {
    g_local_queue->Add(closure);
    return;
  }
  queue_.Add(closure);
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
  thread_count()->BlockUntilThreadCount(CounterType::kLivingThreadCount,
                                        is_threadpool_thread ? 1 : 0,
                                        "shutting down", work_signal());
  GPR_ASSERT(queue_.Empty());
  quiesced_.store(true, std::memory_order_relaxed);
  lifeguard_.BlockUntilShutdown();
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
  thread_count()->BlockUntilThreadCount(CounterType::kLivingThreadCount, 0,
                                        "forking", &work_signal_);
  lifeguard_.BlockUntilShutdown();
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Postfork() {
  SetForking(false);
  Start();
}

// -------- WorkStealingThreadPool::WorkStealingThreadPoolImpl::Lifeguard
// --------

WorkStealingThreadPool::WorkStealingThreadPoolImpl::Lifeguard::Lifeguard()
    : backoff_(grpc_core::BackOff::Options()
                   .set_initial_backoff(kLifeguardMinSleepBetweenChecks)
                   .set_max_backoff(kLifeguardMaxSleepBetweenChecks)
                   .set_multiplier(1.3)) {}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Lifeguard::Start(
    std::shared_ptr<WorkStealingThreadPoolImpl> pool) {
  pool_ = std::move(pool);
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
  thread_running_.store(true);
  while (true) {
    absl::SleepFor(absl::Milliseconds(
        (backoff_.NextAttemptTime() - grpc_core::Timestamp::Now()).millis()));
    if (pool_->IsForking()) break;
    if (pool_->IsShutdown() && pool_->IsQuiesced()) break;
    MaybeStartNewThread();
  }
  thread_running_.store(false);
  pool_.reset();
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Lifeguard::
    BlockUntilShutdown() {
  while (thread_running_.load()) {
    absl::SleepFor(kSleepBetweenQuiesceCheck);
  }
}

void WorkStealingThreadPool::WorkStealingThreadPoolImpl::Lifeguard::
    MaybeStartNewThread() {
  // No new threads are started when forking.
  // No new work is done when forking needs to begin.
  if (pool_->forking_.load()) return;
  int busy_thread_count =
      pool_->thread_count_.GetCount(CounterType::kBusyCount);
  int living_thread_count =
      pool_->thread_count_.GetCount(CounterType::kLivingThreadCount);
  // Wake an idle worker thread if there's global work to be had.
  if (busy_thread_count < living_thread_count) {
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
      "Starting new ThreadPool thread due to backlog (total threads: %d)",
      living_thread_count + 1);
  pool_->StartThread();
  // Tell the lifeguard to monitor the pool more closely.
  backoff_.Reset();
}

// -------- WorkStealingThreadPool::ThreadState --------

WorkStealingThreadPool::ThreadState::ThreadState(
    std::shared_ptr<WorkStealingThreadPoolImpl> pool)
    : pool_(std::move(pool)),
      auto_thread_count_(pool_->thread_count(),
                         CounterType::kLivingThreadCount),
      backoff_(grpc_core::BackOff::Options()
                   .set_initial_backoff(kWorkerThreadMinSleepBetweenChecks)
                   .set_max_backoff(kWorkerThreadMaxSleepBetweenChecks)
                   .set_multiplier(1.3)) {}

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
    ThreadCount::AutoThreadCount auto_busy{pool_->thread_count(),
                                           CounterType::kBusyCount};
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
    // Quit a thread if the pool has more than it requires, and this thread
    // has been idle long enough.
    if (timed_out &&
        pool_->thread_count()->GetCount(CounterType::kLivingThreadCount) >
            pool_->reserve_threads() &&
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
    ThreadCount::AutoThreadCount auto_busy{pool_->thread_count(),
                                           CounterType::kBusyCount};
    closure->Run();
  }
  backoff_.Reset();
  return should_run_again;
}

// -------- WorkStealingThreadPool::ThreadCount --------

void WorkStealingThreadPool::ThreadCount::Add(CounterType counter_type) {
  thread_counts_[counter_type].fetch_add(1, std::memory_order_relaxed);
}

void WorkStealingThreadPool::ThreadCount::Remove(CounterType counter_type) {
  thread_counts_[counter_type].fetch_sub(1, std::memory_order_relaxed);
}

void WorkStealingThreadPool::ThreadCount::BlockUntilThreadCount(
    CounterType counter_type, int desired_threads, const char* why,
    WorkSignal* work_signal) {
  auto& counter = thread_counts_[counter_type];
  int curr_threads = counter.load(std::memory_order_relaxed);
  // Wait for all threads to exit.
  auto last_log_time = grpc_core::Timestamp::Now();
  while (curr_threads > desired_threads) {
    absl::SleepFor(kSleepBetweenQuiesceCheck);
    work_signal->SignalAll();
    if (grpc_core::Timestamp::Now() - last_log_time >
        grpc_core::Duration::Seconds(3)) {
      gpr_log(GPR_DEBUG,
              "Waiting for thread pool to idle before %s. (%d to %d)", why,
              curr_threads, desired_threads);
      last_log_time = grpc_core::Timestamp::Now();
    }
    curr_threads = counter.load(std::memory_order_relaxed);
  }
}

size_t WorkStealingThreadPool::ThreadCount::GetCount(CounterType counter_type) {
  return thread_counts_[counter_type].load(std::memory_order_relaxed);
}

WorkStealingThreadPool::ThreadCount::AutoThreadCount::AutoThreadCount(
    ThreadCount* counter, CounterType counter_type)
    : counter_(counter), counter_type_(counter_type) {
  counter_->Add(counter_type_);
}

WorkStealingThreadPool::ThreadCount::AutoThreadCount::~AutoThreadCount() {
  counter_->Remove(counter_type_);
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
