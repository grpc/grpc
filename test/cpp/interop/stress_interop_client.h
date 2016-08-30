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

#ifndef GRPC_TEST_CPP_STRESS_INTEROP_CLIENT_H
#define GRPC_TEST_CPP_STRESS_INTEROP_CLIENT_H

#include <memory>
#include <string>
#include <vector>

#include <grpc++/create_channel.h>

#include "test/cpp/interop/interop_client.h"
#include "test/cpp/util/metrics_server.h"

namespace grpc {
namespace testing {

using std::pair;
using std::vector;

enum TestCaseType {
  UNKNOWN_TEST = -1,
  EMPTY_UNARY,
  LARGE_UNARY,
  CLIENT_COMPRESSED_UNARY,
  CLIENT_COMPRESSED_STREAMING,
  CLIENT_STREAMING,
  SERVER_STREAMING,
  SERVER_COMPRESSED_UNARY,
  SERVER_COMPRESSED_STREAMING,
  SLOW_CONSUMER,
  HALF_DUPLEX,
  PING_PONG,
  CANCEL_AFTER_BEGIN,
  CANCEL_AFTER_FIRST_RESPONSE,
  TIMEOUT_ON_SLEEPING_SERVER,
  EMPTY_STREAM,
  STATUS_CODE_AND_MESSAGE,
  CUSTOM_METADATA
};

const vector<pair<TestCaseType, grpc::string>> kTestCaseList = {
    {EMPTY_UNARY, "empty_unary"},
    {LARGE_UNARY, "large_unary"},
    {CLIENT_COMPRESSED_UNARY, "client_compressed_unary"},
    {CLIENT_COMPRESSED_STREAMING, "client_compressed_streaming"},
    {CLIENT_STREAMING, "client_streaming"},
    {SERVER_STREAMING, "server_streaming"},
    {SERVER_COMPRESSED_UNARY, "server_compressed_unary"},
    {SERVER_COMPRESSED_STREAMING, "server_compressed_streaming"},
    {SLOW_CONSUMER, "slow_consumer"},
    {HALF_DUPLEX, "half_duplex"},
    {PING_PONG, "ping_pong"},
    {CANCEL_AFTER_BEGIN, "cancel_after_begin"},
    {CANCEL_AFTER_FIRST_RESPONSE, "cancel_after_first_response"},
    {TIMEOUT_ON_SLEEPING_SERVER, "timeout_on_sleeping_server"},
    {EMPTY_STREAM, "empty_stream"},
    {STATUS_CODE_AND_MESSAGE, "status_code_and_message"},
    {CUSTOM_METADATA, "custom_metadata"}};

class WeightedRandomTestSelector {
 public:
  // Takes a vector of <test_case, weight> pairs as the input
  WeightedRandomTestSelector(const vector<pair<TestCaseType, int>>& tests);

  // Returns a weighted-randomly chosen test case based on the test cases and
  // weights passed in the constructor
  TestCaseType GetNextTest() const;

 private:
  const vector<pair<TestCaseType, int>> tests_;
  int total_weight_;
};

class StressTestInteropClient {
 public:
  StressTestInteropClient(int test_id, const grpc::string& server_address,
                          std::shared_ptr<Channel> channel,
                          const WeightedRandomTestSelector& test_selector,
                          long test_duration_secs, long sleep_duration_ms,
                          bool do_not_abort_on_transient_failures);

  // The main function. Use this as the thread entry point.
  // qps_gauge is the QpsGauge to record the requests per second metric
  void MainLoop(std::shared_ptr<QpsGauge> qps_gauge);

 private:
  bool RunTest(TestCaseType test_case);

  int test_id_;
  const grpc::string& server_address_;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<InteropClient> interop_client_;
  const WeightedRandomTestSelector& test_selector_;
  long test_duration_secs_;
  long sleep_duration_ms_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_STRESS_INTEROP_CLIENT_H
