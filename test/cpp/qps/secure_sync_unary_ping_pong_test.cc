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

#include <set>

#include <grpc/support/log.h>

#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/report.h"
#include "test/cpp/util/benchmark_config.h"

namespace grpc {
namespace testing {

static const int WARMUP = 5;
static const int BENCHMARK = 10;

static void RunSynchronousUnaryPingPong() {
  gpr_log(GPR_INFO, "Running Synchronous Unary Ping Pong");

  ClientConfig client_config;
  client_config.set_client_type(SYNC_CLIENT);
  client_config.set_outstanding_rpcs_per_channel(1);
  client_config.set_client_channels(1);
  client_config.set_rpc_type(UNARY);
  client_config.mutable_load_params()->mutable_closed_loop();

  ServerConfig server_config;
  server_config.set_server_type(SYNC_SERVER);

  // Set up security params
  SecurityParams security;
  security.set_use_test_ca(true);
  security.set_server_host_override("foo.test.google.fr");
  client_config.mutable_security_params()->CopyFrom(security);
  server_config.mutable_security_params()->CopyFrom(security);

  const auto result =
      RunScenario(client_config, 1, server_config, 1, WARMUP, BENCHMARK, -2);

  GetReporter()->ReportQPS(*result);
  GetReporter()->ReportLatency(*result);
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::InitBenchmark(&argc, &argv, true);

  grpc::testing::RunSynchronousUnaryPingPong();

  return 0;
}
