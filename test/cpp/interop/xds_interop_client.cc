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

#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

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

#include "absl/flags/flag.h"
#include "absl/strings/str_split.h"
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

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::testing::ClientConfigureRequest;
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

// Unique ID for each outgoing RPC
int global_request_id;
std::map<std::string, int> global_request_id_by_method;
// Stores a set of watchers that should be notified upon outgoing RPC completion
std::set<XdsStatsWatcher*> watchers;
// Global watcher for accumululated stats.
XdsStatsWatcher* global_watcher;
// Mutex for global_request_id and watchers
std::mutex mu;
// Whether at least one RPC has succeeded, indicating xDS resolution completed.
std::atomic<bool> one_rpc_succeeded(false);
// RPC configuration detailing how RPC should be sent.
struct RpcConfig {
  std::string type;
  std::vector<std::pair<std::string, std::string>> metadata;
};
// A queue of RPC configurations detailing how RPCs should be sent.
std::deque<std::vector<RpcConfig>> rpc_configs_queue;
// Mutex for rpc_configs_queue
std::mutex mu_rpc_configs_queue;

/** Records the remote peer distribution for a given range of RPCs. */
class XdsStatsWatcher {
 public:
  XdsStatsWatcher(int start_id, int end_id)
      : start_id_(start_id), end_id_(end_id), rpcs_needed_(end_id - start_id) {
    if (start_id_ == 0 && end_id_ == 0) global = true;
  }

  void RpcCompleted(int request_id, const std::string& rpc_method,
                    const std::string& peer) {
    if (global || (start_id_ <= request_id && request_id < end_id_)) {
      {
        std::lock_guard<std::mutex> lk(m_);
        if (peer.empty()) {
          no_remote_peer_++;
          ++no_remote_peer_by_method_[rpc_method];
        } else {
          rpcs_by_peer_[peer]++;
          rpcs_by_method_[rpc_method][peer]++;
        }
        rpcs_needed_--;
      }
      cv_.notify_one();
    }
  }

  void WaitForRpcStatsResponse(LoadBalancerStatsResponse* response,
                               int timeout_sec) {
    {
      std::unique_lock<std::mutex> lk(m_);
      cv_.wait_for(lk, std::chrono::seconds(timeout_sec),
                   [this] { return rpcs_needed_ == 0; });
      response->mutable_rpcs_by_peer()->insert(rpcs_by_peer_.begin(),
                                               rpcs_by_peer_.end());
      auto& response_rpcs_by_method = *response->mutable_rpcs_by_method();
      for (const auto& rpc_by_method : rpcs_by_method_) {
        auto& response_rpc_by_method =
            response_rpcs_by_method[rpc_by_method.first];
        auto& response_rpcs_by_peer =
            *response_rpc_by_method.mutable_rpcs_by_peer();
        for (const auto& rpc_by_peer : rpc_by_method.second) {
          auto& response_rpc_by_peer = response_rpcs_by_peer[rpc_by_peer.first];
          response_rpc_by_peer = rpc_by_peer.second;
        }
      }
      response->set_num_failures(no_remote_peer_ + rpcs_needed_);
    }
  }

  void GetCurrentRpcStats(LoadBalancerAccumulatedStatsResponse* response) {
    std::unique_lock<std::mutex> lk(m_);
    auto& response_rpcs_started_by_method =
        *response->mutable_num_rpcs_started_by_method();
    auto& response_rpcs_succeeded_by_method =
        *response->mutable_num_rpcs_succeeded_by_method();
    auto& response_rpcs_failed_by_method =
        *response->mutable_num_rpcs_failed_by_method();
    for (const auto& rpc_by_method : rpcs_by_method_) {
      auto total_succeeded = 0;
      for (const auto& rpc_by_peer : rpc_by_method.second) {
        total_succeeded += rpc_by_peer.second;
      }
      response_rpcs_succeeded_by_method[rpc_by_method.first] = total_succeeded;
      response_rpcs_started_by_method[rpc_by_method.first] =
          global_request_id_by_method[rpc_by_method.first];
      response_rpcs_failed_by_method[rpc_by_method.first] =
          no_remote_peer_by_method_[rpc_by_method.first];
    }
  }

 private:
  bool global = false;
  int start_id_;
  int end_id_;
  int rpcs_needed_;
  int no_remote_peer_ = 0;
  std::map<std::string, int> no_remote_peer_by_method_;
  // A map of stats keyed by peer name.
  std::map<std::string, int> rpcs_by_peer_;
  // A two-level map of stats keyed at top level by RPC method and second level
  // by peer name.
  std::map<std::string, std::map<std::string, int>> rpcs_by_method_;
  std::mutex m_;
  std::condition_variable cv_;
};

class TestClient {
 public:
  TestClient(const std::shared_ptr<Channel>& channel)
      : stub_(TestService::NewStub(channel)) {}

  void AsyncUnaryCall(
      std::vector<std::pair<std::string, std::string>> metadata) {
    SimpleResponse response;
    int saved_request_id;
    {
      std::lock_guard<std::mutex> lk(mu);
      saved_request_id = ++global_request_id;
      ++global_request_id_by_method["UNARY_CALL"];
    }
    std::chrono::system_clock::time_point deadline =
        std::chrono::system_clock::now() +
        std::chrono::seconds(absl::GetFlag(FLAGS_rpc_timeout_sec));
    AsyncClientCall* call = new AsyncClientCall;
    call->context.set_deadline(deadline);
    for (const auto& data : metadata) {
      call->context.AddMetadata(data.first, data.second);
    }
    call->saved_request_id = saved_request_id;
    call->rpc_method = "UNARY_CALL";
    call->simple_response_reader = stub_->PrepareAsyncUnaryCall(
        &call->context, SimpleRequest::default_instance(), &cq_);
    call->simple_response_reader->StartCall();
    call->simple_response_reader->Finish(&call->simple_response, &call->status,
                                         (void*)call);
  }

  void AsyncEmptyCall(
      std::vector<std::pair<std::string, std::string>> metadata) {
    Empty response;
    int saved_request_id;
    {
      std::lock_guard<std::mutex> lk(mu);
      saved_request_id = ++global_request_id;
      ++global_request_id_by_method["EMPTY_CALL"];
    }
    std::chrono::system_clock::time_point deadline =
        std::chrono::system_clock::now() +
        std::chrono::seconds(absl::GetFlag(FLAGS_rpc_timeout_sec));
    AsyncClientCall* call = new AsyncClientCall;
    call->context.set_deadline(deadline);
    for (const auto& data : metadata) {
      call->context.AddMetadata(data.first, data.second);
    }
    call->saved_request_id = saved_request_id;
    call->rpc_method = "EMPTY_CALL";
    call->empty_response_reader = stub_->PrepareAsyncEmptyCall(
        &call->context, Empty::default_instance(), &cq_);
    call->empty_response_reader->StartCall();
    call->empty_response_reader->Finish(&call->empty_response, &call->status,
                                        (void*)call);
  }

  void AsyncCompleteRpc() {
    void* got_tag;
    bool ok = false;
    while (cq_.Next(&got_tag, &ok)) {
      AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);
      GPR_ASSERT(ok);
      {
        std::lock_guard<std::mutex> lk(mu);
        auto server_initial_metadata = call->context.GetServerInitialMetadata();
        auto metadata_hostname =
            call->context.GetServerInitialMetadata().find("hostname");
        std::string hostname =
            metadata_hostname != call->context.GetServerInitialMetadata().end()
                ? std::string(metadata_hostname->second.data(),
                              metadata_hostname->second.length())
                : call->simple_response.hostname();
        for (auto watcher : watchers) {
          watcher->RpcCompleted(call->saved_request_id, call->rpc_method,
                                hostname);
        }
      }

      if (!call->status.ok()) {
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
  struct AsyncClientCall {
    Empty empty_response;
    SimpleResponse simple_response;
    ClientContext context;
    Status status;
    int saved_request_id;
    std::string rpc_method;
    std::unique_ptr<ClientAsyncResponseReader<Empty>> empty_response_reader;
    std::unique_ptr<ClientAsyncResponseReader<SimpleResponse>>
        simple_response_reader;
  };

  std::unique_ptr<TestService::Stub> stub_;
  CompletionQueue cq_;
};

class LoadBalancerStatsServiceImpl : public LoadBalancerStatsService::Service {
 public:
  Status GetClientStats(ServerContext* context,
                        const LoadBalancerStatsRequest* request,
                        LoadBalancerStatsResponse* response) override {
    int start_id;
    int end_id;
    XdsStatsWatcher* watcher;
    {
      std::lock_guard<std::mutex> lk(mu);
      start_id = global_request_id + 1;
      end_id = start_id + request->num_rpcs();
      watcher = new XdsStatsWatcher(start_id, end_id);
      watchers.insert(watcher);
    }
    watcher->WaitForRpcStatsResponse(response, request->timeout_sec());
    {
      std::lock_guard<std::mutex> lk(mu);
      watchers.erase(watcher);
    }
    delete watcher;
    return Status::OK;
  }

  Status GetClientAccumulatedStats(
      ServerContext* context,
      const LoadBalancerAccumulatedStatsRequest* request,
      LoadBalancerAccumulatedStatsResponse* response) override {
    std::lock_guard<std::mutex> lk(mu);
    global_watcher->GetCurrentRpcStats(response);
    return Status::OK;
  }
};

class XdsUpdateClientConfigureServiceImpl
    : public XdsUpdateClientConfigureService::Service {
 public:
  Status Configure(ServerContext* context,
                   const ClientConfigureRequest* request,
                   ClientConfigureResponse* response) override {
    std::map<int, std::vector<std::pair<std::string, std::string>>>
        metadata_map;
    for (int i = 0; i < request->metadata_size(); ++i) {
      auto data = request->metadata(i);
      metadata_map[data.type()].push_back({data.key(), data.value()});
    }
    std::vector<RpcConfig> configs;
    for (int i = 0; i < request->types_size(); ++i) {
      auto rpc = request->types(i);
      RpcConfig config;
      config.type = ClientConfigureRequest_RpcType_Name(rpc);
      auto metadata_iter = metadata_map.find(rpc);
      if (metadata_iter != metadata_map.end()) {
        for (auto& data : metadata_iter->second) {
          config.metadata.push_back(
              {std::string(data.first), std::string(data.second)});
        }
      }
      configs.push_back(std::move(config));
    }
    {
      std::lock_guard<std::mutex> lk(mu_rpc_configs_queue);
      rpc_configs_queue.emplace_back(std::move(configs));
    }
    return Status::OK;
  }
};

void RunTestLoop(std::chrono::duration<double> duration_per_query) {
  TestClient client(grpc::CreateChannel(absl::GetFlag(FLAGS_server),
                                        grpc::InsecureChannelCredentials()));
  std::chrono::time_point<std::chrono::system_clock> start =
      std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed;

  std::thread thread = std::thread(&TestClient::AsyncCompleteRpc, &client);

  std::vector<RpcConfig> configs;
  while (true) {
    {
      std::lock_guard<std::mutex> lk(mu_rpc_configs_queue);
      if (!rpc_configs_queue.empty()) {
        configs = std::move(rpc_configs_queue.front());
        rpc_configs_queue.pop_front();
      }
    }
    for (const auto& config : configs) {
      elapsed = std::chrono::system_clock::now() - start;
      if (elapsed > duration_per_query) {
        start = std::chrono::system_clock::now();
        if (config.type == "EMPTY_CALL") {
          client.AsyncEmptyCall(config.metadata);
        } else if (config.type == "UNARY_CALL") {
          client.AsyncUnaryCall(config.metadata);
        } else {
          GPR_ASSERT(0);
        }
      }
    }
  }
  thread.join();
}

void RunServers(const int port) {
  GPR_ASSERT(port != 0);
  std::ostringstream server_address;
  server_address << "0.0.0.0:" << port;

  LoadBalancerStatsServiceImpl stats_service;
  XdsUpdateClientConfigureServiceImpl client_config_service;

  ServerBuilder builder;
  builder.RegisterService(&stats_service);
  builder.RegisterService(&client_config_service);
  builder.AddListeningPort(server_address.str(),
                           grpc::InsecureServerCredentials());
  std::unique_ptr<Server> server(builder.BuildAndStart());
  gpr_log(GPR_DEBUG, "Server listening on %s", server_address.str().c_str());

  server->Wait();
}

void BuildRpcConfigsFromFlags() {
  std::vector<std::string> rpc_methods =
      absl::StrSplit(absl::GetFlag(FLAGS_rpc), ',', absl::SkipEmpty());
  // Store Metadata like
  // "EmptyCall:key1:value1,UnaryCall:key1:value1,UnaryCall:key2:value2" into a
  // map where the key is the RPC method and value is a vector of key:value
  // pairs. {EmptyCall, [{key1,value1}],
  //  UnaryCall, [{key1,value1}, {key2,value2}]}
  std::vector<std::string> rpc_metadata =
      absl::StrSplit(absl::GetFlag(FLAGS_metadata), ',', absl::SkipEmpty());
  std::map<std::string, std::vector<std::pair<std::string, std::string>>>
      metadata_map;
  for (auto& data : rpc_metadata) {
    std::vector<std::string> metadata =
        absl::StrSplit(data, ':', absl::SkipEmpty());
    GPR_ASSERT(metadata.size() == 3);
    if (metadata[0] == "EmptyCall") {
      metadata_map["EMPTY_CALL"].push_back({metadata[1], metadata[2]});
    } else if (metadata[0] == "UnaryCall") {
      metadata_map["UNARY_CALL"].push_back({metadata[1], metadata[2]});
    } else {
      GPR_ASSERT(0);
    }
  }
  std::vector<RpcConfig> configs;
  for (const std::string& rpc_method : rpc_methods) {
    RpcConfig config;
    if (rpc_method == "EmptyCall") {
      config.type = "EMPTY_CALL";
    } else if (rpc_method == "UnaryCall") {
      config.type = "UNARY_CALL";
    } else {
      GPR_ASSERT(0);
    }
    auto metadata_iter = metadata_map.find(config.type);
    if (metadata_iter != metadata_map.end()) {
      for (auto& data : metadata_iter->second) {
        config.metadata.push_back({data.first, data.second});
      }
    }
    configs.push_back(std::move(config));
  }
  {
    std::lock_guard<std::mutex> lk(mu_rpc_configs_queue);
    rpc_configs_queue.emplace_back(std::move(configs));
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);

  {
    std::lock_guard<std::mutex> lk(mu);
    global_watcher = new XdsStatsWatcher(0, 0);
    watchers.insert(global_watcher);
  }

  BuildRpcConfigsFromFlags();

  std::chrono::duration<double> duration_per_query =
      std::chrono::nanoseconds(std::chrono::seconds(1)) /
      absl::GetFlag(FLAGS_qps);

  std::vector<std::thread> test_threads;
  test_threads.reserve(absl::GetFlag(FLAGS_num_channels) + 1);
  for (int i = 0; i < absl::GetFlag(FLAGS_num_channels); i++) {
    test_threads.emplace_back(std::thread(&RunTestLoop, duration_per_query));
  }

  test_threads.emplace_back(
      std::thread(&RunServers, absl::GetFlag(FLAGS_stats_port)));

  for (auto it = test_threads.begin(); it != test_threads.end(); it++) {
    it->join();
  }

  return 0;
}
