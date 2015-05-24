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

#include <gflags/gflags.h>
#include <grpc/support/log.h>

#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/report.h"
#include "test/cpp/util/test_config.h"

DEFINE_int32(num_clients, 1, "Number of client binaries");
DEFINE_int32(num_servers, 1, "Number of server binaries");

DEFINE_int32(warmup_seconds, 5, "Warmup time (in seconds)");
DEFINE_int32(benchmark_seconds, 30, "Benchmark time (in seconds)");
DEFINE_int32(local_workers, 0, "Number of local workers to start");

// Common config
DEFINE_bool(enable_ssl, false, "Use SSL");
DEFINE_string(rpc_type, "UNARY", "Type of RPC: UNARY or STREAMING");

// Server config
DEFINE_int32(server_threads, 1, "Number of server threads");
DEFINE_string(server_type, "SYNCHRONOUS_SERVER", "Server type");

// Client config
DEFINE_int32(outstanding_rpcs_per_channel, 1,
             "Number of outstanding rpcs per channel");
DEFINE_int32(client_channels, 1, "Number of client channels");
DEFINE_int32(payload_size, 1, "Payload size");
DEFINE_string(client_type, "SYNCHRONOUS_CLIENT", "Client type");
DEFINE_int32(async_client_threads, 1, "Async client threads");

using grpc::testing::ClientConfig;
using grpc::testing::ServerConfig;
using grpc::testing::ClientType;
using grpc::testing::ServerType;
using grpc::testing::RpcType;
using grpc::testing::ResourceUsage;

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);

  RpcType rpc_type;
  GPR_ASSERT(RpcType_Parse(FLAGS_rpc_type, &rpc_type));

  ClientType client_type;
  ServerType server_type;
  GPR_ASSERT(ClientType_Parse(FLAGS_client_type, &client_type));
  GPR_ASSERT(ServerType_Parse(FLAGS_server_type, &server_type));

  ClientConfig client_config;
  client_config.set_client_type(client_type);
  client_config.set_enable_ssl(FLAGS_enable_ssl);
  client_config.set_outstanding_rpcs_per_channel(
      FLAGS_outstanding_rpcs_per_channel);
  client_config.set_client_channels(FLAGS_client_channels);
  client_config.set_payload_size(FLAGS_payload_size);
  client_config.set_async_client_threads(FLAGS_async_client_threads);
  client_config.set_rpc_type(rpc_type);

  ServerConfig server_config;
  server_config.set_server_type(server_type);
  server_config.set_threads(FLAGS_server_threads);
  server_config.set_enable_ssl(FLAGS_enable_ssl);

  // If we're running a sync-server streaming test, make sure
  // that we have at least as many threads as the active streams
  // or else threads will be blocked from forward progress and the
  // client will deadlock on a timer.
  GPR_ASSERT(!(server_type == grpc::testing::SYNCHRONOUS_SERVER &&
               rpc_type == grpc::testing::STREAMING &&
               FLAGS_server_threads <
                   FLAGS_client_channels * FLAGS_outstanding_rpcs_per_channel));

  const auto result = RunScenario(
      client_config, FLAGS_num_clients, server_config, FLAGS_num_servers,
      FLAGS_warmup_seconds, FLAGS_benchmark_seconds, FLAGS_local_workers);

  ReportQPSPerCore(*result, server_config);
  ReportLatency(*result);
  ReportTimes(*result);

  return 0;
}
