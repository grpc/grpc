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
 *
 */

#include <cinttypes>
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
#include <grpc/support/string_util.h>

#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/env.h"
#include "src/proto/grpc/testing/services.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/histogram.h"
#include "test/cpp/qps/qps_worker.h"
#include "test/cpp/qps/stats.h"
#include "test/cpp/util/test_credentials_provider.h"

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

static deque<string> get_workers(const string& env_name) {
  char* env = gpr_getenv(env_name.c_str());
  if (!env) {
    env = gpr_strdup("");
  }
  deque<string> out;
  char* p = env;
  if (strlen(env) != 0) {
    for (;;) {
      char* comma = strchr(p, ',');
      if (comma) {
        out.emplace_back(p, comma);
        p = comma + 1;
      } else {
        out.emplace_back(p);
        break;
      }
    }
  }
  if (out.size() == 0) {
    gpr_log(GPR_ERROR,
            "Environment variable \"%s\" does not contain a list of QPS "
            "workers to use. Set it to a comma-separated list of "
            "hostname:port pairs, starting with hosts that should act as "
            "servers. E.g. export "
            "%s=\"serverhost1:1234,clienthost1:1234,clienthost2:1234\"",
            env_name.c_str(), env_name.c_str());
  }
  gpr_free(env);
  return out;
}

// helpers for postprocess_scenario_result
static double WallTime(ClientStats s) { return s.time_elapsed(); }
static double SystemTime(ClientStats s) { return s.time_system(); }
static double UserTime(ClientStats s) { return s.time_user(); }
static double CliPollCount(ClientStats s) { return s.cq_poll_count(); }
static double SvrPollCount(ServerStats s) { return s.cq_poll_count(); }
static double ServerWallTime(ServerStats s) { return s.time_elapsed(); }
static double ServerSystemTime(ServerStats s) { return s.time_system(); }
static double ServerUserTime(ServerStats s) { return s.time_user(); }
static double ServerTotalCpuTime(ServerStats s) { return s.total_cpu_time(); }
static double ServerIdleCpuTime(ServerStats s) { return s.idle_cpu_time(); }
static int Cores(int n) { return n; }

// Postprocess ScenarioResult and populate result summary.
static void postprocess_scenario_result(ScenarioResult* result) {
  Histogram histogram;
  histogram.MergeProto(result->latencies());

  auto time_estimate = average(result->client_stats(), WallTime);
  auto qps = histogram.Count() / time_estimate;
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

  // For Non-linux platform, get_cpu_usage() is not implemented. Thus,
  // ServerTotalCpuTime and ServerIdleCpuTime are both 0.
  if (average(result->server_stats(), ServerTotalCpuTime) == 0) {
    result->mutable_summary()->set_server_cpu_usage(0);
  } else {
    auto server_cpu_usage =
        100 -
        100 * average(result->server_stats(), ServerIdleCpuTime) /
            average(result->server_stats(), ServerTotalCpuTime);
    result->mutable_summary()->set_server_cpu_usage(server_cpu_usage);
  }

  if (result->request_results_size() > 0) {
    int64_t successes = 0;
    int64_t failures = 0;
    for (int i = 0; i < result->request_results_size(); i++) {
      RequestResultCount rrc = result->request_results(i);
      if (rrc.status_code() == 0) {
        successes += rrc.count();
      } else {
        failures += rrc.count();
      }
    }
    result->mutable_summary()->set_successful_requests_per_second(
        successes / time_estimate);
    result->mutable_summary()->set_failed_requests_per_second(failures /
                                                              time_estimate);
  }

  result->mutable_summary()->set_client_polls_per_request(
      sum(result->client_stats(), CliPollCount) / histogram.Count());
  result->mutable_summary()->set_server_polls_per_request(
      sum(result->server_stats(), SvrPollCount) / histogram.Count());

  auto server_queries_per_cpu_sec =
      histogram.Count() / (sum(result->server_stats(), ServerSystemTime) +
                           sum(result->server_stats(), ServerUserTime));
  auto client_queries_per_cpu_sec =
      histogram.Count() / (sum(result->client_stats(), SystemTime) +
                           sum(result->client_stats(), UserTime));

  result->mutable_summary()->set_server_queries_per_cpu_sec(
      server_queries_per_cpu_sec);
  result->mutable_summary()->set_client_queries_per_cpu_sec(
      client_queries_per_cpu_sec);
}

std::unique_ptr<ScenarioResult> RunScenario(
    const ClientConfig& initial_client_config, size_t num_clients,
    const ServerConfig& initial_server_config, size_t num_servers,
    int warmup_seconds, int benchmark_seconds, int spawn_local_worker_count,
    const grpc::string& qps_server_target_override,
    const grpc::string& credential_type) {
  // Log everything from the driver
  gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);

  // ClientContext allocations (all are destroyed at scope exit)
  list<ClientContext> contexts;
  auto alloc_context = [](list<ClientContext>* contexts) {
    contexts->emplace_back();
    auto context = &contexts->back();
    context->set_wait_for_ready(true);
    return context;
  };

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
    local_workers.emplace_back(new QpsWorker(driver_port, 0, credential_type));
    char addr[256];
    sprintf(addr, "localhost:%d", driver_port);
    if (spawn_local_worker_count < 0) {
      workers.push_front(addr);
    } else {
      workers.push_back(addr);
    }
  }
  GPR_ASSERT(workers.size() != 0);

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
  struct ServerData {
    unique_ptr<WorkerService::Stub> stub;
    unique_ptr<ClientReaderWriter<ServerArgs, ServerStatus>> stream;
  };
  std::vector<ServerData> servers(num_servers);
  std::unordered_map<string, std::deque<int>> hosts_cores;
  ChannelArguments channel_args;

  for (size_t i = 0; i < num_servers; i++) {
    gpr_log(GPR_INFO, "Starting server on %s (worker #%" PRIuPTR ")",
            workers[i].c_str(), i);
    servers[i].stub = WorkerService::NewStub(CreateChannel(
        workers[i], GetCredentialsProvider()->GetChannelCredentials(
                        credential_type, &channel_args)));

    ServerConfig server_config = initial_server_config;
    if (server_config.core_limit() != 0) {
      gpr_log(GPR_ERROR,
              "server config core limit is set but ignored by driver");
    }

    ServerArgs args;
    *args.mutable_setup() = server_config;
    servers[i].stream = servers[i].stub->RunServer(alloc_context(&contexts));
    if (!servers[i].stream->Write(args)) {
      gpr_log(GPR_ERROR, "Could not write args to server %zu", i);
    }
    ServerStatus init_status;
    if (!servers[i].stream->Read(&init_status)) {
      gpr_log(GPR_ERROR, "Server %zu did not yield initial status", i);
    }
    if (qps_server_target_override.length() > 0) {
      // overriding the qps server target only works if there is 1 server
      GPR_ASSERT(num_servers == 1);
      client_config.add_server_targets(qps_server_target_override);
    } else {
      std::string host;
      char* cli_target;
      host = get_host(workers[i]);
      gpr_join_host_port(&cli_target, host.c_str(), init_status.port());
      client_config.add_server_targets(cli_target);
      gpr_free(cli_target);
    }
  }

  // Targets are all set by now
  result_client_config = client_config;
  // Start clients
  struct ClientData {
    unique_ptr<WorkerService::Stub> stub;
    unique_ptr<ClientReaderWriter<ClientArgs, ClientStatus>> stream;
  };
  std::vector<ClientData> clients(num_clients);
  size_t channels_allocated = 0;
  for (size_t i = 0; i < num_clients; i++) {
    const auto& worker = workers[i + num_servers];
    gpr_log(GPR_INFO, "Starting client on %s (worker #%" PRIuPTR ")",
            worker.c_str(), i + num_servers);
    clients[i].stub = WorkerService::NewStub(
        CreateChannel(worker, GetCredentialsProvider()->GetChannelCredentials(
                                  credential_type, &channel_args)));
    ClientConfig per_client_config = client_config;

    if (initial_client_config.core_limit() != 0) {
      gpr_log(GPR_ERROR, "client config core limit set but ignored");
    }

    // Reduce channel count so that total channels specified is held regardless
    // of the number of clients available
    size_t num_channels =
        (client_config.client_channels() - channels_allocated) /
        (num_clients - i);
    channels_allocated += num_channels;
    gpr_log(GPR_DEBUG, "Client %" PRIdPTR " gets %" PRIdPTR " channels", i,
            num_channels);
    per_client_config.set_client_channels(num_channels);

    ClientArgs args;
    *args.mutable_setup() = per_client_config;
    clients[i].stream = clients[i].stub->RunClient(alloc_context(&contexts));
    if (!clients[i].stream->Write(args)) {
      gpr_log(GPR_ERROR, "Could not write args to client %zu", i);
    }
  }

  for (size_t i = 0; i < num_clients; i++) {
    ClientStatus init_status;
    if (!clients[i].stream->Read(&init_status)) {
      gpr_log(GPR_ERROR, "Client %zu did not yield initial status", i);
    }
  }

  // Send an initial mark: clients can use this to know that everything is ready
  // to start
  gpr_log(GPR_INFO, "Initiating");
  ServerArgs server_mark;
  server_mark.mutable_mark()->set_reset(true);
  ClientArgs client_mark;
  client_mark.mutable_mark()->set_reset(true);
  ServerStatus server_status;
  ClientStatus client_status;
  for (size_t i = 0; i < num_clients; i++) {
    auto client = &clients[i];
    if (!client->stream->Write(client_mark)) {
      gpr_log(GPR_ERROR, "Couldn't write mark to client %zu", i);
    }
  }
  for (size_t i = 0; i < num_clients; i++) {
    auto client = &clients[i];
    if (!client->stream->Read(&client_status)) {
      gpr_log(GPR_ERROR, "Couldn't get status from client %zu", i);
    }
  }

  // Let everything warmup
  gpr_log(GPR_INFO, "Warming up");
  gpr_timespec start = gpr_now(GPR_CLOCK_REALTIME);
  gpr_sleep_until(
      gpr_time_add(start, gpr_time_from_seconds(warmup_seconds, GPR_TIMESPAN)));

  // Start a run
  gpr_log(GPR_INFO, "Starting");
  for (size_t i = 0; i < num_servers; i++) {
    auto server = &servers[i];
    if (!server->stream->Write(server_mark)) {
      gpr_log(GPR_ERROR, "Couldn't write mark to server %zu", i);
    }
  }
  for (size_t i = 0; i < num_clients; i++) {
    auto client = &clients[i];
    if (!client->stream->Write(client_mark)) {
      gpr_log(GPR_ERROR, "Couldn't write mark to client %zu", i);
    }
  }
  for (size_t i = 0; i < num_servers; i++) {
    auto server = &servers[i];
    if (!server->stream->Read(&server_status)) {
      gpr_log(GPR_ERROR, "Couldn't get status from server %zu", i);
    }
  }
  for (size_t i = 0; i < num_clients; i++) {
    auto client = &clients[i];
    if (!client->stream->Read(&client_status)) {
      gpr_log(GPR_ERROR, "Couldn't get status from client %zu", i);
    }
  }

  // Wait some time
  gpr_log(GPR_INFO, "Running");
  // Use gpr_sleep_until rather than this_thread::sleep_until to support
  // compilers that don't work with this_thread
  gpr_sleep_until(gpr_time_add(
      start,
      gpr_time_from_seconds(warmup_seconds + benchmark_seconds, GPR_TIMESPAN)));

  gpr_timer_set_enabled(0);

  // Finish a run
  std::unique_ptr<ScenarioResult> result(new ScenarioResult);
  Histogram merged_latencies;
  std::unordered_map<int, int64_t> merged_statuses;

  gpr_log(GPR_INFO, "Finishing clients");
  for (size_t i = 0; i < num_clients; i++) {
    auto client = &clients[i];
    if (!client->stream->Write(client_mark)) {
      gpr_log(GPR_ERROR, "Couldn't write mark to client %zu", i);
    }
    if (!client->stream->WritesDone()) {
      gpr_log(GPR_ERROR, "Failed WritesDone for client %zu", i);
    }
  }
  for (size_t i = 0; i < num_clients; i++) {
    auto client = &clients[i];
    // Read the client final status
    if (client->stream->Read(&client_status)) {
      gpr_log(GPR_INFO, "Received final status from client %zu", i);
      const auto& stats = client_status.stats();
      merged_latencies.MergeProto(stats.latencies());
      for (int i = 0; i < stats.request_results_size(); i++) {
        merged_statuses[stats.request_results(i).status_code()] +=
            stats.request_results(i).count();
      }
      result->add_client_stats()->CopyFrom(stats);
      // That final status should be the last message on the client stream
      GPR_ASSERT(!client->stream->Read(&client_status));
    } else {
      gpr_log(GPR_ERROR, "Couldn't get final status from client %zu", i);
    }
  }
  for (size_t i = 0; i < num_clients; i++) {
    auto client = &clients[i];
    Status s = client->stream->Finish();
    result->add_client_success(s.ok());
    if (!s.ok()) {
      gpr_log(GPR_ERROR, "Client %zu had an error %s", i,
              s.error_message().c_str());
    }
  }

  merged_latencies.FillProto(result->mutable_latencies());
  for (std::unordered_map<int, int64_t>::iterator it = merged_statuses.begin();
       it != merged_statuses.end(); ++it) {
    RequestResultCount* rrc = result->add_request_results();
    rrc->set_status_code(it->first);
    rrc->set_count(it->second);
  }

  gpr_log(GPR_INFO, "Finishing servers");
  for (size_t i = 0; i < num_servers; i++) {
    auto server = &servers[i];
    if (!server->stream->Write(server_mark)) {
      gpr_log(GPR_ERROR, "Couldn't write mark to server %zu", i);
    }
    if (!server->stream->WritesDone()) {
      gpr_log(GPR_ERROR, "Failed WritesDone for server %zu", i);
    }
  }
  for (size_t i = 0; i < num_servers; i++) {
    auto server = &servers[i];
    // Read the server final status
    if (server->stream->Read(&server_status)) {
      gpr_log(GPR_INFO, "Received final status from server %zu", i);
      result->add_server_stats()->CopyFrom(server_status.stats());
      result->add_server_cores(server_status.cores());
      // That final status should be the last message on the server stream
      GPR_ASSERT(!server->stream->Read(&server_status));
    } else {
      gpr_log(GPR_ERROR, "Couldn't get final status from server %zu", i);
    }
  }
  for (size_t i = 0; i < num_servers; i++) {
    auto server = &servers[i];
    Status s = server->stream->Finish();
    result->add_server_success(s.ok());
    if (!s.ok()) {
      gpr_log(GPR_ERROR, "Server %zu had an error %s", i,
              s.error_message().c_str());
    }
  }

  postprocess_scenario_result(result.get());
  return result;
}

bool RunQuit(const grpc::string& credential_type) {
  // Get client, server lists
  bool result = true;
  auto workers = get_workers("QPS_WORKERS");
  if (workers.size() == 0) {
    return false;
  }

  ChannelArguments channel_args;
  for (size_t i = 0; i < workers.size(); i++) {
    auto stub = WorkerService::NewStub(CreateChannel(
        workers[i], GetCredentialsProvider()->GetChannelCredentials(
                        credential_type, &channel_args)));
    Void dummy;
    grpc::ClientContext ctx;
    ctx.set_wait_for_ready(true);
    Status s = stub->QuitWorker(&ctx, dummy, &dummy);
    if (!s.ok()) {
      gpr_log(GPR_ERROR, "Worker %zu could not be properly quit because %s", i,
              s.error_message().c_str());
      result = false;
    }
  }
  return result;
}

}  // namespace testing
}  // namespace grpc
