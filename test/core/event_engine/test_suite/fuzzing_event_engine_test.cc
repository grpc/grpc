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
#include <thread>

#include <gtest/gtest.h>

#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/event_engine/test_suite/event_engine_test_framework.h"
#include "test/core/event_engine/test_suite/tests/timer_test.h"

namespace grpc_event_engine {
namespace experimental {
namespace {

class ThreadedFuzzingEventEngine : public FuzzingEventEngine {
 public:
  ThreadedFuzzingEventEngine()
      : FuzzingEventEngine(
            []() {
              Options options;
              return options;
            }(),
            fuzzing_event_engine::Actions()),
        main_([this]() {
          while (!done_.load()) {
            auto tick_start = absl::Now();
            while (absl::Now() - tick_start < absl::Milliseconds(10)) {
              absl::SleepFor(absl::Milliseconds(1));
            }
            Tick();
          }
        }) {}

  ~ThreadedFuzzingEventEngine() override {
    done_.store(true);
    main_.join();
  }

 private:
  std::atomic<bool> done_{false};
  std::thread main_;
};

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
