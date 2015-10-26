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

namespace grpc {
namespace testing {

using std::pair;
using std::vector;

// TODO(sreek): Add more test cases here in future
enum TestCaseType {
  UNKNOWN_TEST = -1,
  EMPTY_UNARY = 0,
  LARGE_UNARY = 1,
  LARGE_COMPRESSED_UNARY = 2,
  CLIENT_STREAMING = 3,
  SERVER_STREAMING = 4,
  EMPTY_STREAM = 5
};

const vector<pair<TestCaseType, grpc::string>> kTestCaseList = {
    {EMPTY_UNARY, "empty_unary"},
    {LARGE_UNARY, "large_unary"},
    {LARGE_COMPRESSED_UNARY, "large_compressed_unary"},
    {CLIENT_STREAMING, "client_streaming"},
    {SERVER_STREAMING, "server_streaming"},
    {EMPTY_STREAM, "empty_stream"}};

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
                          const WeightedRandomTestSelector& test_selector,
                          long test_duration_secs, long sleep_duration_ms);

  void MainLoop();  // The main function. Use this as the thread entry point.

 private:
  void RunTest(TestCaseType test_case);

  int test_id_;
  std::unique_ptr<InteropClient> interop_client_;
  const grpc::string& server_address_;
  const WeightedRandomTestSelector& test_selector_;
  long test_duration_secs_;
  long sleep_duration_ms_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_STRESS_INTEROP_CLIENT_H
