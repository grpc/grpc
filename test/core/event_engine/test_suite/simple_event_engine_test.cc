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
#include <unordered_map>

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

// A simple EventEngine implementation, NOT TO BE USED IN PRODUCTION.
// The task map grows without bounds.
// It is not thread-safe.
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

  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept,
      std::function<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<SliceAllocatorFactory> slice_allocator_factory) override {
    abort();
  }

  EventEngine::TaskHandle RunAt(absl::Time when,
                                EventEngine::Closure* closure) override {
    abort();
  }

  EventEngine::TaskHandle RunAt(absl::Time when,
                                std::function<void()> closure) override {
    // Scheduling a cb on an EventEngine that's being destroyed is UB
    if (shutting_down_.load()) abort();
    std::thread t{[=]() {
      intptr_t current_thread_id = ThreadHash(std::this_thread::get_id());
      while (absl::Now() < when) {
        if (shutting_down_.load()) return;
        absl::SleepFor(SLEEP_TIME);
      }
      bool can_run;
      {
        grpc_core::MutexLock lock(&run_states_mu_);
        auto emplaced = run_states_.emplace(current_thread_id, RunState::kRan);
        bool already_existed = !emplaced.second;
        can_run = !already_existed;
        if (already_existed) {
          switch (emplaced.first->second) {
            case RunState::kNotRun:
              run_states_[current_thread_id] = RunState::kRan;
              can_run = true;
              break;
            case RunState::kCancelled:
              can_run = false;
          }
        }
      }
      if (can_run) {
        std::cerr << absl::StrFormat(
            "DO NOT SUBMIT - Running closure on thd %d. State: %d\n",
            current_thread_id, run_states_[current_thread_id]);
        closure();
      } else {
        std::cerr << "DO NOT SUBMIT - closure cancelled\n";
      }
    }};
    intptr_t id = ThreadHash(t.get_id());
    {
      grpc_core::MutexLock lock(&run_states_mu_);
      // Insert kNotRun if the thread has not already set it.
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
      std::cerr << absl::StrFormat("DO NOT SUBMIT - CAN cancel thd %d\n",
                                   handle.keys[0]);
      run_states_[handle.keys[0]] = RunState::kCancelled;
      return true;
    }
    std::cerr << absl::StrFormat("DO NOT SUBMIT - cannot cancel thd %d\n",
                                 handle.keys[0]);
    return false;
  }

  absl::Status Connect(EventEngine::OnConnectCallback on_connect,
                       const EventEngine::ResolvedAddress& addr,
                       const EndpointConfig& args,
                       std::unique_ptr<SliceAllocator> slice_allocator,
                       absl::Time deadline) override {
    return absl::OkStatus();
  }

  std::unique_ptr<DNSResolver> GetDNSResolver() override { return nullptr; }

  bool IsWorkerThread() override { return false; }

 private:
  intptr_t ThreadHash(const std::thread::id& tid) { return hasher_(tid); }

  enum class RunState { kNotRun, kCancelled, kRan };

  std::unordered_map<intptr_t, std::thread> threads_;
  grpc_core::Mutex run_states_mu_;
  // Using an unordered_map since std::atomic does not have the required
  // constructors to be used in absl::flat_hash_map.
  std::unordered_map<intptr_t, RunState> run_states_
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
