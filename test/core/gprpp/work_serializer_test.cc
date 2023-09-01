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

#include "src/core/lib/gprpp/work_serializer.h"

#include <stddef.h>

#include <algorithm>
#include <memory>
#include <thread>
#include <vector>

#include "absl/synchronization/barrier.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/thd.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/util/test_config.h"

namespace {
TEST(WorkSerializerTest, NoOp) {
  grpc_core::WorkSerializer lock(
      grpc_event_engine::experimental::GetDefaultEventEngine());
}

TEST(WorkSerializerTest, ExecuteOneRun) {
  grpc_core::WorkSerializer lock(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  gpr_event done;
  gpr_event_init(&done);
  lock.Run([&done]() { gpr_event_set(&done, reinterpret_cast<void*>(1)); },
           DEBUG_LOCATION);
  EXPECT_TRUE(gpr_event_wait(&done, grpc_timeout_seconds_to_deadline(5)) !=
              nullptr);
}

TEST(WorkSerializerTest, ExecuteOneScheduleAndDrain) {
  grpc_core::WorkSerializer lock(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  gpr_event done;
  gpr_event_init(&done);
  lock.Run([&done]() { gpr_event_set(&done, reinterpret_cast<void*>(1)); },
           DEBUG_LOCATION);
  EXPECT_EQ(gpr_event_get(&done), nullptr);
  EXPECT_TRUE(gpr_event_wait(&done, grpc_timeout_seconds_to_deadline(5)) !=
              nullptr);
}

class TestThread {
 public:
  explicit TestThread(grpc_core::WorkSerializer* lock)
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

  grpc_core::WorkSerializer* lock_ = nullptr;
  grpc_core::Thread thread_;
  size_t counter_ = 0;
  gpr_event done_;
};

TEST(WorkSerializerTest, ExecuteMany) {
  grpc_core::WorkSerializer lock(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  {
    std::vector<std::unique_ptr<TestThread>> threads;
    for (size_t i = 0; i < 10; ++i) {
      threads.push_back(std::make_unique<TestThread>(&lock));
    }
  }
}

class TestThreadScheduleAndDrain {
 public:
  explicit TestThreadScheduleAndDrain(grpc_core::WorkSerializer* lock)
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

  grpc_core::WorkSerializer* lock_ = nullptr;
  grpc_core::Thread thread_;
  size_t counter_ = 0;
  gpr_event done_;
};

TEST(WorkSerializerTest, ExecuteManyScheduleAndDrain) {
  grpc_core::WorkSerializer lock(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  {
    std::vector<std::unique_ptr<TestThreadScheduleAndDrain>> threads;
    for (size_t i = 0; i < 10; ++i) {
      threads.push_back(std::make_unique<TestThreadScheduleAndDrain>(&lock));
    }
  }
}

TEST(WorkSerializerTest, ExecuteManyMixedRunScheduleAndDrain) {
  grpc_core::WorkSerializer lock(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  {
    std::vector<std::unique_ptr<TestThread>> run_threads;
    std::vector<std::unique_ptr<TestThreadScheduleAndDrain>> schedule_threads;
    for (size_t i = 0; i < 10; ++i) {
      run_threads.push_back(std::make_unique<TestThread>(&lock));
      schedule_threads.push_back(
          std::make_unique<TestThreadScheduleAndDrain>(&lock));
    }
  }
}

// Tests that work serializers allow destruction from the last callback
TEST(WorkSerializerTest, CallbackDestroysWorkSerializer) {
  auto lock = std::make_shared<grpc_core::WorkSerializer>(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  lock->Run([&]() { lock.reset(); }, DEBUG_LOCATION);
  grpc_event_engine::experimental::WaitForSingleOwner(
      grpc_event_engine::experimental::GetDefaultEventEngine());
}

// Tests additional racy conditions when the last callback triggers work
// serializer destruction.
TEST(WorkSerializerTest, WorkSerializerDestructionRace) {
  for (int i = 0; i < 1000; ++i) {
    auto lock = std::make_shared<grpc_core::WorkSerializer>(
        grpc_event_engine::experimental::GetDefaultEventEngine());
    grpc_core::Notification notification;
    std::thread t1([&]() {
      notification.WaitForNotification();
      lock.reset();
    });
    lock->Run([&]() { notification.Notify(); }, DEBUG_LOCATION);
    t1.join();
  }
}

// Tests racy conditions when the last callback triggers work
// serializer destruction.
TEST(WorkSerializerTest, WorkSerializerDestructionRaceMultipleThreads) {
  auto lock = std::make_shared<grpc_core::WorkSerializer>(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  absl::Barrier barrier(11);
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([lock, &barrier]() mutable {
      barrier.Block();
      lock->Run([lock]() mutable { lock.reset(); }, DEBUG_LOCATION);
    });
  }
  barrier.Block();
  lock.reset();
  for (auto& thread : threads) {
    thread.join();
  }
}

#ifndef NDEBUG
TEST(WorkSerializerTest, RunningInWorkSerializer) {
  auto work_serializer1 = std::make_shared<grpc_core::WorkSerializer>(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  auto work_serializer2 = std::make_shared<grpc_core::WorkSerializer>(
      grpc_event_engine::experimental::GetDefaultEventEngine());
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
  work_serializer1.reset();
  work_serializer2.reset();
  grpc_event_engine::experimental::WaitForSingleOwner(
      grpc_event_engine::experimental::GetDefaultEventEngine());
}
#endif

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int retval = RUN_ALL_TESTS();
  grpc_shutdown();
  return retval;
}
