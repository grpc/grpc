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

#include <deque>
#include <list>
#include <thread>
#include <unordered_map>
#include <vector>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <gtest/gtest.h>

#include "src/core/lib/support/env.h"
#include "src/proto/grpc/testing/services.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/histogram.h"
#include "test/cpp/qps/qps_worker.h"
#include "test/cpp/qps/stats.h"

using std::list;
using std::thread;
using std::unique_ptr;
using std::deque;
using std::vector;

namespace grpc {
namespace testing {
static std::string get_host(const std::string& worker) {
  char* host;
  char* port;

  gpr_split_host_port(worker.c_str(), &host, &port);
  const string s(host);

  gpr_free(host);
  gpr_free(port);
  return s;
}

static std::unordered_map<string, std::deque<int>> get_hosts_and_cores(
    const deque<string>& workers) {
  std::unordered_map<string, std::deque<int>> hosts;
  for (auto it = workers.begin(); it != workers.end(); it++) {
    const string host = get_host(*it);
    if (hosts.find(host) == hosts.end()) {
      auto stub = WorkerService::NewStub(
          CreateChannel(*it, InsecureChannelCredentials()));
      grpc::ClientContext ctx;
      ctx.set_fail_fast(false);
      CoreRequest dummy;
      CoreResponse cores;
      grpc::Status s = stub->CoreCount(&ctx, dummy, &cores);
      assert(s.ok());
      std::deque<int> dq;
      for (int i = 0; i < cores.cores(); i++) {
        dq.push_back(i);
      }
      hosts[host] = dq;
    }
  }
  return hosts;
}

static deque<string> get_workers(const string& name) {
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

// helpers for postprocess_scenario_result
static double WallTime(ClientStats s) { return s.time_elapsed(); }
static double SystemTime(ClientStats s) { return s.time_system(); }
static double UserTime(ClientStats s) { return s.time_user(); }
static double ServerWallTime(ServerStats s) { return s.time_elapsed(); }
static double ServerSystemTime(ServerStats s) { return s.time_system(); }
static double ServerUserTime(ServerStats s) { return s.time_user(); }
static int Cores(int n) { return n; }

// Postprocess ScenarioResult and populate result summary.
static void postprocess_scenario_result(ScenarioResult* result) {
  Histogram histogram;
  histogram.MergeProto(result->latencies());

  auto qps = histogram.Count() / average(result->client_stats(), WallTime);
  auto qps_per_server_core = qps / sum(result->server_cores(), Cores);

  result->mutable_summary()->set_qps(qps);
  result->mutable_summary()->set_qps_per_server_core(qps_per_server_core);
  result->mutable_summary()->set_latency_50(histogram.Percentile(50));
  result->mutable_summary()->set_latency_90(histogram.Percentile(90));
  result->mutable_summary()->set_latency_95(histogram.Percentile(95));
  result->mutable_summary()->set_latency_99(histogram.Percentile(99));
  result->mutable_summary()->set_latency_999(histogram.Percentile(99.9));

  auto server_system_time = 100.0 *
                            sum(result->server_stats(), ServerSystemTime) /
                            sum(result->server_stats(), ServerWallTime);
  auto server_user_time = 100.0 * sum(result->server_stats(), ServerUserTime) /
                          sum(result->server_stats(), ServerWallTime);
  auto client_system_time = 100.0 * sum(result->client_stats(), SystemTime) /
                            sum(result->client_stats(), WallTime);
  auto client_user_time = 100.0 * sum(result->client_stats(), UserTime) /
                          sum(result->client_stats(), WallTime);

  result->mutable_summary()->set_server_system_time(server_system_time);
  result->mutable_summary()->set_server_user_time(server_user_time);
  result->mutable_summary()->set_client_system_time(client_system_time);
  result->mutable_summary()->set_client_user_time(client_user_time);
}

// Namespace for classes and functions used only in RunScenario
// Using this rather than local definitions to workaround gcc-4.4 limitations
// regarding using templates without linkage
namespace runsc {

// ClientContext allocator
static ClientContext* AllocContext(list<ClientContext>* contexts) {
  contexts->emplace_back();
  auto context = &contexts->back();
  context->set_fail_fast(false);
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
    const ServerConfig& initial_server_config, size_t num_servers,
    int warmup_seconds, int benchmark_seconds, int spawn_local_worker_count) {
  // ClientContext allocations (all are destroyed at scope exit)
  list<ClientContext> contexts;

  // To be added to the result, containing the final configuration used for
  // client and config (including host, etc.)
  ClientConfig result_client_config;
  const ServerConfig result_server_config = initial_server_config;

  // Get client, server lists
  auto workers = get_workers("QPS_WORKERS");
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

  // Setup the hosts and core counts
  auto hosts_cores = get_hosts_and_cores(workers);

  // if num_clients is set to <=0, do dynamic sizing: all workers
  // except for servers are clients
  if (num_clients <= 0) {
    num_clients = workers.size() - num_servers;
  }

  // TODO(ctiller): support running multiple configurations, and binpack
  // client/server pairs
  // to available workers
  GPR_ASSERT(workers.size() >= num_clients + num_servers);

  // Trim to just what we need
  workers.resize(num_clients + num_servers);

  // Start servers
  using runsc::ServerData;
  // servers is array rather than std::vector to avoid gcc-4.4 issues
  // where class contained in std::vector must have a copy constructor
  auto* servers = new ServerData[num_servers];
  for (size_t i = 0; i < num_servers; i++) {
    gpr_log(GPR_INFO, "Starting server on %s (worker #%d)", workers[i].c_str(),
            i);
    servers[i].stub = WorkerService::NewStub(
        CreateChannel(workers[i], InsecureChannelCredentials()));

    ServerConfig server_config = initial_server_config;
    char* host;
    char* driver_port;
    char* cli_target;
    gpr_split_host_port(workers[i].c_str(), &host, &driver_port);
    string host_str(host);
    int server_core_limit = initial_server_config.core_limit();
    int client_core_limit = initial_client_config.core_limit();

    if (server_core_limit == 0 && client_core_limit > 0) {
      // In this case, limit the server cores if it matches the
      // same host as one or more clients
      const auto& dq = hosts_cores.at(host_str);
      bool match = false;
      int limit = dq.size();
      for (size_t cli = 0; cli < num_clients; cli++) {
        if (host_str == get_host(workers[cli + num_servers])) {
          limit -= client_core_limit;
          match = true;
        }
      }
      if (match) {
        GPR_ASSERT(limit > 0);
        server_core_limit = limit;
      }
    }
    if (server_core_limit > 0) {
      auto& dq = hosts_cores.at(host_str);
      GPR_ASSERT(dq.size() >= static_cast<size_t>(server_core_limit));
      for (int core = 0; core < server_core_limit; core++) {
        server_config.add_core_list(dq.front());
        dq.pop_front();
      }
    }

    ServerArgs args;
    *args.mutable_setup() = server_config;
    servers[i].stream =
        servers[i].stub->RunServer(runsc::AllocContext(&contexts));
    GPR_ASSERT(servers[i].stream->Write(args));
    ServerStatus init_status;
    GPR_ASSERT(servers[i].stream->Read(&init_status));
    gpr_join_host_port(&cli_target, host, init_status.port());
    client_config.add_server_targets(cli_target);
    gpr_free(host);
    gpr_free(driver_port);
    gpr_free(cli_target);
  }

  // Targets are all set by now
  result_client_config = client_config;
  // Start clients
  using runsc::ClientData;
  // clients is array rather than std::vector to avoid gcc-4.4 issues
  // where class contained in std::vector must have a copy constructor
  auto* clients = new ClientData[num_clients];
  size_t channels_allocated = 0;
  for (size_t i = 0; i < num_clients; i++) {
    const auto& worker = workers[i + num_servers];
    gpr_log(GPR_INFO, "Starting client on %s (worker #%d)", worker.c_str(),
            i + num_servers);
    clients[i].stub = WorkerService::NewStub(
        CreateChannel(worker, InsecureChannelCredentials()));
    ClientConfig per_client_config = client_config;

    int server_core_limit = initial_server_config.core_limit();
    int client_core_limit = initial_client_config.core_limit();
    if ((server_core_limit > 0) || (client_core_limit > 0)) {
      auto& dq = hosts_cores.at(get_host(worker));
      if (client_core_limit == 0) {
        // limit client cores if it matches a server host
        bool match = false;
        int limit = dq.size();
        for (size_t srv = 0; srv < num_servers; srv++) {
          if (get_host(worker) == get_host(workers[srv])) {
            match = true;
          }
        }
        if (match) {
          GPR_ASSERT(limit > 0);
          client_core_limit = limit;
        }
      }
      if (client_core_limit > 0) {
        GPR_ASSERT(dq.size() >= static_cast<size_t>(client_core_limit));
        for (int core = 0; core < client_core_limit; core++) {
          per_client_config.add_core_list(dq.front());
          dq.pop_front();
        }
      }
    }

    // Reduce channel count so that total channels specified is held regardless
    // of the number of clients available
    size_t num_channels =
        (client_channels - channels_allocated) / (num_clients - i);
    channels_allocated += num_channels;
    per_client_config.set_client_channels(num_channels);

    ClientArgs args;
    *args.mutable_setup() = per_client_config;
    clients[i].stream =
        clients[i].stub->RunClient(runsc::AllocContext(&contexts));
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
      start,
      gpr_time_from_seconds(warmup_seconds + benchmark_seconds, GPR_TIMESPAN)));

  // Finish a run
  std::unique_ptr<ScenarioResult> result(new ScenarioResult);
  Histogram merged_latencies;

  gpr_log(GPR_INFO, "Finishing clients");
  for (auto client = &clients[0]; client != &clients[num_clients]; client++) {
    GPR_ASSERT(client->stream->Write(client_mark));
    GPR_ASSERT(client->stream->WritesDone());
  }
  for (auto client = &clients[0]; client != &clients[num_clients]; client++) {
    GPR_ASSERT(client->stream->Read(&client_status));
    const auto& stats = client_status.stats();
    merged_latencies.MergeProto(stats.latencies());
    result->add_client_stats()->CopyFrom(stats);
    GPR_ASSERT(!client->stream->Read(&client_status));
  }
  for (auto client = &clients[0]; client != &clients[num_clients]; client++) {
    GPR_ASSERT(client->stream->Finish().ok());
  }
  delete[] clients;

  merged_latencies.FillProto(result->mutable_latencies());

  gpr_log(GPR_INFO, "Finishing servers");
  for (auto server = &servers[0]; server != &servers[num_servers]; server++) {
    GPR_ASSERT(server->stream->Write(server_mark));
    GPR_ASSERT(server->stream->WritesDone());
  }
  for (auto server = &servers[0]; server != &servers[num_servers]; server++) {
    GPR_ASSERT(server->stream->Read(&server_status));
    result->add_server_stats()->CopyFrom(server_status.stats());
    result->add_server_cores(server_status.cores());
    GPR_ASSERT(!server->stream->Read(&server_status));
  }
  for (auto server = &servers[0]; server != &servers[num_servers]; server++) {
    GPR_ASSERT(server->stream->Finish().ok());
  }

  delete[] servers;

  postprocess_scenario_result(result.get());
  return result;
}

void RunQuit() {
  // Get client, server lists
  auto workers = get_workers("QPS_WORKERS");
  for (size_t i = 0; i < workers.size(); i++) {
    auto stub = WorkerService::NewStub(
        CreateChannel(workers[i], InsecureChannelCredentials()));
    Void dummy;
    grpc::ClientContext ctx;
    ctx.set_fail_fast(false);
    GPR_ASSERT(stub->QuitWorker(&ctx, dummy, &dummy).ok());
  }
}

}  // namespace testing
}  // namespace grpc
