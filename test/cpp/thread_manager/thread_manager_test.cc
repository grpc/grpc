/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *is % allowed in string
 */

#include <inttypes.h>
#include <ctime>
#include <memory>
#include <string>

#include <gflags/gflags.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpcpp/grpcpp.h>

#include "src/cpp/thread_manager/thread_manager.h"
#include "test/cpp/util/test_config.h"

namespace grpc {

struct ThreadManagerTestSettings {
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
};

class ThreadManagerTest final : public grpc::ThreadManager {
 public:
  ThreadManagerTest(const char* name, grpc_resource_quota* rq,
                    const ThreadManagerTestSettings& settings)
      : ThreadManager(name, rq, settings.min_pollers, settings.max_pollers),
        settings_(settings),
        num_do_work_(0),
        num_poll_for_work_(0),
        num_work_found_(0) {}

  grpc::ThreadManager::WorkStatus PollForWork(void** tag, bool* ok) override;
  void DoWork(void* tag, bool ok) override;

  // Get number of times PollForWork() returned WORK_FOUND
  int GetNumWorkFound();
  // Get number of times DoWork() was called
  int GetNumDoWork();

 private:
  void SleepForMs(int sleep_time_ms);

  ThreadManagerTestSettings settings_;

  // Counters
  gpr_atm num_do_work_;        // Number of calls to DoWork
  gpr_atm num_poll_for_work_;  // Number of calls to PollForWork
  gpr_atm num_work_found_;     // Number of times WORK_FOUND was returned
};

void ThreadManagerTest::SleepForMs(int duration_ms) {
  gpr_timespec sleep_time =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                   gpr_time_from_millis(duration_ms, GPR_TIMESPAN));
  gpr_sleep_until(sleep_time);
}

grpc::ThreadManager::WorkStatus ThreadManagerTest::PollForWork(void** tag,
                                                               bool* ok) {
  int call_num = gpr_atm_no_barrier_fetch_add(&num_poll_for_work_, 1);
  if (call_num >= settings_.max_poll_calls) {
    Shutdown();
    return SHUTDOWN;
  }

  SleepForMs(settings_.poll_duration_ms);  // Simulate "polling" duration
  *tag = nullptr;
  *ok = true;

  // Return timeout roughly 1 out of every 3 calls just to make the test a bit
  // more interesting
  if (call_num % 3 == 0) {
    return TIMEOUT;
  }

  gpr_atm_no_barrier_fetch_add(&num_work_found_, 1);
  return WORK_FOUND;
}

void ThreadManagerTest::DoWork(void* tag, bool ok) {
  gpr_atm_no_barrier_fetch_add(&num_do_work_, 1);
  SleepForMs(settings_.work_duration_ms);  // Simulate work by sleeping
}

int ThreadManagerTest::GetNumWorkFound() {
  return static_cast<int>(gpr_atm_no_barrier_load(&num_work_found_));
}

int ThreadManagerTest::GetNumDoWork() {
  return static_cast<int>(gpr_atm_no_barrier_load(&num_do_work_));
}
}  // namespace grpc

// Test that the number of times DoWork() is called is equal to the number of
// times PollForWork() returned WORK_FOUND
static void TestPollAndWork() {
  grpc_resource_quota* rq = grpc_resource_quota_create("Test-poll-and-work");
  grpc::ThreadManagerTestSettings settings = {
      2 /* min_pollers */, 10 /* max_pollers */, 10 /* poll_duration_ms */,
      1 /* work_duration_ms */, 50 /* max_poll_calls */};

  grpc::ThreadManagerTest test_thd_mgr("TestThreadManager", rq, settings);
  grpc_resource_quota_unref(rq);

  test_thd_mgr.Initialize();  // Start the thread manager
  test_thd_mgr.Wait();        // Wait for all threads to finish

  // Verify that The number of times DoWork() was called is equal to the number
  // of times WORK_FOUND was returned
  gpr_log(GPR_DEBUG, "DoWork() called %d times", test_thd_mgr.GetNumDoWork());
  GPR_ASSERT(test_thd_mgr.GetNumDoWork() == test_thd_mgr.GetNumWorkFound());
}

static void TestThreadQuota() {
  const int kMaxNumThreads = 3;
  grpc_resource_quota* rq = grpc_resource_quota_create("Test-thread-quota");
  grpc_resource_quota_set_max_threads(rq, kMaxNumThreads);

  // Set work_duration_ms to be much greater than poll_duration_ms. This way,
  // the thread manager will be forced to create more 'polling' threads to
  // honor the min_pollers guarantee
  grpc::ThreadManagerTestSettings settings = {
      1 /* min_pollers */, 1 /* max_pollers */, 1 /* poll_duration_ms */,
      10 /* work_duration_ms */, 50 /* max_poll_calls */};

  // Create two thread managers (but with same resource quota). This means
  // that the max number of active threads across BOTH the thread managers
  // cannot be greater than kMaxNumthreads
  grpc::ThreadManagerTest test_thd_mgr_1("TestThreadManager-1", rq, settings);
  grpc::ThreadManagerTest test_thd_mgr_2("TestThreadManager-2", rq, settings);
  // It is ok to unref resource quota before starting thread managers.
  grpc_resource_quota_unref(rq);

  // Start both thread managers
  test_thd_mgr_1.Initialize();
  test_thd_mgr_2.Initialize();

  // Wait for both to finish
  test_thd_mgr_1.Wait();
  test_thd_mgr_2.Wait();

  // Now verify that the total number of active threads in either thread manager
  // never exceeds kMaxNumThreads
  //
  // NOTE: Actually the total active threads across *both* thread managers at
  // any point of time never exceeds kMaxNumThreads but unfortunately there is
  // no easy way to verify it (i.e we can't just do (max1 + max2 <= k))
  // Its okay to not test this case here. The resource quota c-core tests
  // provide enough coverage to resource quota object with multiple resource
  // users
  int max1 = test_thd_mgr_1.GetMaxActiveThreadsSoFar();
  int max2 = test_thd_mgr_2.GetMaxActiveThreadsSoFar();
  gpr_log(
      GPR_DEBUG,
      "MaxActiveThreads in TestThreadManager_1: %d, TestThreadManager_2: %d",
      max1, max2);
  GPR_ASSERT(max1 <= kMaxNumThreads && max2 <= kMaxNumThreads);
}

int main(int argc, char** argv) {
  std::srand(std::time(nullptr));
  grpc::testing::InitTest(&argc, &argv, true);
  grpc_init();

  TestPollAndWork();
  TestThreadQuota();

  grpc_shutdown();
  return 0;
}
