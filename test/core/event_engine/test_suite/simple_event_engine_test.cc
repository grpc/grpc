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

#include <atomic>
#include <functional>
#include <thread>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_allocator.h>
#include <grpc/support/log.h>

#include "test/core/event_engine/test_suite/event_engine_test.h"

using ::grpc_event_engine::experimental::EndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::SliceAllocator;
using ::grpc_event_engine::experimental::SliceAllocatorFactory;

constexpr absl::Duration SLEEP_TIME = absl::Milliseconds(100);

// A simple EventEngine implementation to exercise the tests
//
// **DO NOT USE IN PRODUCTION**
// * The task map grows without bounds.
// * It is not thread-safe.
// * Eggregious locking for the sake of simplicity
class SimpleEventEngine : public grpc_event_engine::experimental::EventEngine {
 public:
  ~SimpleEventEngine() {
    shutting_down_.store(true);
    for (auto& iter : threads_) {
      iter.second.join();
    }
  }

  // TODO(hork): run async
  void Run(EventEngine::Closure* closure) override { closure->Run(); }

  // TODO(hork): run async
  void Run(std::function<void()> closure) override { closure(); }

  EventEngine::TaskHandle RunAt(absl::Time when,
                                std::function<void()> closure) override {
    // Scheduling a cb on an EventEngine that's being destroyed is UB
    if (shutting_down_.load()) abort();
    std::thread t{[=]() {
      intptr_t current_thread_id = ThreadHash(std::this_thread::get_id());
      // Poll until ready, periodically checking for cancellation and shutdown
      while (absl::Now() < when) {
        if (shutting_down_.load()) return;
        {
          grpc_core::MutexLock lock(&run_states_mu_);
          if (run_states_[current_thread_id] == RunState::kCancelled) return;
        }
        absl::SleepFor(SLEEP_TIME);
      }
      bool can_run;
      {
        grpc_core::MutexLock lock(&run_states_mu_);
        RunState prev_state = run_states_[current_thread_id];
        switch (prev_state) {
          case RunState::kNotRun:
            run_states_[current_thread_id] = RunState::kRan;
            can_run = true;
            break;
          case RunState::kCancelled:
            can_run = false;
            break;
          case RunState::kRan:
            GPR_ASSERT(
                false &&
                "Running the same closure thread twice should be impossible");
        }
      }
      if (can_run) {
        closure();
      }
    }};
    intptr_t id = ThreadHash(t.get_id());
    {
      grpc_core::MutexLock lock(&run_states_mu_);
      // Insert kNotRun if the thread has not already set it beforehand.
      run_states_.emplace(id, RunState::kNotRun);
    }
    threads_.emplace(id, std::move(t));
    return {id, -1};
  }

  bool Cancel(EventEngine::TaskHandle handle) override {
    grpc_core::MutexLock lock(&run_states_mu_);
    auto found = run_states_.find(handle.keys[0]);
    if (found == run_states_.end()) {
      // unknown task
      return false;
    }
    // Double cancellation is invalid usage.
    GPR_ASSERT(found->second != RunState::kCancelled);
    if (found->second == RunState::kNotRun) {
      run_states_[handle.keys[0]] = RunState::kCancelled;
      return true;
    }
    return false;
  }

  // Unimplemented methods

  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      /*on_accept=*/Listener::AcceptCallback,
      /*on_shutdown=*/std::function<void(absl::Status)>,
      /*config=*/const EndpointConfig&,
      /*slice_allocator_factory=*/std::unique_ptr<SliceAllocatorFactory>)
      override {
    abort();
  }

  EventEngine::TaskHandle RunAt(/*when=*/absl::Time,
                                /*closure=*/EventEngine::Closure*) override {
    abort();
  }

  absl::Status Connect(/*on_connect=*/EventEngine::OnConnectCallback,
                       /*addr=*/const EventEngine::ResolvedAddress&,
                       /*args=*/const EndpointConfig&,
                       /*slice_allocator=*/std::unique_ptr<SliceAllocator>,
                       /*deadline=*/absl::Time) override {
    abort();
  }
  std::unique_ptr<DNSResolver> GetDNSResolver() override { abort(); }
  bool IsWorkerThread() override { abort(); }

 private:
  intptr_t ThreadHash(const std::thread::id& tid) { return hasher_(tid); }

  enum class RunState { kNotRun, kCancelled, kRan };

  absl::flat_hash_map<intptr_t, std::thread> threads_;
  grpc_core::Mutex run_states_mu_;
  absl::flat_hash_map<intptr_t, RunState> run_states_
      ABSL_GUARDED_BY(run_states_mu_);
  std::atomic<bool> shutting_down_{false};
  std::hash<std::thread::id> hasher_{};
};

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  SetEventEngineFactory(
      []() { return absl::make_unique<SimpleEventEngine>(); });
  auto result = RUN_ALL_TESTS();
  return result;
}
