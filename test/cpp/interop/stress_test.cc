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

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gflags/gflags.h>
#include <grpc++/create_channel.h>
#include <grpc++/grpc++.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/proto/grpc/testing/metrics.grpc.pb.h"
#include "src/proto/grpc/testing/metrics.pb.h"
#include "test/cpp/interop/interop_client.h"
#include "test/cpp/interop/stress_interop_client.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/util/metrics_server.h"
#include "test/cpp/util/test_config.h"

extern "C" {
extern void gpr_default_log(gpr_log_func_args* args);
}

DEFINE_int32(metrics_port, 8081, "The metrics server port.");

DEFINE_int32(sleep_duration_ms, 0,
             "The duration (in millisec) between two"
             " consecutive test calls (per server) issued by the server.");

DEFINE_int32(test_duration_secs, -1,
             "The length of time (in seconds) to run"
             " the test. Enter -1 if the test should run continuously until"
             " forcefully terminated.");

DEFINE_string(server_addresses, "localhost:8080",
              "The list of server addresses. The format is: \n"
              " \"<name_1>:<port_1>,<name_2>:<port_1>...<name_N>:<port_N>\"\n"
              " Note: <name> can be servername or IP address.");

DEFINE_int32(num_channels_per_server, 1, "Number of channels for each server");

DEFINE_int32(num_stubs_per_channel, 1,
             "Number of stubs per each channels to server. This number also "
             "indicates the max number of parallel RPC calls on each channel "
             "at any given time.");

// TODO(sreek): Add more test cases here in future
DEFINE_string(test_cases, "",
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

DEFINE_int32(log_level, GPR_LOG_SEVERITY_INFO,
             "Severity level of messages that should be logged. Any messages "
             "greater than or equal to the level set here will be logged. "
             "The choices are: 0 (GPR_LOG_SEVERITY_DEBUG), 1 "
             "(GPR_LOG_SEVERITY_INFO) and 2 (GPR_LOG_SEVERITY_ERROR)");

DEFINE_bool(do_not_abort_on_transient_failures, true,
            "If set to 'true', abort() is not called in case of transient "
            "failures like temporary connection failures.");

// Options from client.cc (for compatibility with interop test).
// TODO(sreek): Consolidate overlapping options
DEFINE_bool(use_tls, false, "Whether to use tls.");
DEFINE_bool(use_test_ca, false, "False to use SSL roots for google");
DEFINE_string(server_host_override, "foo.test.google.fr",
              "Override the server host which is sent in HTTP header");

using grpc::testing::MetricsService;
using grpc::testing::MetricsServiceImpl;
using grpc::testing::StressTestInteropClient;
using grpc::testing::TestCaseType;
using grpc::testing::UNKNOWN_TEST;
using grpc::testing::WeightedRandomTestSelector;
using grpc::testing::kTestCaseList;

static int log_level = GPR_LOG_SEVERITY_DEBUG;

// A simple wrapper to grp_default_log() function. This only logs messages at or
// above the current log level (set in 'log_level' variable)
void TestLogFunction(gpr_log_func_args* args) {
  if (args->severity >= log_level) {
    gpr_default_log(args);
  }
}

TestCaseType GetTestTypeFromName(const grpc::string& test_name) {
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
bool ParseCommaDelimitedString(const grpc::string& comma_delimited_str,
                               std::vector<grpc::string>& tokens) {
  size_t bpos = 0;
  size_t epos = grpc::string::npos;

  while ((epos = comma_delimited_str.find(',', bpos)) != grpc::string::npos) {
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
bool ParseTestCasesString(const grpc::string& test_cases,
                          std::vector<std::pair<TestCaseType, int>>& tests) {
  bool is_success = true;

  std::vector<grpc::string> tokens;
  ParseCommaDelimitedString(test_cases, tokens);

  for (auto it = tokens.begin(); it != tokens.end(); it++) {
    // Token is in the form <test_name>:<test_weight>
    size_t colon_pos = it->find(':');
    if (colon_pos == grpc::string::npos) {
      gpr_log(GPR_ERROR, "Error in parsing test case string: %s", it->c_str());
      is_success = false;
      break;
    }

    grpc::string test_name = it->substr(0, colon_pos);
    int weight = std::stoi(it->substr(colon_pos + 1));
    TestCaseType test_case = GetTestTypeFromName(test_name);
    if (test_case == UNKNOWN_TEST) {
      gpr_log(GPR_ERROR, "Unknown test case: %s", test_name.c_str());
      is_success = false;
      break;
    }

    tests.emplace_back(std::make_pair(test_case, weight));
  }

  return is_success;
}

// For debugging purposes
void LogParameterInfo(const std::vector<grpc::string>& addresses,
                      const std::vector<std::pair<TestCaseType, int>>& tests) {
  gpr_log(GPR_INFO, "server_addresses: %s", FLAGS_server_addresses.c_str());
  gpr_log(GPR_INFO, "test_cases : %s", FLAGS_test_cases.c_str());
  gpr_log(GPR_INFO, "sleep_duration_ms: %d", FLAGS_sleep_duration_ms);
  gpr_log(GPR_INFO, "test_duration_secs: %d", FLAGS_test_duration_secs);
  gpr_log(GPR_INFO, "num_channels_per_server: %d",
          FLAGS_num_channels_per_server);
  gpr_log(GPR_INFO, "num_stubs_per_channel: %d", FLAGS_num_stubs_per_channel);
  gpr_log(GPR_INFO, "log_level: %d", FLAGS_log_level);
  gpr_log(GPR_INFO, "do_not_abort_on_transient_failures: %s",
          FLAGS_do_not_abort_on_transient_failures ? "true" : "false");

  int num = 0;
  for (auto it = addresses.begin(); it != addresses.end(); it++) {
    gpr_log(GPR_INFO, "%d:%s", ++num, it->c_str());
  }

  num = 0;
  for (auto it = tests.begin(); it != tests.end(); it++) {
    TestCaseType test_case = it->first;
    int weight = it->second;
    gpr_log(GPR_INFO, "%d. TestCaseType: %d, Weight: %d", ++num, test_case,
            weight);
  }
}

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);

  if (FLAGS_log_level > GPR_LOG_SEVERITY_ERROR ||
      FLAGS_log_level < GPR_LOG_SEVERITY_DEBUG) {
    gpr_log(GPR_ERROR, "log_level should be an integer between %d and %d",
            GPR_LOG_SEVERITY_DEBUG, GPR_LOG_SEVERITY_ERROR);
    return 1;
  }

  // Change the default log function to TestLogFunction which respects the
  // log_level setting.
  log_level = FLAGS_log_level;
  gpr_set_log_function(TestLogFunction);

  srand(time(nullptr));

  // Parse the server addresses
  std::vector<grpc::string> server_addresses;
  ParseCommaDelimitedString(FLAGS_server_addresses, server_addresses);

  // Parse test cases and weights
  if (FLAGS_test_cases.length() == 0) {
    gpr_log(GPR_ERROR, "No test cases supplied");
    return 1;
  }

  std::vector<std::pair<TestCaseType, int>> tests;
  if (!ParseTestCasesString(FLAGS_test_cases, tests)) {
    gpr_log(GPR_ERROR, "Error in parsing test cases string %s ",
            FLAGS_test_cases.c_str());
    return 1;
  }

  LogParameterInfo(server_addresses, tests);

  WeightedRandomTestSelector test_selector(tests);
  MetricsServiceImpl metrics_service;

  gpr_log(GPR_INFO, "Starting test(s)..");

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
  for (auto it = server_addresses.begin(); it != server_addresses.end(); it++) {
    ++server_idx;
    // Create channel(s) for each server
    for (int channel_idx = 0; channel_idx < FLAGS_num_channels_per_server;
         channel_idx++) {
      gpr_log(GPR_INFO, "Starting test with %s channel_idx=%d..", it->c_str(),
              channel_idx);
      std::shared_ptr<grpc::Channel> channel = grpc::CreateTestChannel(
          *it, FLAGS_server_host_override, FLAGS_use_tls, !FLAGS_use_test_ca);

      // Create stub(s) for each channel
      for (int stub_idx = 0; stub_idx < FLAGS_num_stubs_per_channel;
           stub_idx++) {
        clients.emplace_back(new StressTestInteropClient(
            ++thread_idx, *it, channel, test_selector, FLAGS_test_duration_secs,
            FLAGS_sleep_duration_ms, FLAGS_do_not_abort_on_transient_failures));

        bool is_already_created = false;
        // QpsGauge name
        std::snprintf(buffer, sizeof(buffer),
                      "/stress_test/server_%d/channel_%d/stub_%d/qps",
                      server_idx, channel_idx, stub_idx);

        test_threads.emplace_back(std::thread(
            &StressTestInteropClient::MainLoop, clients.back().get(),
            metrics_service.CreateQpsGauge(buffer, &is_already_created)));

        // The QpsGauge should not have been already created
        GPR_ASSERT(!is_already_created);
      }
    }
  }

  // Start metrics server before waiting for the stress test threads
  std::unique_ptr<grpc::Server> metrics_server;
  if (FLAGS_metrics_port > 0) {
    metrics_server = metrics_service.StartServer(FLAGS_metrics_port);
  }

  // Wait for the stress test threads to complete
  for (auto it = test_threads.begin(); it != test_threads.end(); it++) {
    it->join();
  }

  return 0;
}
