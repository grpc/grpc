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

#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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
DEFINE_string(rpc_types, "UNARY",
              "Comma-separated list of types of RPC: UNARY or STREAMING");

// Server config
DEFINE_string(server_threads, "1",
             "Comma-separated list of number of server threads");
DEFINE_string(
    server_types, "SYNCHRONOUS_SERVER",
    "Comma-separated list of server types: ASYNC_SERVER or SYNCHRONOUS_SERVER");

// Client config
DEFINE_string(outstanding_rpcs_per_channel, "1",
             "Comma-separated list of number of outstanding rpcs per channel");
DEFINE_string(client_channels, "1",
             "Comma-separated list of number of client channels");
DEFINE_string(payload_sizes, "1", "Comma-separated list of payload sizes");
DEFINE_string(
    client_types, "SYNCHRONOUS_CLIENT",
    "Comma-separated list of client types: ASYNC_CLIENT or SYNCHRONOUS_CLIENT");
DEFINE_string(async_client_threads, "1",
             "Comma-separated list of async client threads");

using grpc::testing::ClientConfig;
using grpc::testing::ServerConfig;
using grpc::testing::ClientType;
using grpc::testing::ServerType;
using grpc::testing::RpcType;
using grpc::testing::ResourceUsage;

using std::vector;
using std::string;

vector<string> ParseCommaSeparatedIntList(string list) {
  // Pass by value because strtok consumes its input.
  vector<string> result;
  char *p = strtok((char*)list.c_str(), ",");
  while (p) {
    result.push_back(p);
    p = strtok(NULL, ",");
  }
  return result;
}

vector<ClientConfig> GetClientConfigsFromFlags() {
  vector<ClientConfig> configs;
  const auto rpc_types = ParseCommaSeparatedIntList(FLAGS_rpc_types);
  const auto outstanding_rpcs_per_channel =
      ParseCommaSeparatedIntList(FLAGS_outstanding_rpcs_per_channel);
  const auto client_channels =
      ParseCommaSeparatedIntList(FLAGS_client_channels);
  const auto payload_sizes = ParseCommaSeparatedIntList(FLAGS_payload_sizes);
  const auto client_types = ParseCommaSeparatedIntList(FLAGS_client_types);
  const auto async_client_threads =
      ParseCommaSeparatedIntList(FLAGS_async_client_threads);

  ClientConfig cc;
  cc.set_enable_ssl(FLAGS_enable_ssl);

  for (const auto& rpc_type_str : rpc_types) {
    RpcType rpc_type;
    GPR_ASSERT(RpcType_Parse(rpc_type_str, &rpc_type));
    cc.set_rpc_type(rpc_type);
    for (const auto& out_rpcs_per_channel_str : outstanding_rpcs_per_channel) {
      const int out_rpcs_per_channel = atoi(out_rpcs_per_channel_str.c_str());
      cc.set_outstanding_rpcs_per_channel(out_rpcs_per_channel);
      for (const auto& client_channel_str : client_channels) {
        const int client_channel = atoi(client_channel_str.c_str());
        cc.set_client_channels(client_channel);
        for (const auto& payload_size_str : payload_sizes) {
          const int payload_size = atoi(payload_size_str.c_str());
          cc.set_payload_size(payload_size);
          for (const auto& client_type_str : client_types) {
            ClientType client_type;
            GPR_ASSERT(ClientType_Parse(client_type_str, &client_type));
            cc.set_client_type(client_type);
            for (const auto& async_client_thread_str : async_client_threads) {
              const int async_client_thread =
                  atoi(async_client_thread_str.c_str());
              cc.set_async_client_threads(async_client_thread);

              configs.push_back(cc);
            }
          }
        }
      }
    }
  }
  return configs;
}

vector<ServerConfig> GetServerConfigsFromFlags() {
  vector<ServerConfig> configs;
  const auto server_threads = ParseCommaSeparatedIntList(FLAGS_server_threads);
  const auto server_types = ParseCommaSeparatedIntList(FLAGS_server_types);

  ServerConfig sc;
  sc.set_enable_ssl(FLAGS_enable_ssl);

  for (const auto& server_type_str : server_types) {
    ServerType server_type;
    GPR_ASSERT(ServerType_Parse(server_type_str, &server_type));
    sc.set_server_type(server_type);
    for (const auto& server_thread_str : server_threads) {
      const int server_thread = atoi(server_thread_str.c_str());
      sc.set_threads(server_thread);

      configs.push_back(sc);
    }
  }
  return configs;
}

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);

  const auto client_configs = GetClientConfigsFromFlags();
  const auto server_configs = GetServerConfigsFromFlags();

  for (const ServerConfig& server_config : server_configs) {
    for (const ClientConfig& client_config : client_configs) {

      // If we're running a sync-server streaming test, make sure
      // that we have at least as many threads as the active streams
      // or else threads will be blocked from forward progress and the
      // client will deadlock on a timer.
      GPR_ASSERT(
          !(server_config.server_type() == grpc::testing::SYNCHRONOUS_SERVER &&
            client_config.rpc_type() == grpc::testing::STREAMING &&
            server_config.threads() <
                client_config.client_channels() *
                    client_config.outstanding_rpcs_per_channel()));

      gpr_log(GPR_INFO, "------------------");
      gpr_log(GPR_INFO, "ClientConfig: %s",
              client_config.ShortDebugString().c_str());
      gpr_log(GPR_INFO, "ServerConfig: %s",
              server_config.ShortDebugString().c_str());
      auto result = RunScenario(client_config, FLAGS_num_clients,
                                server_config, FLAGS_num_servers,
                                FLAGS_warmup_seconds, FLAGS_benchmark_seconds,
                                FLAGS_local_workers);

      ReportQPSPerCore(result, server_config);
      ReportLatency(result);
      ReportTimes(result);
    }
  }

  return 0;
}
