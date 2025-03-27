// Copyright 2022 The gRPC Authors
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

#include <benchmark/benchmark.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

#include <algorithm>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/lockfree_event.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/util/sync.h"

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::Scheduler;

namespace {
class TestScheduler : public Scheduler {
 public:
  explicit TestScheduler(std::shared_ptr<EventEngine> engine)
      : engine_(std::move(engine)) {}
  void Run(
      grpc_event_engine::experimental::EventEngine::Closure* closure) override {
    engine_->Run(closure);
  }

  void Run(absl::AnyInvocable<void()> cb) override {
    engine_->Run(std::move(cb));
  }

 private:
  std::shared_ptr<EventEngine> engine_;
};

TestScheduler* g_scheduler;

}  // namespace

namespace grpc_event_engine {
namespace experimental {

TEST(LockFreeEventTest, BasicTest) {
  LockfreeEvent event(g_scheduler);
  grpc_core::Mutex mu;
  grpc_core::CondVar cv;
  event.InitEvent();
  grpc_core::MutexLock lock(&mu);
  // Set NotifyOn first and then SetReady
  event.NotifyOn(
      PosixEngineClosure::TestOnlyToClosure([&mu, &cv](absl::Status status) {
        grpc_core::MutexLock lock(&mu);
        EXPECT_TRUE(status.ok());
        cv.Signal();
      }));
  event.SetReady();
  EXPECT_FALSE(cv.WaitWithTimeout(&mu, absl::Seconds(10)));

  // SetReady first first and then call NotifyOn
  event.SetReady();
  event.NotifyOn(
      PosixEngineClosure::TestOnlyToClosure([&mu, &cv](absl::Status status) {
        grpc_core::MutexLock lock(&mu);
        EXPECT_TRUE(status.ok());
        cv.Signal();
      }));
  EXPECT_FALSE(cv.WaitWithTimeout(&mu, absl::Seconds(10)));

  // Set NotifyOn and then call SetShutdown
  event.NotifyOn(
      PosixEngineClosure::TestOnlyToClosure([&mu, &cv](absl::Status status) {
        grpc_core::MutexLock lock(&mu);
        EXPECT_FALSE(status.ok());
        EXPECT_EQ(status, absl::CancelledError("Shutdown"));
        cv.Signal();
      }));
  event.SetShutdown(absl::CancelledError("Shutdown"));
  EXPECT_FALSE(cv.WaitWithTimeout(&mu, absl::Seconds(10)));
  event.DestroyEvent();
}

TEST(LockFreeEventTest, MultiThreadedTest) {
  std::vector<std::thread> threads;
  LockfreeEvent event(g_scheduler);
  grpc_core::Mutex mu;
  grpc_core::CondVar cv;
  bool signalled = false;
  int active = 0;
  static constexpr int kNumOperations = 100;
  threads.reserve(2);
  event.InitEvent();
  // Spin up two threads alternating between NotifyOn and SetReady
  for (int i = 0; i < 2; i++) {
    threads.emplace_back([&, thread_id = i]() {
      for (int j = 0; j < kNumOperations; j++) {
        grpc_core::MutexLock lock(&mu);
        // Wait for both threads to process the previous operation before
        // starting the next one.
        while (signalled) {
          cv.Wait(&mu);
        }
        active++;
        if (thread_id == 0) {
          event.NotifyOn(PosixEngineClosure::TestOnlyToClosure(
              [&mu, &cv, &signalled](absl::Status status) {
                grpc_core::MutexLock lock(&mu);
                EXPECT_TRUE(status.ok());
                signalled = true;
                cv.SignalAll();
              }));
        } else {
          event.SetReady();
        }
        while (!signalled) {
          cv.Wait(&mu);
        }
        // The last thread to finish the current operation sets signalled to
        // false and wakes up the other thread if its blocked waiting to
        // start the next operation.
        if (--active == 0) {
          signalled = false;
          cv.Signal();
        }
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  event.SetShutdown(absl::OkStatus());
  event.DestroyEvent();
}

namespace {

// A trivial callback sceduler which inherits from the Scheduler interface but
// immediatey runs the callback/closure.
class BenchmarkCallbackScheduler : public Scheduler {
 public:
  BenchmarkCallbackScheduler() = default;
  void Run(
      grpc_event_engine::experimental::EventEngine::Closure* closure) override {
    closure->Run();
  }

  void Run(absl::AnyInvocable<void()> cb) override { cb(); }
};

// A benchmark which repeatedly registers a NotifyOn callback and invokes the
// callback with SetReady. This benchmark is intended to measure the cost of
// NotifyOn and SetReady implementations of the lock free event.
void BM_LockFreeEvent(benchmark::State& state) {
  BenchmarkCallbackScheduler cb_scheduler;
  LockfreeEvent event(&cb_scheduler);
  event.InitEvent();
  PosixEngineClosure* notify_on_closure =
      PosixEngineClosure::ToPermanentClosure([](absl::Status /*status*/) {});
  for (auto s : state) {
    event.NotifyOn(notify_on_closure);
    event.SetReady();
  }
  event.SetShutdown(absl::CancelledError("Shutting down"));
  delete notify_on_closure;
  event.DestroyEvent();
}
BENCHMARK(BM_LockFreeEvent)->ThreadRange(1, 64);

}  // namespace

}  // namespace experimental
}  // namespace grpc_event_engine

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // TODO(ctiller): EventEngine temporarily needs grpc to be initialized first
  // until we clear out the iomgr shutdown code.
  grpc_init();
  g_scheduler = new TestScheduler(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  int r = RUN_ALL_TESTS();
  benchmark::RunTheBenchmarksNamespaced();
  grpc_shutdown();
  return r;
}
