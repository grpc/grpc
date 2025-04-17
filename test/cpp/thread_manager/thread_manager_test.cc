//
//
// Copyright 2016 gRPC authors.
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
// is % allowed in string
//

#include "src/cpp/thread_manager/thread_manager.h"

#include <grpc/support/port_platform.h>
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <memory>
#include <thread>

#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/util/crash.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace {

struct TestThreadManagerSettings {
  // The min number of pollers that SHOULD be active in ThreadManager
  int min_pollers;

  // The max number of pollers that could be active in ThreadManager
  int max_pollers;

  // The sleep duration in PollForWork() function to simulate "polling"
  int poll_duration_ms;

  // The sleep duration in DoWork() function to simulate "work"
  int work_duration_ms;

  // Max number of times PollForWork() is called before shutting down
  int max_poll_calls;

  // The thread limit (for use in resource quote)
  int thread_limit;

  // How many should be instantiated
  int thread_manager_count;
};

class TestThreadManager final : public grpc::ThreadManager {
 public:
  TestThreadManager(const char* name, grpc_resource_quota* rq,
                    const TestThreadManagerSettings& settings)
      : ThreadManager(name, rq, settings.min_pollers, settings.max_pollers),
        settings_(settings),
        num_do_work_(0),
        num_poll_for_work_(0),
        num_work_found_(0) {}

  grpc::ThreadManager::WorkStatus PollForWork(void** tag, bool* ok) override;
  void DoWork(void* /* tag */, bool /*ok*/, bool /*resources*/) override {
    num_do_work_.fetch_add(1, std::memory_order_relaxed);

    // Simulate work by sleeping
    std::this_thread::sleep_for(
        std::chrono::milliseconds(settings_.work_duration_ms));
  }

  // Get number of times PollForWork() was called
  int num_poll_for_work() const {
    return num_poll_for_work_.load(std::memory_order_relaxed);
  }
  // Get number of times PollForWork() returned WORK_FOUND
  int num_work_found() const {
    return num_work_found_.load(std::memory_order_relaxed);
  }
  // Get number of times DoWork() was called
  int num_do_work() const {
    return num_do_work_.load(std::memory_order_relaxed);
  }

 private:
  TestThreadManagerSettings settings_;

  // Counters
  std::atomic_int num_do_work_;        // Number of calls to DoWork
  std::atomic_int num_poll_for_work_;  // Number of calls to PollForWork
  std::atomic_int num_work_found_;  // Number of times WORK_FOUND was returned
};

grpc::ThreadManager::WorkStatus TestThreadManager::PollForWork(void** tag,
                                                               bool* ok) {
  int call_num = num_poll_for_work_.fetch_add(1, std::memory_order_relaxed);
  if (call_num >= settings_.max_poll_calls) {
    Shutdown();
    return SHUTDOWN;
  }

  // Simulate "polling" duration
  std::this_thread::sleep_for(
      std::chrono::milliseconds(settings_.poll_duration_ms));
  *tag = nullptr;
  *ok = true;

  // Return timeout roughly 1 out of every 3 calls just to make the test a bit
  // more interesting
  if (call_num % 3 == 0) {
    return TIMEOUT;
  }

  num_work_found_.fetch_add(1, std::memory_order_relaxed);
  return WORK_FOUND;
}

class ThreadManagerTest
    : public ::testing::TestWithParam<TestThreadManagerSettings> {
 protected:
  void SetUp() override {
    grpc_resource_quota* rq = grpc_resource_quota_create("Thread manager test");
    if (GetParam().thread_limit > 0) {
      grpc_resource_quota_set_max_threads(rq, GetParam().thread_limit);
    }
    for (int i = 0; i < GetParam().thread_manager_count; i++) {
      thread_manager_.emplace_back(
          new TestThreadManager("TestThreadManager", rq, GetParam()));
    }
    grpc_resource_quota_unref(rq);
    for (auto& tm : thread_manager_) {
      tm->Initialize();
    }
    for (auto& tm : thread_manager_) {
      tm->Wait();
    }
  }

  std::vector<std::unique_ptr<TestThreadManager>> thread_manager_;
};

TestThreadManagerSettings scenarios[] = {
    {2 /* min_pollers */, 10 /* max_pollers */, 10 /* poll_duration_ms */,
     1 /* work_duration_ms */, 50 /* max_poll_calls */,
     INT_MAX /* thread_limit */, 1 /* thread_manager_count */},
    {1 /* min_pollers */, 1 /* max_pollers */, 1 /* poll_duration_ms */,
     10 /* work_duration_ms */, 50 /* max_poll_calls */, 3 /* thread_limit */,
     2 /* thread_manager_count */}};

INSTANTIATE_TEST_SUITE_P(ThreadManagerTest, ThreadManagerTest,
                         ::testing::ValuesIn(scenarios));

TEST_P(ThreadManagerTest, TestPollAndWork) {
  for (auto& tm : thread_manager_) {
    // Verify that The number of times DoWork() was called is equal to the
    // number of times WORK_FOUND was returned
    VLOG(2) << "DoWork() called " << tm->num_do_work() << " times";
    EXPECT_GE(tm->num_poll_for_work(), GetParam().max_poll_calls);
    EXPECT_EQ(tm->num_do_work(), tm->num_work_found());
  }
}

TEST_P(ThreadManagerTest, TestThreadQuota) {
  if (GetParam().thread_limit > 0) {
    for (auto& tm : thread_manager_) {
      EXPECT_GE(tm->num_poll_for_work(), GetParam().max_poll_calls);
      EXPECT_LE(tm->GetMaxActiveThreadsSoFar(), GetParam().thread_limit);
    }
  }
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  std::srand(std::time(nullptr));
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);

  grpc_init();
  auto ret = RUN_ALL_TESTS();
  grpc_shutdown();

  return ret;
}
