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

#include "test/cpp/qps/driver.h"
#include "src/core/support/env.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/host_port.h>
#include <grpc++/channel_arguments.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/stream.h>
#include <list>
#include <thread>
#include <vector>
#include "test/cpp/qps/histogram.h"

using std::list;
using std::thread;
using std::unique_ptr;
using std::vector;

namespace grpc {
namespace testing {
static vector<string> get_hosts(const string& name) {
  char* env = gpr_getenv(name.c_str());
  if (!env) return vector<string>();

  vector<string> out;
  char* p = env;
  for (;;) {
    char* comma = strchr(p, ',');
    if (comma) {
      out.emplace_back(p, comma);
      p = comma + 1;
    } else {
      out.emplace_back(p);
      gpr_free(env);
      return out;
    }
  }
}

ScenarioResult RunScenario(const ClientConfig& initial_client_config,
                           size_t num_clients,
                           const ServerConfig& server_config,
                           size_t num_servers) {
  // ClientContext allocator (all are destroyed at scope exit)
  list<ClientContext> contexts;
  auto alloc_context = [&contexts]() {
    contexts.emplace_back();
    return &contexts.back();
  };

  // Get client, server lists
  auto workers = get_hosts("QPS_WORKERS");
  ClientConfig client_config = initial_client_config;

  // TODO(ctiller): support running multiple configurations, and binpack
  // client/server pairs
  // to available workers
  GPR_ASSERT(workers.size() >= num_clients + num_servers);

  // Trim to just what we need
  workers.resize(num_clients + num_servers);

  // Start servers
  struct ServerData {
    unique_ptr<Worker::Stub> stub;
    unique_ptr<ClientReaderWriter<ServerArgs, ServerStatus>> stream;
  };
  vector<ServerData> servers;
  for (size_t i = 0; i < num_servers; i++) {
    ServerData sd;
    sd.stub = std::move(Worker::NewStub(
        CreateChannel(workers[i], InsecureCredentials(), ChannelArguments())));
    ServerArgs args;
    *args.mutable_setup() = server_config;
    sd.stream = std::move(sd.stub->RunServer(alloc_context()));
    GPR_ASSERT(sd.stream->Write(args));
    ServerStatus init_status;
    GPR_ASSERT(sd.stream->Read(&init_status));
    char* host;
    char* driver_port;
    char* cli_target;
    gpr_split_host_port(workers[i].c_str(), &host, &driver_port);
    gpr_join_host_port(&cli_target, host, init_status.port());
    client_config.add_server_targets(cli_target);
    gpr_free(host);
    gpr_free(driver_port);
    gpr_free(cli_target);

    servers.push_back(std::move(sd));
  }

  // Start clients
  struct ClientData {
    unique_ptr<Worker::Stub> stub;
    unique_ptr<ClientReaderWriter<ClientArgs, ClientStatus>> stream;
  };
  vector<ClientData> clients;
  for (size_t i = 0; i < num_clients; i++) {
    ClientData cd;
    cd.stub = std::move(Worker::NewStub(CreateChannel(
        workers[i + num_servers], InsecureCredentials(), ChannelArguments())));
    ClientArgs args;
    *args.mutable_setup() = client_config;
    cd.stream = std::move(cd.stub->RunTest(alloc_context()));
    GPR_ASSERT(cd.stream->Write(args));
    ClientStatus init_status;
    GPR_ASSERT(cd.stream->Read(&init_status));

    clients.push_back(std::move(cd));
  }

  // Let everything warmup
  gpr_log(GPR_INFO, "Warming up");
  gpr_timespec start = gpr_now();
  gpr_sleep_until(gpr_time_add(start, gpr_time_from_seconds(5)));

  // Start a run
  gpr_log(GPR_INFO, "Starting");
  ServerArgs server_mark;
  server_mark.mutable_mark();
  ClientArgs client_mark;
  client_mark.mutable_mark();
  for (auto& server : servers) {
    GPR_ASSERT(server.stream->Write(server_mark));
  }
  for (auto& client : clients) {
    GPR_ASSERT(client.stream->Write(client_mark));
  }
  ServerStatus server_status;
  ClientStatus client_status;
  for (auto& server : servers) {
    GPR_ASSERT(server.stream->Read(&server_status));
  }
  for (auto& client : clients) {
    GPR_ASSERT(client.stream->Read(&client_status));
  }

  // Wait some time
  gpr_log(GPR_INFO, "Running");
  gpr_sleep_until(gpr_time_add(start, gpr_time_from_seconds(15)));

  // Finish a run
  ScenarioResult result;
  gpr_log(GPR_INFO, "Finishing");
  for (auto& server : servers) {
    GPR_ASSERT(server.stream->Write(server_mark));
  }
  for (auto& client : clients) {
    GPR_ASSERT(client.stream->Write(client_mark));
  }
  for (auto& server : servers) {
    GPR_ASSERT(server.stream->Read(&server_status));
    const auto& stats = server_status.stats();
    result.server_resources.push_back(ResourceUsage{
        stats.time_elapsed(), stats.time_user(), stats.time_system()});
  }
  for (auto& client : clients) {
    GPR_ASSERT(client.stream->Read(&client_status));
    const auto& stats = client_status.stats();
    result.latencies.MergeProto(stats.latencies());
    result.client_resources.push_back(ResourceUsage{
        stats.time_elapsed(), stats.time_user(), stats.time_system()});
  }

  for (auto& client : clients) {
    GPR_ASSERT(client.stream->WritesDone());
    GPR_ASSERT(client.stream->Finish().IsOk());
  }
  for (auto& server : servers) {
    GPR_ASSERT(server.stream->WritesDone());
    GPR_ASSERT(server.stream->Finish().IsOk());
  }
  return result;
}
}  // namespace testing
}  // namespace grpc
