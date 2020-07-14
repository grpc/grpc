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

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gflags/gflags.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "absl/strings/str_split.h"
#include "absl/types/optional.h"
#include "src/core/lib/gpr/env.h"
#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/test_config.h"

DEFINE_bool(fail_on_failed_rpc, false, "Fail client if any RPCs fail.");
DEFINE_int32(num_channels, 1, "Number of channels.");
DEFINE_bool(print_response, false, "Write RPC response to stdout.");
DEFINE_int32(qps, 1, "Qps per channel.");
DEFINE_int32(rpc_timeout_sec, 30, "Per RPC timeout seconds.");
DEFINE_string(server, "localhost:50051", "Address of server.");
DEFINE_int32(stats_port, 50052,
             "Port to expose peer distribution stats service.");
DEFINE_string(rpc, "UnaryCall", "a comma separated list of rpc types.");
DEFINE_string(metadata, "", "metadata to send with the RPC.");

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCredentials;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using grpc::testing::Empty;
using grpc::testing::LoadBalancerStatsRequest;
using grpc::testing::LoadBalancerStatsResponse;
using grpc::testing::LoadBalancerStatsService;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::TestService;

class XdsStatsWatcher;

// Unique ID for each outgoing RPC
int global_request_id;
// Stores a set of watchers that should be notified upon outgoing RPC completion
std::set<XdsStatsWatcher*> watchers;
// Mutex for global_request_id and watchers
std::mutex mu;

/** Records the remote peer distribution for a given range of RPCs. */
class XdsStatsWatcher {
 public:
  XdsStatsWatcher(int start_id, int end_id)
      : start_id_(start_id), end_id_(end_id), rpcs_needed_(end_id - start_id) {}

  void RpcCompleted(int request_id, const std::string& rpc_type,
                    const std::string& peer) {
    if (start_id_ <= request_id && request_id < end_id_) {
      {
        std::lock_guard<std::mutex> lk(m_);
        if (peer.empty()) {
          no_remote_peer_++;
        } else {
          rpcs_by_peer_[peer]++;
          rpcs_by_type_[rpc_type][peer]++;
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
      auto& response_rpcs_by_type = *response->mutable_rpcs_by_type();
      for (auto rpc_by_type : rpcs_by_type_) {
        auto& response_rpc_by_type = response_rpcs_by_type[rpc_by_type.first];
        auto& response_rpcs_by_peer =
            *response_rpc_by_type.mutable_rpcs_by_peer();
        for (auto rpc_by_peer : rpc_by_type.second) {
          auto& response_rpc_by_peer = response_rpcs_by_peer[rpc_by_peer.first];
          response_rpc_by_peer = rpc_by_peer.second;
          gpr_log(GPR_INFO, "donna spot stats %s %s %d",
                  std::string(rpc_by_type.first).c_str(),
                  std::string(rpc_by_peer.first).c_str(), rpc_by_peer.second);
        }
      }
      response->set_num_failures(no_remote_peer_ + rpcs_needed_);
      gpr_log(GPR_INFO, "donna see response %s",
              response->DebugString().c_str());
    }
  }

 private:
  int start_id_;
  int end_id_;
  int rpcs_needed_;
  int no_remote_peer_ = 0;
  std::map<std::string, int> rpcs_by_peer_;
  std::map<std::string, std::map<std::string, int>> rpcs_by_type_;
  std::mutex m_;
  std::condition_variable cv_;
};

class TestClient {
 public:
  TestClient(const std::shared_ptr<Channel>& channel)
      : stub_(TestService::NewStub(channel)) {}

  void AsyncUnaryCall(
      absl::optional<std::vector<std::pair<std::string, std::string>>>
          metadata) {
    SimpleResponse response;
    int saved_request_id;
    {
      std::lock_guard<std::mutex> lk(mu);
      saved_request_id = ++global_request_id;
    }
    std::chrono::system_clock::time_point deadline =
        std::chrono::system_clock::now() +
        std::chrono::seconds(FLAGS_rpc_timeout_sec);
    AsyncClientCall* call = new AsyncClientCall;
    call->context.set_deadline(deadline);
    if (metadata.has_value()) {
      for (auto data : metadata.value()) {
        call->context.AddMetadata(data.first, data.second);
        gpr_log(GPR_INFO, "donna check metadata method: Unary: %s and %s",
                data.first.c_str(), data.second.c_str());
      }
    } else {
      gpr_log(GPR_INFO, "donna saw no metadata for this method: Unary");
    }
    call->saved_request_id = saved_request_id;
    call->rpc_type = "UnaryCall";
    call->simple_response_reader = stub_->PrepareAsyncUnaryCall(
        &call->context, SimpleRequest::default_instance(), &cq_);
    call->simple_response_reader->StartCall();
    call->simple_response_reader->Finish(&call->simple_response, &call->status,
                                         (void*)call);
  }

  void AsyncEmptyCall(
      absl::optional<std::vector<std::pair<std::string, std::string>>>
          metadata) {
    Empty response;
    int saved_request_id;
    {
      std::lock_guard<std::mutex> lk(mu);
      saved_request_id = ++global_request_id;
    }
    std::chrono::system_clock::time_point deadline =
        std::chrono::system_clock::now() +
        std::chrono::seconds(FLAGS_rpc_timeout_sec);
    AsyncClientCall* call = new AsyncClientCall;
    call->context.set_deadline(deadline);
    if (metadata.has_value()) {
      for (auto data : metadata.value()) {
        call->context.AddMetadata(data.first, data.second);
        gpr_log(GPR_INFO, "donna check metadata method: Empty: %s and %s",
                data.first.c_str(), data.second.c_str());
      }
    } else {
      gpr_log(GPR_INFO, "donna saw no metadata for this method: Empty");
    }
    // call->context.AddMetadata("xds_md", "exact_match");
    call->saved_request_id = saved_request_id;
    call->rpc_type = "EmptyCall";
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
        auto hostname =
            call->context.GetServerInitialMetadata().find("hostname");
        for (auto watcher : watchers) {
          watcher->RpcCompleted(
              call->saved_request_id, call->rpc_type,
              hostname != call->context.GetServerInitialMetadata().end()
                  ? std::string(hostname->second.data(),
                                hostname->second.length())
                  : call->simple_response.hostname());
        }
      }

      if (!call->status.ok()) {
        if (FLAGS_print_response || FLAGS_fail_on_failed_rpc) {
          std::cout << "RPC failed: " << call->status.error_code() << ": "
                    << call->status.error_message() << std::endl;
        }
        if (FLAGS_fail_on_failed_rpc) {
          abort();
        }
      } else {
        if (FLAGS_print_response) {
          std::cout << "Greeting: Hello world, this is "
                    << call->simple_response.hostname() << ", from "
                    << call->context.peer() << std::endl;
        }
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
    std::string rpc_type;
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
                        LoadBalancerStatsResponse* response) {
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
};

void RunTestLoop(const std::string& server,
                 std::chrono::duration<double> duration_per_query) {
  std::vector<std::string> rpc_types = absl::StrSplit(FLAGS_rpc, ',');
  // Store Metadata like
  // "EmptyCall:key1:value1,UnaryCall:key1:value1,UnaryCall:key2:value2" into a
  // map where the key is the RPC type and value is a vector of key:value pairs.
  // {EmptyCall, [{key1,value1}],
  //  UnaryCall, [{key1,value1}, {key2,value2}]}
  std::vector<std::string> rpc_metadata = absl::StrSplit(FLAGS_metadata, ',');
  std::map<std::string, std::vector<std::pair<std::string, std::string>>>
      metadata_map;
  for (auto& data : rpc_metadata) {
    std::vector<std::string> metadata = absl::StrSplit(data, ':');
    GPR_ASSERT(metadata.size() == 3);
    metadata_map[metadata[0]].push_back(std::pair<std::string, std::string>(
        std::move(metadata[1]), std::move(metadata[2])));
  }
  TestClient client(
      grpc::CreateChannel(server, grpc::InsecureChannelCredentials()));
  std::chrono::time_point<std::chrono::system_clock> start =
      std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed;

  std::thread thread = std::thread(&TestClient::AsyncCompleteRpc, &client);

  while (true) {
    for (auto rpc_type : rpc_types) {
      elapsed = std::chrono::system_clock::now() - start;
      if (elapsed > duration_per_query) {
        start = std::chrono::system_clock::now();
        absl::optional<std::vector<std::pair<std::string, std::string>>>
            metadata;
        auto metadata_iter = metadata_map.find(rpc_type);
        if (metadata_iter != metadata_map.end()) {
          metadata = metadata_iter->second;
        }
        if (rpc_type == "EmptyCall") {
          client.AsyncEmptyCall(metadata);
        } else {
          client.AsyncUnaryCall(metadata);
        }
      }
    }
  }
  thread.join();
}

void RunServer(const int port) {
  GPR_ASSERT(port != 0);
  std::ostringstream server_address;
  server_address << "0.0.0.0:" << port;

  LoadBalancerStatsServiceImpl service;

  ServerBuilder builder;
  builder.RegisterService(&service);
  builder.AddListeningPort(server_address.str(),
                           grpc::InsecureServerCredentials());
  std::unique_ptr<Server> server(builder.BuildAndStart());
  gpr_log(GPR_INFO, "Stats server listening on %s",
          server_address.str().c_str());

  server->Wait();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  gpr_setenv("GRPC_XDS_EXPERIMENTAL_ROUTING", "true");

  std::chrono::duration<double> duration_per_query =
      std::chrono::nanoseconds(std::chrono::seconds(1)) / FLAGS_qps;
  // todo: metadata
  std::vector<std::string> metadata = absl::StrSplit(FLAGS_metadata, ',');

  std::vector<std::thread> test_threads;

  test_threads.reserve(FLAGS_num_channels);
  for (int i = 0; i < FLAGS_num_channels; i++) {
    test_threads.emplace_back(
        std::thread(&RunTestLoop, FLAGS_server, duration_per_query));
  }

  RunServer(FLAGS_stats_port);

  for (auto it = test_threads.begin(); it != test_threads.end(); it++) {
    it->join();
  }

  return 0;
}
