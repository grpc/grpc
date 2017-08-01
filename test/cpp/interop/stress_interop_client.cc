/*
 *
 * Copyright 2015 gRPC authors.
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

#include "test/cpp/interop/stress_interop_client.h"

#include <memory>
#include <string>
#include <vector>

#include <grpc++/create_channel.h>
#include <grpc/support/log.h>

#include "test/cpp/interop/interop_client.h"
#include "test/cpp/util/metrics_server.h"

namespace grpc {
namespace testing {

using std::pair;
using std::vector;

WeightedRandomTestSelector::WeightedRandomTestSelector(
    const vector<pair<TestCaseType, int>>& tests)
    : tests_(tests) {
  total_weight_ = 0;
  for (auto it = tests.begin(); it != tests.end(); it++) {
    total_weight_ += it->second;
  }
}

// Returns a weighted-randomly selected test case based on the test weights
// passed in the constructror
TestCaseType WeightedRandomTestSelector::GetNextTest() const {
  int random = 0;
  TestCaseType selected_test = UNKNOWN_TEST;

  // Get a random number from [0 to the total_weight - 1]
  random = rand() % total_weight_;

  int weight_sofar = 0;
  for (auto it = tests_.begin(); it != tests_.end(); it++) {
    weight_sofar += it->second;
    if (random < weight_sofar) {
      selected_test = it->first;
      break;
    }
  }

  // It is a bug in the logic if no test is selected at this point
  GPR_ASSERT(selected_test != UNKNOWN_TEST);
  return selected_test;
}

StressTestInteropClient::StressTestInteropClient(
    int test_id, const grpc::string& server_address,
    std::shared_ptr<Channel> channel,
    const WeightedRandomTestSelector& test_selector, long test_duration_secs,
    long sleep_duration_ms, bool do_not_abort_on_transient_failures)
    : test_id_(test_id),
      server_address_(server_address),
      channel_(channel),
      interop_client_(new InteropClient(channel, false,
                                        do_not_abort_on_transient_failures)),
      test_selector_(test_selector),
      test_duration_secs_(test_duration_secs),
      sleep_duration_ms_(sleep_duration_ms) {}

void StressTestInteropClient::MainLoop(std::shared_ptr<QpsGauge> qps_gauge) {
  gpr_log(GPR_INFO, "Running test %d. ServerAddr: %s", test_id_,
          server_address_.c_str());

  gpr_timespec test_end_time;
  if (test_duration_secs_ < 0) {
    test_end_time = gpr_inf_future(GPR_CLOCK_REALTIME);
  } else {
    test_end_time =
        gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                     gpr_time_from_seconds(test_duration_secs_, GPR_TIMESPAN));
  }

  qps_gauge->Reset();

  while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), test_end_time) < 0) {
    // Select the test case to execute based on the weights and execute it
    TestCaseType test_case = test_selector_.GetNextTest();
    gpr_log(GPR_DEBUG, "%d - Executing the test case %d", test_id_, test_case);
    RunTest(test_case);

    qps_gauge->Incr();

    // Sleep between successive calls if needed
    if (sleep_duration_ms_ > 0) {
      gpr_timespec sleep_time =
          gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                       gpr_time_from_millis(sleep_duration_ms_, GPR_TIMESPAN));
      gpr_sleep_until(sleep_time);
    }
  }
}

bool StressTestInteropClient::RunTest(TestCaseType test_case) {
  bool is_success = false;
  switch (test_case) {
    case EMPTY_UNARY: {
      is_success = interop_client_->DoEmpty();
      break;
    }
    case LARGE_UNARY: {
      is_success = interop_client_->DoLargeUnary();
      break;
    }
    case CLIENT_COMPRESSED_UNARY: {
      is_success = interop_client_->DoClientCompressedUnary();
      break;
    }
    case CLIENT_COMPRESSED_STREAMING: {
      is_success = interop_client_->DoClientCompressedStreaming();
      break;
    }
    case CLIENT_STREAMING: {
      is_success = interop_client_->DoRequestStreaming();
      break;
    }
    case SERVER_STREAMING: {
      is_success = interop_client_->DoResponseStreaming();
      break;
    }
    case SERVER_COMPRESSED_UNARY: {
      is_success = interop_client_->DoServerCompressedUnary();
      break;
    }
    case SERVER_COMPRESSED_STREAMING: {
      is_success = interop_client_->DoServerCompressedStreaming();
      break;
    }
    case SLOW_CONSUMER: {
      is_success = interop_client_->DoResponseStreamingWithSlowConsumer();
      break;
    }
    case HALF_DUPLEX: {
      is_success = interop_client_->DoHalfDuplex();
      break;
    }
    case PING_PONG: {
      is_success = interop_client_->DoPingPong();
      break;
    }
    case CANCEL_AFTER_BEGIN: {
      is_success = interop_client_->DoCancelAfterBegin();
      break;
    }
    case CANCEL_AFTER_FIRST_RESPONSE: {
      is_success = interop_client_->DoCancelAfterFirstResponse();
      break;
    }
    case TIMEOUT_ON_SLEEPING_SERVER: {
      is_success = interop_client_->DoTimeoutOnSleepingServer();
      break;
    }
    case EMPTY_STREAM: {
      is_success = interop_client_->DoEmptyStream();
      break;
    }
    case STATUS_CODE_AND_MESSAGE: {
      is_success = interop_client_->DoStatusWithMessage();
      break;
    }
    case CUSTOM_METADATA: {
      is_success = interop_client_->DoCustomMetadata();
      break;
    }
    default: {
      gpr_log(GPR_ERROR, "Invalid test case (%d)", test_case);
      GPR_ASSERT(false);
      break;
    }
  }

  return is_success;
}

}  // namespace testing
}  // namespace grpc
