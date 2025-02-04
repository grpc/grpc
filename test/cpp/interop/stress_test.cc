//
//
// Copyright 2015 gRPC authors.
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

#include <grpc/support/time.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/grpcpp.h>
#include <limits.h>

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "src/core/util/crash.h"
#include "src/proto/grpc/testing/metrics.grpc.pb.h"
#include "src/proto/grpc/testing/metrics.pb.h"
#include "test/cpp/interop/interop_client.h"
#include "test/cpp/interop/stress_interop_client.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/util/metrics_server.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(int32_t, metrics_port, 8081, "The metrics server port.");

// TODO(Capstan): Consider using absl::Duration
ABSL_FLAG(int32_t, sleep_duration_ms, 0,
          "The duration (in millisec) between two"
          " consecutive test calls (per server) issued by the server.");

// TODO(Capstan): Consider using absl::Duration
ABSL_FLAG(int32_t, test_duration_secs, -1,
          "The length of time (in seconds) to run"
          " the test. Enter -1 if the test should run continuously until"
          " forcefully terminated.");

ABSL_FLAG(std::string, server_addresses, "localhost:8080",
          "The list of server addresses. The format is: \n"
          " \"<name_1>:<port_1>,<name_2>:<port_1>...<name_N>:<port_N>\"\n"
          " Note: <name> can be servername or IP address.");

ABSL_FLAG(int32_t, num_channels_per_server, 1,
          "Number of channels for each server");

ABSL_FLAG(int32_t, num_stubs_per_channel, 1,
          "Number of stubs per each channels to server. This number also "
          "indicates the max number of parallel RPC calls on each channel "
          "at any given time.");

// TODO(sreek): Add more test cases here in future
ABSL_FLAG(std::string, test_cases, "",
          "List of test cases to call along with the"
          " relative weights in the following format:\n"
          " \"<testcase_1:w_1>,<testcase_2:w_2>...<testcase_n:w_n>\"\n"
          " The following testcases are currently supported:\n"
          "   empty_unary\n"
          "   large_unary\n"
          "   large_compressed_unary\n"
          "   client_streaming\n"
          "   server_streaming\n"
          "   server_compressed_streaming\n"
          "   slow_consumer\n"
          "   half_duplex\n"
          "   ping_pong\n"
          "   cancel_after_begin\n"
          "   cancel_after_first_response\n"
          "   timeout_on_sleeping_server\n"
          "   empty_stream\n"
          "   status_code_and_message\n"
          "   custom_metadata\n"
          " Example: \"empty_unary:20,large_unary:10,empty_stream:70\"\n"
          " The above will execute 'empty_unary', 20% of the time,"
          " 'large_unary', 10% of the time and 'empty_stream' the remaining"
          " 70% of the time");

ABSL_FLAG(
    int32_t, absl_min_log_level,
    static_cast<int32_t>(absl::LogSeverityAtLeast::kInfo),
    "Severity level of messages that should be logged by absl::SetMinLogLevel");

ABSL_FLAG(int32_t, absl_vlog_level, -1,
          "Severity level of messages that should be logged. Set using "
          "absl::SetVLogLevel");

ABSL_FLAG(bool, do_not_abort_on_transient_failures, true,
          "If set to 'true', abort() is not called in case of transient "
          "failures like temporary connection failures.");

// Options from client.cc (for compatibility with interop test).
// TODO(sreek): Consolidate overlapping options
ABSL_FLAG(bool, use_alts, false,
          "Whether to use alts. Enable alts will disable tls.");
ABSL_FLAG(bool, use_tls, false, "Whether to use tls.");
ABSL_FLAG(bool, use_test_ca, false, "False to use SSL roots for google");
ABSL_FLAG(std::string, server_host_override, "",
          "Override the server host which is sent in HTTP header");

using grpc::testing::ALTS;
using grpc::testing::INSECURE;
using grpc::testing::kTestCaseList;
using grpc::testing::MetricsServiceImpl;
using grpc::testing::StressTestInteropClient;
using grpc::testing::TestCaseType;
using grpc::testing::TLS;
using grpc::testing::transport_security;
using grpc::testing::UNKNOWN_TEST;
using grpc::testing::WeightedRandomTestSelector;

static int absl_vlog_level = -1;
static absl::LogSeverityAtLeast absl_min_log_level =
    absl::LogSeverityAtLeast::kInfo;

TestCaseType GetTestTypeFromName(const std::string& test_name) {
  TestCaseType test_case = UNKNOWN_TEST;

  for (auto it = kTestCaseList.begin(); it != kTestCaseList.end(); it++) {
    if (test_name == it->second) {
      test_case = it->first;
      break;
    }
  }

  return test_case;
}

// Converts a string of comma delimited tokens to a vector of tokens
bool ParseCommaDelimitedString(const std::string& comma_delimited_str,
                               std::vector<std::string>& tokens) {
  size_t bpos = 0;
  size_t epos = std::string::npos;

  while ((epos = comma_delimited_str.find(',', bpos)) != std::string::npos) {
    tokens.emplace_back(comma_delimited_str.substr(bpos, epos - bpos));
    bpos = epos + 1;
  }

  tokens.emplace_back(comma_delimited_str.substr(bpos));  // Last token
  return true;
}

// Input: Test case string "<testcase_name:weight>,<testcase_name:weight>...."
// Output:
//   - Whether parsing was successful (return value)
//   - Vector of (test_type_enum, weight) pairs returned via 'tests' parameter
bool ParseTestCasesString(const std::string& test_cases,
                          std::vector<std::pair<TestCaseType, int>>& tests) {
  bool is_success = true;

  std::vector<std::string> tokens;
  ParseCommaDelimitedString(test_cases, tokens);

  for (auto it = tokens.begin(); it != tokens.end(); it++) {
    // Token is in the form <test_name>:<test_weight>
    size_t colon_pos = it->find(':');
    if (colon_pos == std::string::npos) {
      LOG(ERROR) << "Error in parsing test case string: " << it->c_str();
      is_success = false;
      break;
    }

    std::string test_name = it->substr(0, colon_pos);
    int weight = std::stoi(it->substr(colon_pos + 1));
    TestCaseType test_case = GetTestTypeFromName(test_name);
    if (test_case == UNKNOWN_TEST) {
      LOG(ERROR) << "Unknown test case: " << test_name;
      is_success = false;
      break;
    }

    tests.emplace_back(std::pair(test_case, weight));
  }

  return is_success;
}

// For debugging purposes
void LogParameterInfo(const std::vector<std::string>& addresses,
                      const std::vector<std::pair<TestCaseType, int>>& tests) {
  LOG(INFO) << "server_addresses: " << absl::GetFlag(FLAGS_server_addresses);
  LOG(INFO) << "test_cases : " << absl::GetFlag(FLAGS_test_cases);
  LOG(INFO) << "sleep_duration_ms: " << absl::GetFlag(FLAGS_sleep_duration_ms);
  LOG(INFO) << "test_duration_secs: "
            << absl::GetFlag(FLAGS_test_duration_secs);
  LOG(INFO) << "num_channels_per_server: "
            << absl::GetFlag(FLAGS_num_channels_per_server);
  LOG(INFO) << "num_stubs_per_channel: "
            << absl::GetFlag(FLAGS_num_stubs_per_channel);
  LOG(INFO) << "absl_vlog_level: " << absl::GetFlag(FLAGS_absl_vlog_level);
  LOG(INFO) << "absl_min_log_level: "
            << absl::GetFlag(FLAGS_absl_min_log_level);
  LOG(INFO) << "do_not_abort_on_transient_failures: "
            << (absl::GetFlag(FLAGS_do_not_abort_on_transient_failures)
                    ? "true"
                    : "false");

  int num = 0;
  for (auto it = addresses.begin(); it != addresses.end(); it++) {
    LOG(INFO) << ++num << ":" << it->c_str();
  }

  num = 0;
  for (auto it = tests.begin(); it != tests.end(); it++) {
    TestCaseType test_case = it->first;
    int weight = it->second;
    LOG(INFO) << ++num << ". TestCaseType: " << test_case
              << ", Weight: " << weight;
  }
}

void SetLogLevels() {
  absl_vlog_level = absl::GetFlag(FLAGS_absl_vlog_level);
  CHECK_LE(-1, absl_vlog_level);
  CHECK_LE(absl_vlog_level, (INT_MAX - 1));
  absl::SetVLogLevel("*grpc*/*", absl_vlog_level);

  absl_min_log_level = static_cast<absl::LogSeverityAtLeast>(
      absl::GetFlag(FLAGS_absl_min_log_level));
  CHECK_LE(absl::LogSeverityAtLeast::kInfo, absl_min_log_level);
  CHECK_LE(absl_min_log_level, absl::LogSeverityAtLeast::kInfinity);
  absl::SetMinLogLevel(absl_min_log_level);
}

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);

  SetLogLevels();
  srand(time(nullptr));

  // Parse the server addresses
  std::vector<std::string> server_addresses;
  ParseCommaDelimitedString(absl::GetFlag(FLAGS_server_addresses),
                            server_addresses);

  // Parse test cases and weights
  if (absl::GetFlag(FLAGS_test_cases).empty()) {
    LOG(ERROR) << "No test cases supplied";
    return 1;
  }

  std::vector<std::pair<TestCaseType, int>> tests;
  if (!ParseTestCasesString(absl::GetFlag(FLAGS_test_cases), tests)) {
    LOG(ERROR) << "Error in parsing test cases string "
               << absl::GetFlag(FLAGS_test_cases);
    return 1;
  }

  LogParameterInfo(server_addresses, tests);

  WeightedRandomTestSelector test_selector(tests);
  MetricsServiceImpl metrics_service;

  LOG(INFO) << "Starting test(s)..";

  std::vector<std::thread> test_threads;
  std::vector<std::unique_ptr<StressTestInteropClient>> clients;

  // Create and start the test threads.
  // Note that:
  // - Each server can have multiple channels (as configured by
  // FLAGS_num_channels_per_server).
  //
  // - Each channel can have multiple stubs (as configured by
  // FLAGS_num_stubs_per_channel). This is to test calling multiple RPCs in
  // parallel on the same channel.
  int thread_idx = 0;
  int server_idx = -1;
  char buffer[256];
  transport_security security_type =
      absl::GetFlag(FLAGS_use_alts)
          ? ALTS
          : (absl::GetFlag(FLAGS_use_tls) ? TLS : INSECURE);
  for (auto it = server_addresses.begin(); it != server_addresses.end(); it++) {
    ++server_idx;
    // Create channel(s) for each server
    for (int channel_idx = 0;
         channel_idx < absl::GetFlag(FLAGS_num_channels_per_server);
         channel_idx++) {
      LOG(INFO) << "Starting test with " << it->c_str()
                << " channel_idx=" << channel_idx << "..";
      grpc::testing::ChannelCreationFunc channel_creation_func =
          std::bind(static_cast<std::shared_ptr<grpc::Channel> (*)(
                        const std::string&, const std::string&,
                        grpc::testing::transport_security, bool)>(
                        grpc::CreateTestChannel),
                    *it, absl::GetFlag(FLAGS_server_host_override),
                    security_type, !absl::GetFlag(FLAGS_use_test_ca));

      // Create stub(s) for each channel
      for (int stub_idx = 0;
           stub_idx < absl::GetFlag(FLAGS_num_stubs_per_channel); stub_idx++) {
        clients.emplace_back(new StressTestInteropClient(
            ++thread_idx, *it, channel_creation_func, test_selector,
            absl::GetFlag(FLAGS_test_duration_secs),
            absl::GetFlag(FLAGS_sleep_duration_ms),
            absl::GetFlag(FLAGS_do_not_abort_on_transient_failures)));

        bool is_already_created = false;
        // QpsGauge name
        std::snprintf(buffer, sizeof(buffer),
                      "/stress_test/server_%d/channel_%d/stub_%d/qps",
                      server_idx, channel_idx, stub_idx);

        test_threads.emplace_back(std::thread(
            &StressTestInteropClient::MainLoop, clients.back().get(),
            metrics_service.CreateQpsGauge(buffer, &is_already_created)));

        // The QpsGauge should not have been already created
        CHECK(!is_already_created);
      }
    }
  }

  // Start metrics server before waiting for the stress test threads
  std::unique_ptr<grpc::Server> metrics_server;
  if (absl::GetFlag(FLAGS_metrics_port) > 0) {
    metrics_server =
        metrics_service.StartServer(absl::GetFlag(FLAGS_metrics_port));
  }

  // Wait for the stress test threads to complete
  for (auto it = test_threads.begin(); it != test_threads.end(); it++) {
    it->join();
  }

  return 0;
}
