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
#include <grpc/support/port_platform.h>

#include <chrono>

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/promise.h"
#include "test/core/event_engine/test_suite/event_engine_test.h"

namespace {
using namespace std::chrono_literals;

using ::grpc_event_engine::experimental::AnyInvocableClosure;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::Promise;
}  // namespace

class ExecutorTest : public EventEngineTest {};

TEST_F(ExecutorTest, RunsInvocable) {
  auto engine = this->NewEventEngine();
  Promise<bool> done{false};
  engine->Run([&] { done.Set(true); });
  ASSERT_TRUE(done.Get());
}

TEST_F(ExecutorTest, RunsClosure) {
  auto engine = this->NewEventEngine();
  Promise<bool> done{false};
  AnyInvocableClosure closure([&] { done.Set(true); });
  engine->Run(&closure);
  ASSERT_TRUE(done.Get());
}

TEST_F(ExecutorTest, WaitForPendingReturnsImmediatelyWhenIdle) {
  auto engine = this->NewEventEngine();
  auto status = engine->WaitForPendingTasks(EventEngine::Duration::zero());
  ASSERT_TRUE(status.ok());
}

TEST_F(ExecutorTest, WaitForPendingTimesOut) {
  auto engine = this->NewEventEngine();
  Promise<bool> closure_started{false};
  engine->Run([&] {
    closure_started.Set(true);
    absl::SleepFor(absl::Seconds(10));
    gpr_log(GPR_DEBUG, "DO NOT SUBMIT: cb done");
  });
  ASSERT_TRUE(closure_started.Get());
  auto status = engine->WaitForPendingTasks(10ms);
  ASSERT_TRUE(absl::IsDeadlineExceeded(status));
}

TEST_F(ExecutorTest, WaitForPendingWorks) {
  auto engine = this->NewEventEngine();
  Promise<bool> resume_closure{false};
  Promise<bool> start_waiting{false};
  engine->Run([&] {
    // tell the main thread to start its wait
    start_waiting.Set(true);
    ASSERT_TRUE(resume_closure.Get());
  });
  // wait for the closure to begin its execution
  ASSERT_TRUE(start_waiting.Get());
  // ensure a zero wait fails
  auto status = engine->WaitForPendingTasks(EventEngine::Duration::zero());
  ASSERT_FALSE(status.ok());
  ASSERT_TRUE(absl::IsDeadlineExceeded(status));
  // intruct the closure to finish and wait for it to complete
  resume_closure.Set(true);
  status = engine->WaitForPendingTasks(5s);
  ASSERT_TRUE(status.ok());
}
