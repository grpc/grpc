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

#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "grpc/event_engine/event_engine.h"
#include "grpc/event_engine/slice_buffer.h"

#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/event_engine/test_suite/event_engine_test_framework.h"
#include "test/core/event_engine/test_suite/tests/timer_test.h"

using namespace std::chrono_literals;

namespace grpc_event_engine {
namespace experimental {
namespace {

class ThreadedFuzzingEventEngine : public FuzzingEventEngine {
 public:
  ThreadedFuzzingEventEngine()
      : FuzzingEventEngine(
            []() {
              Options options;
              options.final_tick_length = std::chrono::milliseconds(10);
              return options;
            }(),
            fuzzing_event_engine::Actions()),
        main_([this]() { RunWorkerLoop(); }) {}

  void Pause() {
    done_.store(true);
    main_.join();
  }

  void Resume() {
    done_.store(false);
    main_ = std::thread([this]() { RunWorkerLoop(); });
  }

  void RunWorkerLoop() {
    while (!done_.load()) {
      auto tick_start = absl::Now();
      while (absl::Now() - tick_start < absl::Milliseconds(10)) {
        absl::SleepFor(absl::Milliseconds(1));
      }
      Tick();
    }
  }

  ~ThreadedFuzzingEventEngine() override {
    done_.store(true);
    main_.join();
  }

 private:
  std::atomic<bool> done_{false};
  std::thread main_;
};

TEST_F(EventEngineTest, FuzzingEventEngineMockEndpointTest) {
  // Create a mock endpoint which is expected to read specified bytes at
  // specified times and verify that the read operations succeed and fail when
  // they should.
  SliceBuffer read_buf;
  std::shared_ptr<ThreadedFuzzingEventEngine> fuzzing_engine =
      std::dynamic_pointer_cast<ThreadedFuzzingEventEngine>(
          this->NewEventEngine());
  // Stop the previous worker loop because we want fine grained control over
  // timing for this test.
  fuzzing_engine->Pause();
  MockEndpointActions actions;

  // This concatenated block should be read after 10ms
  actions.emplace_back(10ms, "abc");
  actions.emplace_back(0ms, "def");

  // This concatenated block should be read after 20ms
  actions.emplace_back(10ms, "ghi");
  actions.emplace_back(0ms, "jkl");

  // This block should be read after 40ms
  actions.emplace_back(20ms, "blah blah blah ");
  actions.emplace_back(0ms, "go go go");

  auto endpoint = fuzzing_engine->CreateMockEndpoint(actions);
  endpoint->Read([](absl::Status status) { EXPECT_TRUE(status.ok()); },
                 &read_buf, nullptr);
  // Advance by 10ms
  fuzzing_engine->Tick();
  EXPECT_EQ(ExtractSliceBufferIntoString(&read_buf), "abcdef");

  endpoint->Read([](absl::Status status) { EXPECT_TRUE(status.ok()); },
                 &read_buf, nullptr);
  // Advance by 20ms
  fuzzing_engine->Tick();
  EXPECT_EQ(ExtractSliceBufferIntoString(&read_buf), "ghijkl");

  endpoint->Read([](absl::Status status) { EXPECT_TRUE(status.ok()); },
                 &read_buf, nullptr);
  // Advance by 30ms
  fuzzing_engine->Tick();
  // The callback should not have executed after 30ms.
  EXPECT_EQ(read_buf.Length(), 0);

  // Advance by 40ms
  fuzzing_engine->Tick();
  // The callback should have executed after 40ms.
  EXPECT_EQ(ExtractSliceBufferIntoString(&read_buf), "blah blah blah go go go");

  // There is nothing to read now. The following endpoint Read should fail with
  // non OK status.
  endpoint->Read([](absl::Status status) { EXPECT_FALSE(status.ok()); },
                 &read_buf, nullptr);
  fuzzing_engine->TickUntilIdle();
  fuzzing_engine->Resume();
}

}  // namespace
}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine> engine =
      std::make_shared<
          grpc_event_engine::experimental::ThreadedFuzzingEventEngine>();
  SetEventEngineFactories([engine]() { return engine; },
                          [engine]() { return engine; });
  grpc_event_engine::experimental::InitTimerTests();
  return RUN_ALL_TESTS();
}
