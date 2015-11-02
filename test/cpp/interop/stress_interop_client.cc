/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *is % allowed in string
 */

#include "test/cpp/interop/stress_interop_client.h"

#include <memory>
#include <string>
#include <vector>

#include <grpc++/create_channel.h>

#include "test/cpp/interop/interop_client.h"

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
    const WeightedRandomTestSelector& test_selector, long test_duration_secs,
    long sleep_duration_ms)
    : test_id_(test_id),
      server_address_(server_address),
      test_selector_(test_selector),
      test_duration_secs_(test_duration_secs),
      sleep_duration_ms_(sleep_duration_ms) {
  // TODO(sreek): This will change once we add support for other tests
  // that won't work with InsecureCredentials()
  std::shared_ptr<Channel> channel(
      CreateChannel(server_address, InsecureCredentials()));
  interop_client_.reset(new InteropClient(channel, false));
}

void StressTestInteropClient::MainLoop() {
  gpr_log(GPR_INFO, "Running test %d. ServerAddr: %s", test_id_,
          server_address_.c_str());

  gpr_timespec test_end_time =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                   gpr_time_from_seconds(test_duration_secs_, GPR_TIMESPAN));

  gpr_timespec current_time = gpr_now(GPR_CLOCK_REALTIME);
  while (test_duration_secs_ < 0 ||
         gpr_time_cmp(current_time, test_end_time) < 0) {
    // Select the test case to execute based on the weights and execute it
    TestCaseType test_case = test_selector_.GetNextTest();
    gpr_log(GPR_INFO, "%d - Executing the test case %d", test_id_, test_case);
    RunTest(test_case);

    // Sleep between successive calls if needed
    if (sleep_duration_ms_ > 0) {
      gpr_timespec sleep_time = gpr_time_add(
          current_time, gpr_time_from_millis(sleep_duration_ms_, GPR_TIMESPAN));
      gpr_sleep_until(sleep_time);
    }

    current_time = gpr_now(GPR_CLOCK_REALTIME);
  }
}

// TODO(sree): Add all interop tests
void StressTestInteropClient::RunTest(TestCaseType test_case) {
  switch (test_case) {
    case EMPTY_UNARY: {
      interop_client_->DoEmpty();
      break;
    }
    case LARGE_UNARY: {
      interop_client_->DoLargeUnary();
      break;
    }
    case LARGE_COMPRESSED_UNARY: {
      interop_client_->DoLargeCompressedUnary();
      break;
    }
    case CLIENT_STREAMING: {
      interop_client_->DoRequestStreaming();
      break;
    }
    case SERVER_STREAMING: {
      interop_client_->DoResponseStreaming();
      break;
    }
    case EMPTY_STREAM: {
      interop_client_->DoEmptyStream();
      break;
    }
    default: {
      gpr_log(GPR_ERROR, "Invalid test case (%d)", test_case);
      GPR_ASSERT(false);
      break;
    }
  }
}

}  // namespace testing
}  // namespace grpc
