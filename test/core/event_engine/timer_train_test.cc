//
//
// Copyright 2024 gRPC authors.
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

#include "src/core/lib/event_engine/timer_train.h"

#include <benchmark/benchmark.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/time.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/util/time.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"

using testing::Mock;
using testing::StrictMock;

namespace grpc_event_engine {
namespace experimental {

namespace {

class MockClosure : public experimental::EventEngine::Closure {
 public:
  MOCK_METHOD(void, Run, ());
};

absl::AnyInvocable<void()> ToAnyInvocable(MockClosure* closure) {
  return [closure]() { closure->Run(); };
}

class SimulatedHost : public TimerListHost {
 public:
  explicit SimulatedHost(FuzzingEventEngine* engine) : engine_(engine) {};
  grpc_core::Timestamp Now() override {
    int64_t now_ns = engine_->Now().time_since_epoch().count();
    return grpc_core::Timestamp::FromMillisecondsAfterProcessEpoch(now_ns /
                                                                   1000000) +
           grpc_core::Duration::NanosecondsRoundUp(now_ns % 1000000);
  }
  void Kick() override {};

 private:
  FuzzingEventEngine* engine_;
};

}  // namespace

TEST(TimerTrainTest, AddCancelAndExtend) {
  std::shared_ptr<FuzzingEventEngine> engine =
      std::make_shared<FuzzingEventEngine>(FuzzingEventEngine::Options(),
                                           fuzzing_event_engine::Actions());
  std::unique_ptr<TimerListHost> host =
      std::make_unique<SimulatedHost>(engine.get());
  grpc_core::Duration kTrainPeriod = grpc_core::Duration::Minutes(1);
  grpc_core::Duration kEpsilon = grpc_core::Duration::Milliseconds(1);
  TimerTrain::Options options;
  options.period = kTrainPeriod;
  options.num_shards = 5;
  options.event_engine = engine;
  TimerTrain timer_train(std::move(host), options);
  StrictMock<MockClosure> closures[5];
  timer_train.RunAfter(std::chrono::seconds(10), ToAnyInvocable(&closures[0]));
  timer_train.RunAfter(std::chrono::minutes(1), ToAnyInvocable(&closures[1]));
  timer_train.RunAfter(std::chrono::minutes(2), ToAnyInvocable(&closures[2]));
  auto handle3 = timer_train.RunAfter(std::chrono::minutes(3),
                                      ToAnyInvocable(&closures[3]));
  auto handle4 = timer_train.RunAfter(std::chrono::minutes(3),
                                      ToAnyInvocable(&closures[4]));
  EXPECT_CALL(closures[0], Run());
  EXPECT_CALL(closures[1], Run());
  // Need to tick for a duration slightly larger than the train period because
  // each train's step will further enqueue closures onto the event engine and
  // those need to run as well.
  engine->TickForDuration(kTrainPeriod + kEpsilon);
  Mock::VerifyAndClearExpectations(&closures[0]);
  Mock::VerifyAndClearExpectations(&closures[1]);

  EXPECT_CALL(closures[2], Run());
  engine->TickForDuration(kTrainPeriod + kEpsilon);
  Mock::VerifyAndClearExpectations(&closures[2]);

  EXPECT_TRUE(timer_train.Cancel(handle3));
  EXPECT_TRUE(timer_train.Extend(handle4, std::chrono::minutes(1)));
  engine->TickForDuration(kTrainPeriod + kEpsilon);

  EXPECT_CALL(closures[4], Run());
  engine->TickForDuration(kTrainPeriod + kEpsilon);
  Mock::VerifyAndClearExpectations(&closures[4]);
}

TEST(TimerTrainTest, AddDeleteTrain) {
  std::shared_ptr<FuzzingEventEngine> engine =
      std::make_shared<FuzzingEventEngine>(FuzzingEventEngine::Options(),
                                           fuzzing_event_engine::Actions());
  std::unique_ptr<TimerListHost> host =
      std::make_unique<SimulatedHost>(engine.get());
  grpc_core::Duration kTrainPeriod = grpc_core::Duration::Minutes(1);
  StrictMock<MockClosure> closures[3];
  // Add expectations that the closures are not run.
  EXPECT_CALL(closures[0], Run()).Times(0);
  EXPECT_CALL(closures[1], Run()).Times(0);
  EXPECT_CALL(closures[2], Run()).Times(0);
  {
    TimerTrain::Options options;
    options.period = kTrainPeriod;
    options.num_shards = 5;
    options.event_engine = engine;
    TimerTrain timer_train(std::move(host), options);
    timer_train.RunAfter(std::chrono::seconds(10),
                         ToAnyInvocable(&closures[0]));
    timer_train.RunAfter(std::chrono::minutes(1), ToAnyInvocable(&closures[1]));
    timer_train.RunAfter(std::chrono::minutes(2), ToAnyInvocable(&closures[2]));
    engine->TickForDuration(grpc_core::Duration::Seconds(30));
  }
  Mock::VerifyAndClearExpectations(&closures[0]);
  Mock::VerifyAndClearExpectations(&closures[1]);
  Mock::VerifyAndClearExpectations(&closures[2]);
}

void BM_EventEngineCancelReschedule(benchmark::State& state) {
  std::shared_ptr<EventEngine> engine = GetDefaultEventEngine();
  constexpr int kNumTimers = 1000;
  EventEngine::TaskHandle handle[kNumTimers];
  for (auto _ : state) {
    for (int i = 0; i < kNumTimers; ++i) {
      handle[i] = engine->RunAfter(std::chrono::seconds(100), []() {});
    }
    for (int i = 0; i < kNumTimers; ++i) {
      engine->Cancel(handle[i]);
    }
  }
}
BENCHMARK(BM_EventEngineCancelReschedule);

void BM_TimerTrainExtend(benchmark::State& state) {
  grpc_core::Duration kTrainPeriod = grpc_core::Duration::Minutes(1);
  std::shared_ptr<EventEngine> engine = GetDefaultEventEngine();
  constexpr int kNumTimers = 1000;
  TimerTrain::Options options;
  options.period = kTrainPeriod;
  options.num_shards = 32;
  options.event_engine = engine;
  TimerTrain timer_train(options);
  EventEngine::TaskHandle handle[kNumTimers];
  for (int i = 0; i < kNumTimers; ++i) {
    handle[i] = timer_train.RunAfter(std::chrono::seconds(100), []() {});
  }
  for (auto _ : state) {
    for (int i = 0; i < kNumTimers; ++i) {
      timer_train.Extend(handle[i], std::chrono::seconds(100));
    }
  }
}
BENCHMARK(BM_TimerTrainExtend);

}  // namespace experimental
}  // namespace grpc_event_engine

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  {
    grpc_init();
    benchmark::RunTheBenchmarksNamespaced();
    grpc_shutdown();
  }
  return RUN_ALL_TESTS();
}