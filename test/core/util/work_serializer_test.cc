//
//
// Copyright 2019 gRPC authors.
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

#include "src/core/util/work_serializer.h"

#include <grpc/grpc.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <stddef.h>

#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/synchronization/barrier.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/telemetry/histogram_view.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/notification.h"
#include "src/core/util/thd.h"
#include "src/core/util/wait_for_single_owner.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/test_util/test_config.h"

using grpc_event_engine::experimental::GetDefaultEventEngine;

namespace grpc_core {
namespace {
TEST(WorkSerializerTest, NoOp) {
  auto lock = std::make_unique<WorkSerializer>(GetDefaultEventEngine());
  lock.reset();
  WaitForSingleOwner(GetDefaultEventEngine());
}

TEST(WorkSerializerTest, ExecuteOneRun) {
  auto lock = std::make_unique<WorkSerializer>(GetDefaultEventEngine());
  gpr_event done;
  gpr_event_init(&done);
  lock->Run([&done]() { gpr_event_set(&done, reinterpret_cast<void*>(1)); });
  EXPECT_TRUE(gpr_event_wait(&done, grpc_timeout_seconds_to_deadline(5)) !=
              nullptr);
  lock.reset();
  WaitForSingleOwner(GetDefaultEventEngine());
}

TEST(WorkSerializerTest, ExecuteOneScheduleAndDrain) {
  auto lock = std::make_unique<WorkSerializer>(GetDefaultEventEngine());
  gpr_event done;
  gpr_event_init(&done);
  lock->Run(
      [&done]() {
        EXPECT_EQ(gpr_event_get(&done), nullptr);
        gpr_event_set(&done, reinterpret_cast<void*>(1));
      },
      DEBUG_LOCATION);
  EXPECT_TRUE(gpr_event_wait(&done, grpc_timeout_seconds_to_deadline(5)) !=
              nullptr);
  lock.reset();
  WaitForSingleOwner(GetDefaultEventEngine());
}

class TestThread {
 public:
  explicit TestThread(WorkSerializer* lock)
      : lock_(lock), thread_("grpc_execute_many", ExecuteManyLoop, this) {
    gpr_event_init(&done_);
    thread_.Start();
  }

  ~TestThread() {
    EXPECT_NE(gpr_event_wait(&done_, gpr_inf_future(GPR_CLOCK_REALTIME)),
              nullptr);
    thread_.Join();
  }

 private:
  static void ExecuteManyLoop(void* arg) {
    TestThread* self = static_cast<TestThread*>(arg);
    size_t n = 1;
    for (size_t i = 0; i < 10; i++) {
      for (size_t j = 0; j < 10000; j++) {
        struct ExecutionArgs {
          size_t* counter;
          size_t value;
        };
        ExecutionArgs* c = new ExecutionArgs;
        c->counter = &self->counter_;
        c->value = n++;
        self->lock_->Run(
            [c]() {
              EXPECT_TRUE(*c->counter == c->value - 1);
              *c->counter = c->value;
              delete c;
            },
            DEBUG_LOCATION);
      }
      // sleep for a little bit, to test other threads picking up the load
      gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
    }
    self->lock_->Run(
        [self]() { gpr_event_set(&self->done_, reinterpret_cast<void*>(1)); },
        DEBUG_LOCATION);
  }

  WorkSerializer* lock_ = nullptr;
  Thread thread_;
  size_t counter_ = 0;
  gpr_event done_;
};

TEST(WorkSerializerTest, ExecuteMany) {
  auto lock = std::make_unique<WorkSerializer>(GetDefaultEventEngine());
  {
    std::vector<std::unique_ptr<TestThread>> threads;
    for (size_t i = 0; i < 10; ++i) {
      threads.push_back(std::make_unique<TestThread>(lock.get()));
    }
  }
  lock.reset();
  WaitForSingleOwner(GetDefaultEventEngine());
}

class TestThreadScheduleAndDrain {
 public:
  explicit TestThreadScheduleAndDrain(WorkSerializer* lock)
      : lock_(lock), thread_("grpc_execute_many", ExecuteManyLoop, this) {
    gpr_event_init(&done_);
    thread_.Start();
  }

  ~TestThreadScheduleAndDrain() {
    EXPECT_NE(gpr_event_wait(&done_, gpr_inf_future(GPR_CLOCK_REALTIME)),
              nullptr);
    thread_.Join();
  }

 private:
  static void ExecuteManyLoop(void* arg) {
    TestThreadScheduleAndDrain* self =
        static_cast<TestThreadScheduleAndDrain*>(arg);
    size_t n = 1;
    for (size_t i = 0; i < 10; i++) {
      for (size_t j = 0; j < 10000; j++) {
        struct ExecutionArgs {
          size_t* counter;
          size_t value;
        };
        ExecutionArgs* c = new ExecutionArgs;
        c->counter = &self->counter_;
        c->value = n++;
        self->lock_->Run(
            [c]() {
              EXPECT_TRUE(*c->counter == c->value - 1);
              *c->counter = c->value;
              delete c;
            },
            DEBUG_LOCATION);
      }
      // sleep for a little bit, to test other threads picking up the load
      gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
    }
    self->lock_->Run(
        [self]() { gpr_event_set(&self->done_, reinterpret_cast<void*>(1)); },
        DEBUG_LOCATION);
  }

  WorkSerializer* lock_ = nullptr;
  Thread thread_;
  size_t counter_ = 0;
  gpr_event done_;
};

TEST(WorkSerializerTest, ExecuteManyScheduleAndDrain) {
  auto lock = std::make_unique<WorkSerializer>(GetDefaultEventEngine());
  {
    std::vector<std::unique_ptr<TestThreadScheduleAndDrain>> threads;
    for (size_t i = 0; i < 10; ++i) {
      threads.push_back(
          std::make_unique<TestThreadScheduleAndDrain>(lock.get()));
    }
  }
  lock.reset();
  WaitForSingleOwner(GetDefaultEventEngine());
}

TEST(WorkSerializerTest, ExecuteManyMixedRunScheduleAndDrain) {
  auto lock = std::make_unique<WorkSerializer>(GetDefaultEventEngine());
  {
    std::vector<std::unique_ptr<TestThread>> run_threads;
    std::vector<std::unique_ptr<TestThreadScheduleAndDrain>> schedule_threads;
    for (size_t i = 0; i < 10; ++i) {
      run_threads.push_back(std::make_unique<TestThread>(lock.get()));
      schedule_threads.push_back(
          std::make_unique<TestThreadScheduleAndDrain>(lock.get()));
    }
  }
  lock.reset();
  WaitForSingleOwner(GetDefaultEventEngine());
}

// Tests that work serializers allow destruction from the last callback
TEST(WorkSerializerTest, CallbackDestroysWorkSerializer) {
  auto lock = std::make_shared<WorkSerializer>(GetDefaultEventEngine());
  lock->Run([&]() { lock.reset(); });
  WaitForSingleOwner(GetDefaultEventEngine());
}

// Tests additional racy conditions when the last callback triggers work
// serializer destruction.
TEST(WorkSerializerTest, WorkSerializerDestructionRace) {
  for (int i = 0; i < 1000; ++i) {
    auto lock = std::make_shared<WorkSerializer>(GetDefaultEventEngine());
    Notification notification;
    std::thread t1([&]() {
      notification.WaitForNotification();
      lock.reset();
    });
    lock->Run([&]() { notification.Notify(); });
    t1.join();
  }
  WaitForSingleOwner(GetDefaultEventEngine());
}

// Tests racy conditions when the last callback triggers work
// serializer destruction.
TEST(WorkSerializerTest, WorkSerializerDestructionRaceMultipleThreads) {
  auto lock = std::make_shared<WorkSerializer>(GetDefaultEventEngine());
  absl::Barrier barrier(11);
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([lock, &barrier]() mutable {
      barrier.Block();
      lock->Run([lock]() mutable { lock.reset(); });
    });
  }
  barrier.Block();
  lock.reset();
  for (auto& thread : threads) {
    thread.join();
  }
  WaitForSingleOwner(GetDefaultEventEngine());
}

TEST(WorkSerializerTest, MetricsWork) {
  auto serializer = std::make_unique<WorkSerializer>(GetDefaultEventEngine());
  auto schedule_sleep = [&serializer](absl::Duration how_long) {
    ExecCtx exec_ctx;
    auto n = std::make_shared<Notification>();
    serializer->Run(
        [how_long, n]() {
          absl::SleepFor(how_long);
          n->Notify();
        },
        DEBUG_LOCATION);
    return n;
  };
  auto before = global_stats().Collect();
  auto stats_diff_from = [&before](absl::AnyInvocable<void()> f) {
    f();
    // Insert a pause for the work serialier to update the stats. Reading stats
    // here can still race with the work serializer's update attempt.
    gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
    auto after = global_stats().Collect();
    auto diff = after->Diff(*before);
    before = std::move(after);
    return diff;
  };
  // Test adding one work item to the queue
  auto diff = stats_diff_from(
      [&] { schedule_sleep(absl::Seconds(1))->WaitForNotification(); });
  EXPECT_EQ(diff->work_serializer_items_enqueued, 1);
  EXPECT_EQ(diff->work_serializer_items_dequeued, 1);
  EXPECT_GE(diff->histogram(GlobalStats::Histogram::kWorkSerializerItemsPerRun)
                .Percentile(0.5),
            1.0);
  EXPECT_LE(diff->histogram(GlobalStats::Histogram::kWorkSerializerItemsPerRun)
                .Percentile(0.5),
            2.0);
  EXPECT_GE(diff->histogram(GlobalStats::Histogram::kWorkSerializerRunTimeMs)
                .Percentile(0.5),
            800.0);
  EXPECT_LE(diff->histogram(GlobalStats::Histogram::kWorkSerializerRunTimeMs)
                .Percentile(0.5),
            1300.0);
  EXPECT_GE(diff->histogram(GlobalStats::Histogram::kWorkSerializerWorkTimeMs)
                .Percentile(0.5),
            800.0);
  EXPECT_LE(diff->histogram(GlobalStats::Histogram::kWorkSerializerWorkTimeMs)
                .Percentile(0.5),
            1300.0);
  EXPECT_GE(
      diff->histogram(GlobalStats::Histogram::kWorkSerializerWorkTimePerItemMs)
          .Percentile(0.5),
      800.0);
  EXPECT_LE(
      diff->histogram(GlobalStats::Histogram::kWorkSerializerWorkTimePerItemMs)
          .Percentile(0.5),
      1300.0);
  EXPECT_LE(diff->histogram(GlobalStats::Histogram::kWorkSerializerRunTimeMs)
                .Percentile(0.5),
            diff->histogram(GlobalStats::Histogram::kWorkSerializerWorkTimeMs)
                .Percentile(0.5));
  // Now throw a bunch of work in and see that we get good results
  diff = stats_diff_from([&] {
    for (int i = 0; i < 10; i++) {
      schedule_sleep(absl::Milliseconds(1000));
    }
    schedule_sleep(absl::Milliseconds(1000))->WaitForNotification();
  });
  EXPECT_EQ(diff->work_serializer_items_enqueued, 11);
  EXPECT_EQ(diff->work_serializer_items_dequeued, 11);
  EXPECT_GE(diff->histogram(GlobalStats::Histogram::kWorkSerializerItemsPerRun)
                .Percentile(0.5),
            7.0);
  EXPECT_LE(diff->histogram(GlobalStats::Histogram::kWorkSerializerItemsPerRun)
                .Percentile(0.5),
            15.0);
  EXPECT_GE(diff->histogram(GlobalStats::Histogram::kWorkSerializerRunTimeMs)
                .Percentile(0.5),
            7000.0);
  EXPECT_LE(diff->histogram(GlobalStats::Histogram::kWorkSerializerRunTimeMs)
                .Percentile(0.5),
            15000.0);
  EXPECT_GE(diff->histogram(GlobalStats::Histogram::kWorkSerializerWorkTimeMs)
                .Percentile(0.5),
            7000.0);
  EXPECT_LE(diff->histogram(GlobalStats::Histogram::kWorkSerializerWorkTimeMs)
                .Percentile(0.5),
            15000.0);
  EXPECT_GE(
      diff->histogram(GlobalStats::Histogram::kWorkSerializerWorkTimePerItemMs)
          .Percentile(0.5),
      800.0);
  EXPECT_LE(
      diff->histogram(GlobalStats::Histogram::kWorkSerializerWorkTimePerItemMs)
          .Percentile(0.5),
      1300.0);
  EXPECT_LE(diff->histogram(GlobalStats::Histogram::kWorkSerializerRunTimeMs)
                .Percentile(0.5),
            diff->histogram(GlobalStats::Histogram::kWorkSerializerWorkTimeMs)
                .Percentile(0.5));

  serializer.reset();
  WaitForSingleOwner(GetDefaultEventEngine());
}

#ifndef NDEBUG
TEST(WorkSerializerTest, RunningInWorkSerializer) {
  auto work_serializer1 =
      std::make_shared<WorkSerializer>(GetDefaultEventEngine());
  auto work_serializer2 =
      std::make_shared<WorkSerializer>(GetDefaultEventEngine());
  EXPECT_FALSE(work_serializer1->RunningInWorkSerializer());
  EXPECT_FALSE(work_serializer2->RunningInWorkSerializer());
  work_serializer1->Run(
      [=]() {
        EXPECT_TRUE(work_serializer1->RunningInWorkSerializer());
        EXPECT_FALSE(work_serializer2->RunningInWorkSerializer());
        work_serializer2->Run(
            [=]() {
              EXPECT_FALSE(work_serializer1->RunningInWorkSerializer());
              EXPECT_TRUE(work_serializer2->RunningInWorkSerializer());
            },
            DEBUG_LOCATION);
      },
      DEBUG_LOCATION);
  EXPECT_FALSE(work_serializer1->RunningInWorkSerializer());
  EXPECT_FALSE(work_serializer2->RunningInWorkSerializer());
  work_serializer2->Run(
      [=]() {
        EXPECT_FALSE(work_serializer1->RunningInWorkSerializer());
        EXPECT_TRUE(work_serializer2->RunningInWorkSerializer());
        work_serializer1->Run(
            [=]() {
              EXPECT_TRUE(work_serializer1->RunningInWorkSerializer());
              EXPECT_FALSE(work_serializer2->RunningInWorkSerializer());
            },
            DEBUG_LOCATION);
      },
      DEBUG_LOCATION);
  EXPECT_FALSE(work_serializer1->RunningInWorkSerializer());
  EXPECT_FALSE(work_serializer2->RunningInWorkSerializer());
  Notification done1;
  Notification done2;
  work_serializer1->Run([&done1]() { done1.Notify(); });
  work_serializer2->Run([&done2]() { done2.Notify(); });
  done1.WaitForNotification();
  done2.WaitForNotification();
  work_serializer1.reset();
  work_serializer2.reset();
  WaitForSingleOwner(GetDefaultEventEngine());
}
#endif

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int retval = RUN_ALL_TESTS();
  grpc_shutdown();
  return retval;
}
