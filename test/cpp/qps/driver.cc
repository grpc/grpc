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

#include <list>
#include <thread>
#include <deque>
#include <vector>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/host_port.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>

#include "src/core/support/env.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/histogram.h"
#include "test/cpp/qps/qps_worker.h"
#include "test/proto/benchmarks/services.grpc.pb.h"

using std::list;
using std::thread;
using std::unique_ptr;
using std::deque;
using std::vector;

namespace grpc {
namespace testing {
static deque<string> get_hosts(const string& name) {
  char* env = gpr_getenv(name.c_str());
  if (!env) return deque<string>();

  deque<string> out;
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

// Namespace for classes and functions used only in RunScenario
// Using this rather than local definitions to workaround gcc-4.4 limitations
// regarding using templates without linkage
namespace runsc {

// ClientContext allocator
template <class T>
static ClientContext* AllocContext(list<ClientContext>* contexts, T deadline) {
  contexts->emplace_back();
  auto context = &contexts->back();
  context->set_deadline(deadline);
  return context;
}

struct ServerData {
  unique_ptr<WorkerService::Stub> stub;
  unique_ptr<ClientReaderWriter<ServerArgs, ServerStatus>> stream;
};

struct ClientData {
  unique_ptr<WorkerService::Stub> stub;
  unique_ptr<ClientReaderWriter<ClientArgs, ClientStatus>> stream;
};
}  // namespace runsc

std::unique_ptr<ScenarioResult> RunScenario(
    const ClientConfig& initial_client_config, size_t num_clients,
    const ServerConfig& server_config, size_t num_servers, int warmup_seconds,
    int benchmark_seconds, int spawn_local_worker_count) {
  // ClientContext allocations (all are destroyed at scope exit)
  list<ClientContext> contexts;

  // To be added to the result, containing the final configuration used for
  // client and config (incluiding host, etc.)
  ClientConfig result_client_config;
  ServerConfig result_server_config;

  // Get client, server lists
  auto workers = get_hosts("QPS_WORKERS");
  ClientConfig client_config = initial_client_config;

  // Spawn some local workers if desired
  vector<unique_ptr<QpsWorker>> local_workers;
  for (int i = 0; i < abs(spawn_local_worker_count); i++) {
    // act as if we're a new test -- gets a good rng seed
    static bool called_init = false;
    if (!called_init) {
      char args_buf[100];
      strcpy(args_buf, "some-benchmark");
      char* args[] = {args_buf};
      grpc_test_init(1, args);
      called_init = true;
    }

    int driver_port = grpc_pick_unused_port_or_die();
    local_workers.emplace_back(new QpsWorker(driver_port));
    char addr[256];
    sprintf(addr, "localhost:%d", driver_port);
    if (spawn_local_worker_count < 0) {
      workers.push_front(addr);
    } else {
      workers.push_back(addr);
    }
  }

  // TODO(ctiller): support running multiple configurations, and binpack
  // client/server pairs
  // to available workers
  GPR_ASSERT(workers.size() >= num_clients + num_servers);

  // Trim to just what we need
  workers.resize(num_clients + num_servers);

  gpr_timespec deadline =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                   gpr_time_from_seconds(
                       warmup_seconds + benchmark_seconds + 20, GPR_TIMESPAN));

  // Start servers
  using runsc::ServerData;
  // servers is array rather than std::vector to avoid gcc-4.4 issues
  // where class contained in std::vector must have a copy constructor
  auto* servers = new ServerData[num_servers];
  for (size_t i = 0; i < num_servers; i++) {
    servers[i].stub = WorkerService::NewStub(
        CreateChannel(workers[i], InsecureCredentials()));
    ServerArgs args;
    result_server_config = server_config;
    *args.mutable_setup() = server_config;
    servers[i].stream =
        servers[i].stub->RunServer(runsc::AllocContext(&contexts, deadline));
    GPR_ASSERT(servers[i].stream->Write(args));
    ServerStatus init_status;
    GPR_ASSERT(servers[i].stream->Read(&init_status));
    char* host;
    char* driver_port;
    char* cli_target;
    gpr_split_host_port(workers[i].c_str(), &host, &driver_port);
    gpr_join_host_port(&cli_target, host, init_status.port());
    client_config.add_server_targets(cli_target);
    gpr_free(host);
    gpr_free(driver_port);
    gpr_free(cli_target);
  }

  // Start clients
  using runsc::ClientData;
  // clients is array rather than std::vector to avoid gcc-4.4 issues
  // where class contained in std::vector must have a copy constructor
  auto* clients = new ClientData[num_clients];
  for (size_t i = 0; i < num_clients; i++) {
    clients[i].stub = WorkerService::NewStub(
        CreateChannel(workers[i + num_servers], InsecureCredentials()));
    ClientArgs args;
    result_client_config = client_config;
    *args.mutable_setup() = client_config;
    clients[i].stream =
        clients[i].stub->RunClient(runsc::AllocContext(&contexts, deadline));
    GPR_ASSERT(clients[i].stream->Write(args));
    ClientStatus init_status;
    GPR_ASSERT(clients[i].stream->Read(&init_status));
  }

  // Let everything warmup
  gpr_log(GPR_INFO, "Warming up");
  gpr_timespec start = gpr_now(GPR_CLOCK_REALTIME);
  gpr_sleep_until(
      gpr_time_add(start, gpr_time_from_seconds(warmup_seconds, GPR_TIMESPAN)));

  // Start a run
  gpr_log(GPR_INFO, "Starting");
  ServerArgs server_mark;
  server_mark.mutable_mark()->set_reset(true);
  ClientArgs client_mark;
  client_mark.mutable_mark()->set_reset(true);
  for (auto server = &servers[0]; server != &servers[num_servers]; server++) {
    GPR_ASSERT(server->stream->Write(server_mark));
  }
  for (auto client = &clients[0]; client != &clients[num_clients]; client++) {
    GPR_ASSERT(client->stream->Write(client_mark));
  }
  ServerStatus server_status;
  ClientStatus client_status;
  for (auto server = &servers[0]; server != &servers[num_servers]; server++) {
    GPR_ASSERT(server->stream->Read(&server_status));
  }
  for (auto client = &clients[0]; client != &clients[num_clients]; client++) {
    GPR_ASSERT(client->stream->Read(&client_status));
  }

  // Wait some time
  gpr_log(GPR_INFO, "Running");
  // Use gpr_sleep_until rather than this_thread::sleep_until to support
  // compilers that don't work with this_thread
  gpr_sleep_until(gpr_time_add(
      start, gpr_time_from_seconds(benchmark_seconds, GPR_TIMESPAN)));

  // Finish a run
  std::unique_ptr<ScenarioResult> result(new ScenarioResult);
  result->client_config = result_client_config;
  result->server_config = result_server_config;
  gpr_log(GPR_INFO, "Finishing");
  for (auto server = &servers[0]; server != &servers[num_servers]; server++) {
    GPR_ASSERT(server->stream->Write(server_mark));
  }
  for (auto client = &clients[0]; client != &clients[num_clients]; client++) {
    GPR_ASSERT(client->stream->Write(client_mark));
  }
  for (auto server = &servers[0]; server != &servers[num_servers]; server++) {
    GPR_ASSERT(server->stream->Read(&server_status));
    const auto& stats = server_status.stats();
    result->server_resources.emplace_back(
        stats.time_elapsed(), stats.time_user(), stats.time_system(),
        server_status.cores());
  }
  for (auto client = &clients[0]; client != &clients[num_clients]; client++) {
    GPR_ASSERT(client->stream->Read(&client_status));
    const auto& stats = client_status.stats();
    result->latencies.MergeProto(stats.latencies());
    result->client_resources.emplace_back(
        stats.time_elapsed(), stats.time_user(), stats.time_system(), -1);
  }

  for (auto client = &clients[0]; client != &clients[num_clients]; client++) {
    GPR_ASSERT(client->stream->WritesDone());
    GPR_ASSERT(client->stream->Finish().ok());
  }
  for (auto server = &servers[0]; server != &servers[num_servers]; server++) {
    GPR_ASSERT(server->stream->WritesDone());
    GPR_ASSERT(server->stream->Finish().ok());
  }
  delete[] clients;
  delete[] servers;
  return result;
}
}  // namespace testing
}  // namespace grpc
