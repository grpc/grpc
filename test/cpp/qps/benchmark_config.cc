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
 *
 */

#include "test/cpp/qps/benchmark_config.h"
#include <gflags/gflags.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>
#include <grpc/support/log.h>

DEFINE_bool(enable_log_reporter, true,
            "Enable reporting of benchmark results through GprLog");

DEFINE_string(scenario_result_file, "",
              "Write JSON benchmark report to the file specified.");

DEFINE_string(hashed_id, "", "Hash of the user id");

DEFINE_string(test_name, "", "Name of the test being executed");

DEFINE_string(sys_info, "", "System information");

DEFINE_string(server_address, "localhost:50052",
              "Address of the performance database server");

DEFINE_string(tag, "", "Optional tag for the test");

DEFINE_string(rpc_reporter_server_address, "",
              "Server address for rpc reporter to send results to");

DEFINE_bool(enable_rpc_reporter, false, "Enable use of RPC reporter");

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

namespace grpc {
namespace testing {

void InitBenchmark(int* argc, char*** argv, bool remove_flags) {
  ParseCommandLineFlags(argc, argv, remove_flags);
}

static std::shared_ptr<Reporter> InitBenchmarkReporters() {
  auto* composite_reporter = new CompositeReporter;
  if (FLAGS_enable_log_reporter) {
    composite_reporter->add(
        std::unique_ptr<Reporter>(new GprLogReporter("LogReporter")));
  }
  if (FLAGS_scenario_result_file != "") {
    composite_reporter->add(std::unique_ptr<Reporter>(
        new JsonReporter("JsonReporter", FLAGS_scenario_result_file)));
  }
  if (FLAGS_enable_rpc_reporter) {
    GPR_ASSERT(!FLAGS_rpc_reporter_server_address.empty());
    composite_reporter->add(std::unique_ptr<Reporter>(new RpcReporter(
        "RpcReporter",
        grpc::CreateChannel(FLAGS_rpc_reporter_server_address,
                            grpc::InsecureChannelCredentials()))));
  }

  return std::shared_ptr<Reporter>(composite_reporter);
}

std::shared_ptr<Reporter> GetReporter() {
  static std::shared_ptr<Reporter> reporter(InitBenchmarkReporters());
  return reporter;
}

}  // namespace testing
}  // namespace grpc
