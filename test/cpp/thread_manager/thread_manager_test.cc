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

#include <ctime>
#include <memory>
#include <string>

#include <gflags/gflags.h>
#include <grpc++/grpc++.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/cpp/thread_manager/thread_manager.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
class MockThreadManager final : public grpc::ThreadManager {
 public:
  MockThreadManager(int min_pollers, int max_pollers)
      : ThreadManager(min_pollers, max_pollers),
        num_do_work_(0),
        num_poll_for_work_(0),
        num_work_found_(0) {}

  MockThreadManager(int min_pollers, int max_pollers, int max_threads)
      : ThreadManager(min_pollers, max_pollers, max_threads),
        num_do_work_(0),
        num_poll_for_work_(0),
        num_work_found_(0) {}

  grpc::ThreadManager::WorkStatus PollForWork(void** tag, bool* ok) override;
  void DoWork(void* tag, bool ok) override;

  // Number of times DoWork() was called
  long NumDoWork() { return gpr_atm_no_barrier_load(&num_do_work_); }

  // Number of times work was found
  long NumWorkFound() { return gpr_atm_no_barrier_load(&num_work_found_); }

 private:
  void SleepForMs(int sleep_time_ms);

  static const int kPollingTimeoutMsec = 10;
  static const int kDoWorkDurationMsec = 1;

  // PollForWork will return SHUTDOWN after these many number of invocations
  static const int kMaxNumPollForWork = 50;

  gpr_atm num_do_work_;        // Number of calls to DoWork
  gpr_atm num_poll_for_work_;  // Number of calls to PollForWork
  gpr_atm num_work_found_;     // Number of times WORK_FOUND was returned
};

void MockThreadManager::SleepForMs(int duration_ms) {
  gpr_timespec sleep_time =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                   gpr_time_from_millis(duration_ms, GPR_TIMESPAN));
  gpr_sleep_until(sleep_time);
}

grpc::ThreadManager::WorkStatus MockThreadManager::PollForWork(void** tag,
                                                               bool* ok) {
  int call_num = gpr_atm_no_barrier_fetch_add(&num_poll_for_work_, 1);

  if (call_num >= kMaxNumPollForWork) {
    Shutdown();
    return SHUTDOWN;
  }

  // Simulate "polling for work" by sleeping for sometime
  SleepForMs(kPollingTimeoutMsec);

  *tag = nullptr;
  *ok = true;

  // Return timeout roughly 1 out of every 3 calls
  if (call_num % 3 == 0) {
    return TIMEOUT;
  } else {
    gpr_atm_no_barrier_fetch_add(&num_work_found_, 1);
    return WORK_FOUND;
  }
}

void MockThreadManager::DoWork(void* tag, bool ok) {
  gpr_atm_no_barrier_fetch_add(&num_do_work_, 1);
  SleepForMs(kDoWorkDurationMsec);  // Simulate doing work by sleeping
}

}  // namespace grpc

static void PerformTest(int min_pollers, int max_pollers, int max_threads = 0) {
  std::unique_ptr<grpc::MockThreadManager> mock_tm;

  if (max_threads == 0) {
    // We could instead call MockThreadManager's constrcutor with max_threads =
    // INT_MAX but the idea here is to test both the constrctors of
    // ThreadManager
    mock_tm.reset(new grpc::MockThreadManager(min_pollers, max_pollers));
  } else {
    mock_tm.reset(
        new grpc::MockThreadManager(min_pollers, max_pollers, max_threads));
  }

  // Initialize() starts the ThreadManager
  mock_tm->Initialize();

  // Wait for all the threads to gracefully terminate
  mock_tm->Wait();

  // The number of times DoWork() was called is equal to the number of times
  // WORK_FOUND was returned
  gpr_log(GPR_DEBUG, "DoWork() called %ld times", mock_tm->NumDoWork());
  GPR_ASSERT(mock_tm->NumDoWork() == mock_tm->NumWorkFound());
}

int main(int argc, char** argv) {
  std::srand(std::time(nullptr));

  grpc::testing::InitTest(&argc, &argv, true);
  PerformTest(2, 10);
  PerformTest(2, 10, -1);  // Same as PerformTest(2, 10). Just tests the case
                           // where max_threads = -1
  PerformTest(1, 1, 1);
  PerformTest(2, 3, 4);

  return 0;
}
