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

#include <memory>
#include <set>
#include <signal.h>

#include <gflags/gflags.h>
#include <grpc/support/log.h>

#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/report.h"
#include "test/cpp/util/benchmark_config.h"

DEFINE_int32(num_clients, 1, "Number of client binaries");
DEFINE_int32(num_servers, 1, "Number of server binaries");

DEFINE_int32(warmup_seconds, 5, "Warmup time (in seconds)");
DEFINE_int32(benchmark_seconds, 30, "Benchmark time (in seconds)");
DEFINE_int32(local_workers, 0, "Number of local workers to start");

// Common config
DEFINE_string(rpc_type, "UNARY", "Type of RPC: UNARY or STREAMING");

// Server config
DEFINE_int32(async_server_threads, 1, "Number of threads for async servers");
DEFINE_string(server_type, "SYNC_SERVER", "Server type");

// Client config
DEFINE_int32(outstanding_rpcs_per_channel, 1,
             "Number of outstanding rpcs per channel");
DEFINE_int32(client_channels, 1, "Number of client channels");

DEFINE_int32(simple_req_size, -1, "Simple proto request payload size");
DEFINE_int32(simple_resp_size, -1, "Simple proto response payload size");

DEFINE_string(client_type, "SYNC_CLIENT", "Client type");
DEFINE_int32(async_client_threads, 1, "Async client threads");

DEFINE_double(poisson_load, -1.0, "Poisson offered load (qps)");
DEFINE_double(uniform_lo, -1.0, "Uniform low interarrival time (us)");
DEFINE_double(uniform_hi, -1.0, "Uniform high interarrival time (us)");
DEFINE_double(determ_load, -1.0, "Deterministic offered load (qps)");
DEFINE_double(pareto_base, -1.0, "Pareto base interarrival time (us)");
DEFINE_double(pareto_alpha, -1.0, "Pareto alpha value");

using grpc::testing::ClientConfig;
using grpc::testing::ServerConfig;
using grpc::testing::ClientType;
using grpc::testing::ServerType;
using grpc::testing::RpcType;
using grpc::testing::ResourceUsage;

namespace grpc {
namespace testing {

static void QpsDriver() {
  RpcType rpc_type;
  GPR_ASSERT(RpcType_Parse(FLAGS_rpc_type, &rpc_type));

  ClientType client_type;
  ServerType server_type;
  GPR_ASSERT(ClientType_Parse(FLAGS_client_type, &client_type));
  GPR_ASSERT(ServerType_Parse(FLAGS_server_type, &server_type));

  ClientConfig client_config;
  client_config.set_client_type(client_type);
  client_config.set_outstanding_rpcs_per_channel(
      FLAGS_outstanding_rpcs_per_channel);
  client_config.set_client_channels(FLAGS_client_channels);

  // Decide which type to use based on the response type
  if (FLAGS_simple_resp_size >= 0) {
    auto params =
        client_config.mutable_payload_config()->mutable_simple_params();
    params->set_resp_size(FLAGS_simple_resp_size);
    if (FLAGS_simple_req_size >= 0) {
      params->set_req_size(FLAGS_simple_req_size);
    }
  } else {
    // choose a reasonable default
    auto params =
        client_config.mutable_payload_config()->mutable_simple_params();
    params->set_resp_size(1);
  }

  client_config.set_async_client_threads(FLAGS_async_client_threads);
  client_config.set_rpc_type(rpc_type);

  // set up the load parameters
  if (FLAGS_poisson_load > 0.0) {
    auto poisson = client_config.mutable_load_params()->mutable_poisson();
    poisson->set_offered_load(FLAGS_poisson_load);
  } else if (FLAGS_uniform_lo > 0.0) {
    auto uniform = client_config.mutable_load_params()->mutable_uniform();
    uniform->set_interarrival_lo(FLAGS_uniform_lo / 1e6);
    uniform->set_interarrival_hi(FLAGS_uniform_hi / 1e6);
  } else if (FLAGS_determ_load > 0.0) {
    auto determ = client_config.mutable_load_params()->mutable_determ();
    determ->set_offered_load(FLAGS_determ_load);
  } else if (FLAGS_pareto_base > 0.0) {
    auto pareto = client_config.mutable_load_params()->mutable_pareto();
    pareto->set_interarrival_base(FLAGS_pareto_base / 1e6);
    pareto->set_alpha(FLAGS_pareto_alpha);
  } else {
    client_config.mutable_load_params()->mutable_closed_loop();
    // No further load parameters to set up for closed loop
  }

  ServerConfig server_config;
  server_config.set_server_type(server_type);
  server_config.set_async_server_threads(FLAGS_async_server_threads);

  const auto result = RunScenario(
      client_config, FLAGS_num_clients, server_config, FLAGS_num_servers,
      FLAGS_warmup_seconds, FLAGS_benchmark_seconds, FLAGS_local_workers);

  GetReporter()->ReportQPS(*result);
  GetReporter()->ReportQPSPerCore(*result);
  GetReporter()->ReportLatency(*result);
  GetReporter()->ReportTimes(*result);
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::InitBenchmark(&argc, &argv, true);

  signal(SIGPIPE, SIG_IGN);
  grpc::testing::QpsDriver();

  return 0;
}
