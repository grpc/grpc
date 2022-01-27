/*
 *
 * Copyright 2020 gRPC authors.
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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_split.h"

#include <grpcpp/ext/admin_services.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/env.h"
#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(bool, fail_on_failed_rpc, false,
          "Fail client if any RPCs fail after first successful RPC.");
ABSL_FLAG(int32_t, num_channels, 1, "Number of channels.");
ABSL_FLAG(bool, print_response, false, "Write RPC response to stdout.");
ABSL_FLAG(int32_t, qps, 1, "Qps per channel.");
// TODO(Capstan): Consider using absl::Duration
ABSL_FLAG(int32_t, rpc_timeout_sec, 30, "Per RPC timeout seconds.");
ABSL_FLAG(std::string, server, "localhost:50051", "Address of server.");
ABSL_FLAG(int32_t, stats_port, 50052,
          "Port to expose peer distribution stats service.");
ABSL_FLAG(std::string, rpc, "UnaryCall",
          "a comma separated list of rpc methods.");
ABSL_FLAG(std::string, metadata, "", "metadata to send with the RPC.");
ABSL_FLAG(std::string, expect_status, "OK",
          "RPC status for the test RPC to be considered successful");
ABSL_FLAG(
    bool, secure_mode, false,
    "If true, XdsCredentials are used, InsecureChannelCredentials otherwise");

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::testing::ClientConfigureRequest;
using grpc::testing::ClientConfigureRequest_RpcType_Name;
using grpc::testing::ClientConfigureResponse;
using grpc::testing::Empty;
using grpc::testing::LoadBalancerAccumulatedStatsRequest;
using grpc::testing::LoadBalancerAccumulatedStatsResponse;
using grpc::testing::LoadBalancerStatsRequest;
using grpc::testing::LoadBalancerStatsResponse;
using grpc::testing::LoadBalancerStatsService;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::TestService;
using grpc::testing::XdsUpdateClientConfigureService;

class XdsStatsWatcher;

struct StatsWatchers {
  // Unique ID for each outgoing RPC
  int global_request_id = 0;
  // Unique ID for each outgoing RPC by RPC method type
  std::map<int, int> global_request_id_by_type;
  // Stores a set of watchers that should be notified upon outgoing RPC
  // completion
  std::set<XdsStatsWatcher*> watchers;
  // Global watcher for accumululated stats.
  XdsStatsWatcher* global_watcher;
  // Mutex for global_request_id and watchers
  std::mutex mu;
};
// Whether at least one RPC has succeeded, indicating xDS resolution completed.
std::atomic<bool> one_rpc_succeeded(false);
// RPC configuration detailing how RPC should be sent.
struct RpcConfig {
  ClientConfigureRequest::RpcType type;
  std::vector<std::pair<std::string, std::string>> metadata;
  int timeout_sec = 0;
};
struct RpcConfigurationsQueue {
  // A queue of RPC configurations detailing how RPCs should be sent.
  std::deque<std::vector<RpcConfig>> rpc_configs_queue;
  // Mutex for rpc_configs_queue
  std::mutex mu_rpc_configs_queue;
};
struct AsyncClientCall {
  Empty empty_response;
  SimpleResponse simple_response;
  ClientContext context;
  Status status;
  int saved_request_id;
  ClientConfigureRequest::RpcType rpc_type;
  std::unique_ptr<ClientAsyncResponseReader<Empty>> empty_response_reader;
  std::unique_ptr<ClientAsyncResponseReader<SimpleResponse>>
      simple_response_reader;
};

/** Records the remote peer distribution for a given range of RPCs. */
class XdsStatsWatcher {
 public:
  XdsStatsWatcher(int start_id, int end_id)
      : start_id_(start_id), end_id_(end_id), rpcs_needed_(end_id - start_id) {}

  // Upon the completion of an RPC, we will look at the request_id, the
  // rpc_type, and the peer the RPC was sent to in order to count
  // this RPC into the right stats bin.
  void RpcCompleted(AsyncClientCall* call, const std::string& peer) {
    // We count RPCs for global watcher or if the request_id falls into the
    // watcher's interested range of request ids.
    if ((start_id_ == 0 && end_id_ == 0) ||
        (start_id_ <= call->saved_request_id &&
         call->saved_request_id < end_id_)) {
      {
        std::lock_guard<std::mutex> lock(m_);
        if (peer.empty()) {
          no_remote_peer_++;
          ++no_remote_peer_by_type_[call->rpc_type];
        } else {
          // RPC is counted into both per-peer bin and per-method-per-peer bin.
          rpcs_by_peer_[peer]++;
          rpcs_by_type_[call->rpc_type][peer]++;
        }
        rpcs_needed_--;
        // Report accumulated stats.
        auto& stats_per_method = *accumulated_stats_.mutable_stats_per_method();
        auto& method_stat =
            stats_per_method[ClientConfigureRequest_RpcType_Name(
                call->rpc_type)];
        auto& result = *method_stat.mutable_result();
        grpc_status_code code =
            static_cast<grpc_status_code>(call->status.error_code());
        auto& num_rpcs = result[code];
        ++num_rpcs;
        auto rpcs_started = method_stat.rpcs_started();
        method_stat.set_rpcs_started(++rpcs_started);
      }
      cv_.notify_one();
    }
  }

  void WaitForRpcStatsResponse(LoadBalancerStatsResponse* response,
                               int timeout_sec) {
    std::unique_lock<std::mutex> lock(m_);
    cv_.wait_for(lock, std::chrono::seconds(timeout_sec),
                 [this] { return rpcs_needed_ == 0; });
    response->mutable_rpcs_by_peer()->insert(rpcs_by_peer_.begin(),
                                             rpcs_by_peer_.end());
    auto& response_rpcs_by_method = *response->mutable_rpcs_by_method();
    for (const auto& rpc_by_type : rpcs_by_type_) {
      std::string method_name;
      if (rpc_by_type.first == ClientConfigureRequest::EMPTY_CALL) {
        method_name = "EmptyCall";
      } else if (rpc_by_type.first == ClientConfigureRequest::UNARY_CALL) {
        method_name = "UnaryCall";
      } else {
        GPR_ASSERT(0);
      }
      // TODO(@donnadionne): When the test runner changes to accept EMPTY_CALL
      // and UNARY_CALL we will just use the name of the enum instead of the
      // method_name variable.
      auto& response_rpc_by_method = response_rpcs_by_method[method_name];
      auto& response_rpcs_by_peer =
          *response_rpc_by_method.mutable_rpcs_by_peer();
      for (const auto& rpc_by_peer : rpc_by_type.second) {
        auto& response_rpc_by_peer = response_rpcs_by_peer[rpc_by_peer.first];
        response_rpc_by_peer = rpc_by_peer.second;
      }
    }
    response->set_num_failures(no_remote_peer_ + rpcs_needed_);
  }

  void GetCurrentRpcStats(LoadBalancerAccumulatedStatsResponse* response,
                          StatsWatchers* stats_watchers) {
    std::unique_lock<std::mutex> lock(m_);
    response->CopyFrom(accumulated_stats_);
    // TODO(@donnadionne): delete deprecated stats below when the test is no
    // longer using them.
    auto& response_rpcs_started_by_method =
        *response->mutable_num_rpcs_started_by_method();
    auto& response_rpcs_succeeded_by_method =
        *response->mutable_num_rpcs_succeeded_by_method();
    auto& response_rpcs_failed_by_method =
        *response->mutable_num_rpcs_failed_by_method();
    for (const auto& rpc_by_type : rpcs_by_type_) {
      auto total_succeeded = 0;
      for (const auto& rpc_by_peer : rpc_by_type.second) {
        total_succeeded += rpc_by_peer.second;
      }
      response_rpcs_succeeded_by_method[ClientConfigureRequest_RpcType_Name(
          rpc_by_type.first)] = total_succeeded;
      response_rpcs_started_by_method[ClientConfigureRequest_RpcType_Name(
          rpc_by_type.first)] =
          stats_watchers->global_request_id_by_type[rpc_by_type.first];
      response_rpcs_failed_by_method[ClientConfigureRequest_RpcType_Name(
          rpc_by_type.first)] = no_remote_peer_by_type_[rpc_by_type.first];
    }
  }

 private:
  int start_id_;
  int end_id_;
  int rpcs_needed_;
  int no_remote_peer_ = 0;
  std::map<int, int> no_remote_peer_by_type_;
  // A map of stats keyed by peer name.
  std::map<std::string, int> rpcs_by_peer_;
  // A two-level map of stats keyed at top level by RPC method and second level
  // by peer name.
  std::map<int, std::map<std::string, int>> rpcs_by_type_;
  // Storing accumulated stats in the response proto format.
  LoadBalancerAccumulatedStatsResponse accumulated_stats_;
  std::mutex m_;
  std::condition_variable cv_;
};

class TestClient {
 public:
  TestClient(const std::shared_ptr<Channel>& channel,
             StatsWatchers* stats_watchers)
      : stub_(TestService::NewStub(channel)), stats_watchers_(stats_watchers) {}

  void AsyncUnaryCall(const RpcConfig& config) {
    SimpleResponse response;
    int saved_request_id;
    {
      std::lock_guard<std::mutex> lock(stats_watchers_->mu);
      saved_request_id = ++stats_watchers_->global_request_id;
      ++stats_watchers_
            ->global_request_id_by_type[ClientConfigureRequest::UNARY_CALL];
    }
    std::chrono::system_clock::time_point deadline =
        std::chrono::system_clock::now() +
        std::chrono::seconds(config.timeout_sec != 0
                                 ? config.timeout_sec
                                 : absl::GetFlag(FLAGS_rpc_timeout_sec));
    AsyncClientCall* call = new AsyncClientCall;
    for (const auto& data : config.metadata) {
      call->context.AddMetadata(data.first, data.second);
      // TODO(@donnadionne): move deadline to separate proto.
      if (data.first == "rpc-behavior" && data.second == "keep-open") {
        deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(INT_MAX);
      }
    }
    call->context.set_deadline(deadline);
    call->saved_request_id = saved_request_id;
    call->rpc_type = ClientConfigureRequest::UNARY_CALL;
    call->simple_response_reader = stub_->PrepareAsyncUnaryCall(
        &call->context, SimpleRequest::default_instance(), &cq_);
    call->simple_response_reader->StartCall();
    call->simple_response_reader->Finish(&call->simple_response, &call->status,
                                         call);
  }

  void AsyncEmptyCall(const RpcConfig& config) {
    Empty response;
    int saved_request_id;
    {
      std::lock_guard<std::mutex> lock(stats_watchers_->mu);
      saved_request_id = ++stats_watchers_->global_request_id;
      ++stats_watchers_
            ->global_request_id_by_type[ClientConfigureRequest::EMPTY_CALL];
    }
    std::chrono::system_clock::time_point deadline =
        std::chrono::system_clock::now() +
        std::chrono::seconds(config.timeout_sec != 0
                                 ? config.timeout_sec
                                 : absl::GetFlag(FLAGS_rpc_timeout_sec));
    AsyncClientCall* call = new AsyncClientCall;
    for (const auto& data : config.metadata) {
      call->context.AddMetadata(data.first, data.second);
      // TODO(@donnadionne): move deadline to separate proto.
      if (data.first == "rpc-behavior" && data.second == "keep-open") {
        deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(INT_MAX);
      }
    }
    call->context.set_deadline(deadline);
    call->saved_request_id = saved_request_id;
    call->rpc_type = ClientConfigureRequest::EMPTY_CALL;
    call->empty_response_reader = stub_->PrepareAsyncEmptyCall(
        &call->context, Empty::default_instance(), &cq_);
    call->empty_response_reader->StartCall();
    call->empty_response_reader->Finish(&call->empty_response, &call->status,
                                        call);
  }

  void AsyncCompleteRpc() {
    void* got_tag;
    bool ok = false;
    while (cq_.Next(&got_tag, &ok)) {
      AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);
      GPR_ASSERT(ok);
      {
        std::lock_guard<std::mutex> lock(stats_watchers_->mu);
        auto server_initial_metadata = call->context.GetServerInitialMetadata();
        auto metadata_hostname =
            call->context.GetServerInitialMetadata().find("hostname");
        std::string hostname =
            metadata_hostname != call->context.GetServerInitialMetadata().end()
                ? std::string(metadata_hostname->second.data(),
                              metadata_hostname->second.length())
                : call->simple_response.hostname();
        for (auto watcher : stats_watchers_->watchers) {
          watcher->RpcCompleted(call, hostname);
        }
      }

      if (!RpcStatusCheckSuccess(call)) {
        if (absl::GetFlag(FLAGS_print_response) ||
            absl::GetFlag(FLAGS_fail_on_failed_rpc)) {
          std::cout << "RPC failed: " << call->status.error_code() << ": "
                    << call->status.error_message() << std::endl;
        }
        if (absl::GetFlag(FLAGS_fail_on_failed_rpc) &&
            one_rpc_succeeded.load()) {
          abort();
        }
      } else {
        if (absl::GetFlag(FLAGS_print_response)) {
          auto metadata_hostname =
              call->context.GetServerInitialMetadata().find("hostname");
          std::string hostname =
              metadata_hostname !=
                      call->context.GetServerInitialMetadata().end()
                  ? std::string(metadata_hostname->second.data(),
                                metadata_hostname->second.length())
                  : call->simple_response.hostname();
          std::cout << "Greeting: Hello world, this is " << hostname
                    << ", from " << call->context.peer() << std::endl;
        }
        one_rpc_succeeded = true;
      }

      delete call;
    }
  }

 private:
  static bool RpcStatusCheckSuccess(AsyncClientCall* call) {
    // Determine RPC success based on expected status.
    grpc_status_code code;
    GPR_ASSERT(grpc_status_code_from_string(
        absl::GetFlag(FLAGS_expect_status).c_str(), &code));
    return code == static_cast<grpc_status_code>(call->status.error_code());
  }

  std::unique_ptr<TestService::Stub> stub_;
  StatsWatchers* stats_watchers_;
  CompletionQueue cq_;
};

class LoadBalancerStatsServiceImpl : public LoadBalancerStatsService::Service {
 public:
  explicit LoadBalancerStatsServiceImpl(StatsWatchers* stats_watchers)
      : stats_watchers_(stats_watchers) {}

  Status GetClientStats(ServerContext* /*context*/,
                        const LoadBalancerStatsRequest* request,
                        LoadBalancerStatsResponse* response) override {
    int start_id;
    int end_id;
    XdsStatsWatcher* watcher;
    {
      std::lock_guard<std::mutex> lock(stats_watchers_->mu);
      start_id = stats_watchers_->global_request_id + 1;
      end_id = start_id + request->num_rpcs();
      watcher = new XdsStatsWatcher(start_id, end_id);
      stats_watchers_->watchers.insert(watcher);
    }
    watcher->WaitForRpcStatsResponse(response, request->timeout_sec());
    {
      std::lock_guard<std::mutex> lock(stats_watchers_->mu);
      stats_watchers_->watchers.erase(watcher);
    }
    delete watcher;
    return Status::OK;
  }

  Status GetClientAccumulatedStats(
      ServerContext* /*context*/,
      const LoadBalancerAccumulatedStatsRequest* /*request*/,
      LoadBalancerAccumulatedStatsResponse* response) override {
    std::lock_guard<std::mutex> lock(stats_watchers_->mu);
    stats_watchers_->global_watcher->GetCurrentRpcStats(response,
                                                        stats_watchers_);
    return Status::OK;
  }

 private:
  StatsWatchers* stats_watchers_;
};

class XdsUpdateClientConfigureServiceImpl
    : public XdsUpdateClientConfigureService::Service {
 public:
  explicit XdsUpdateClientConfigureServiceImpl(
      RpcConfigurationsQueue* rpc_configs_queue)
      : rpc_configs_queue_(rpc_configs_queue) {}

  Status Configure(ServerContext* /*context*/,
                   const ClientConfigureRequest* request,
                   ClientConfigureResponse* /*response*/) override {
    std::map<int, std::vector<std::pair<std::string, std::string>>>
        metadata_map;
    for (const auto& data : request->metadata()) {
      metadata_map[data.type()].push_back({data.key(), data.value()});
    }
    std::vector<RpcConfig> configs;
    for (const auto& rpc : request->types()) {
      RpcConfig config;
      config.timeout_sec = request->timeout_sec();
      config.type = static_cast<ClientConfigureRequest::RpcType>(rpc);
      auto metadata_iter = metadata_map.find(rpc);
      if (metadata_iter != metadata_map.end()) {
        config.metadata = metadata_iter->second;
      }
      configs.push_back(std::move(config));
    }
    {
      std::lock_guard<std::mutex> lock(
          rpc_configs_queue_->mu_rpc_configs_queue);
      rpc_configs_queue_->rpc_configs_queue.emplace_back(std::move(configs));
    }
    return Status::OK;
  }

 private:
  RpcConfigurationsQueue* rpc_configs_queue_;
};

void RunTestLoop(std::chrono::duration<double> duration_per_query,
                 StatsWatchers* stats_watchers,
                 RpcConfigurationsQueue* rpc_configs_queue) {
  grpc::ChannelArguments channel_args;
  channel_args.SetInt(GRPC_ARG_ENABLE_RETRIES, 1);
  TestClient client(
      grpc::CreateCustomChannel(absl::GetFlag(FLAGS_server),
                                absl::GetFlag(FLAGS_secure_mode)
                                    ? grpc::experimental::XdsCredentials(
                                          grpc::InsecureChannelCredentials())
                                    : grpc::InsecureChannelCredentials(),
                                channel_args),
      stats_watchers);
  std::chrono::time_point<std::chrono::system_clock> start =
      std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed;

  std::thread thread = std::thread(&TestClient::AsyncCompleteRpc, &client);

  std::vector<RpcConfig> configs;
  while (true) {
    {
      std::lock_guard<std::mutex> lockk(
          rpc_configs_queue->mu_rpc_configs_queue);
      if (!rpc_configs_queue->rpc_configs_queue.empty()) {
        configs = std::move(rpc_configs_queue->rpc_configs_queue.front());
        rpc_configs_queue->rpc_configs_queue.pop_front();
      }
    }

    elapsed = std::chrono::system_clock::now() - start;
    if (elapsed > duration_per_query) {
      start = std::chrono::system_clock::now();
      for (const auto& config : configs) {
        if (config.type == ClientConfigureRequest::EMPTY_CALL) {
          client.AsyncEmptyCall(config);
        } else if (config.type == ClientConfigureRequest::UNARY_CALL) {
          client.AsyncUnaryCall(config);
        } else {
          GPR_ASSERT(0);
        }
      }
    }
  }
  GPR_UNREACHABLE_CODE(thread.join());
}

void RunServer(const int port, StatsWatchers* stats_watchers,
               RpcConfigurationsQueue* rpc_configs_queue) {
  GPR_ASSERT(port != 0);
  std::ostringstream server_address;
  server_address << "0.0.0.0:" << port;

  LoadBalancerStatsServiceImpl stats_service(stats_watchers);
  XdsUpdateClientConfigureServiceImpl client_config_service(rpc_configs_queue);

  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  builder.RegisterService(&stats_service);
  builder.RegisterService(&client_config_service);
  grpc::AddAdminServices(&builder);
  builder.AddListeningPort(server_address.str(),
                           grpc::InsecureServerCredentials());
  std::unique_ptr<Server> server(builder.BuildAndStart());
  gpr_log(GPR_DEBUG, "Server listening on %s", server_address.str().c_str());

  server->Wait();
}

void BuildRpcConfigsFromFlags(RpcConfigurationsQueue* rpc_configs_queue) {
  // Store Metadata like
  // "EmptyCall:key1:value1,UnaryCall:key1:value1,UnaryCall:key2:value2" into a
  // map where the key is the RPC method and value is a vector of key:value
  // pairs. {EmptyCall, [{key1,value1}],
  //  UnaryCall, [{key1,value1}, {key2,value2}]}
  std::vector<std::string> rpc_metadata =
      absl::StrSplit(absl::GetFlag(FLAGS_metadata), ',', absl::SkipEmpty());
  std::map<int, std::vector<std::pair<std::string, std::string>>> metadata_map;
  for (auto& data : rpc_metadata) {
    std::vector<std::string> metadata =
        absl::StrSplit(data, ':', absl::SkipEmpty());
    GPR_ASSERT(metadata.size() == 3);
    if (metadata[0] == "EmptyCall") {
      metadata_map[ClientConfigureRequest::EMPTY_CALL].push_back(
          {metadata[1], metadata[2]});
    } else if (metadata[0] == "UnaryCall") {
      metadata_map[ClientConfigureRequest::UNARY_CALL].push_back(
          {metadata[1], metadata[2]});
    } else {
      GPR_ASSERT(0);
    }
  }
  std::vector<RpcConfig> configs;
  std::vector<std::string> rpc_methods =
      absl::StrSplit(absl::GetFlag(FLAGS_rpc), ',', absl::SkipEmpty());
  for (const std::string& rpc_method : rpc_methods) {
    RpcConfig config;
    if (rpc_method == "EmptyCall") {
      config.type = ClientConfigureRequest::EMPTY_CALL;
    } else if (rpc_method == "UnaryCall") {
      config.type = ClientConfigureRequest::UNARY_CALL;
    } else {
      GPR_ASSERT(0);
    }
    auto metadata_iter = metadata_map.find(config.type);
    if (metadata_iter != metadata_map.end()) {
      config.metadata = metadata_iter->second;
    }
    configs.push_back(std::move(config));
  }
  {
    std::lock_guard<std::mutex> lock(rpc_configs_queue->mu_rpc_configs_queue);
    rpc_configs_queue->rpc_configs_queue.emplace_back(std::move(configs));
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  // Validate the expect_status flag.
  grpc_status_code code;
  GPR_ASSERT(grpc_status_code_from_string(
      absl::GetFlag(FLAGS_expect_status).c_str(), &code));
  StatsWatchers stats_watchers;
  RpcConfigurationsQueue rpc_config_queue;

  {
    std::lock_guard<std::mutex> lock(stats_watchers.mu);
    stats_watchers.global_watcher = new XdsStatsWatcher(0, 0);
    stats_watchers.watchers.insert(stats_watchers.global_watcher);
  }

  BuildRpcConfigsFromFlags(&rpc_config_queue);

  std::chrono::duration<double> duration_per_query =
      std::chrono::nanoseconds(std::chrono::seconds(1)) /
      absl::GetFlag(FLAGS_qps);

  std::vector<std::thread> test_threads;
  test_threads.reserve(absl::GetFlag(FLAGS_num_channels));
  for (int i = 0; i < absl::GetFlag(FLAGS_num_channels); i++) {
    test_threads.emplace_back(std::thread(&RunTestLoop, duration_per_query,
                                          &stats_watchers, &rpc_config_queue));
  }

  RunServer(absl::GetFlag(FLAGS_stats_port), &stats_watchers,
            &rpc_config_queue);

  for (auto it = test_threads.begin(); it != test_threads.end(); it++) {
    it->join();
  }

  return 0;
}
