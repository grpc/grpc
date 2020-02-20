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

#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/test_config.h"

DEFINE_int32(num_channels, 1, "Number of channels.");
DEFINE_bool(print_response, false, "Write RPC response to stdout.");
DEFINE_int32(qps, 1, "Qps per channel.");
DEFINE_int32(rpc_timeout_sec, 10, "Per RPC timeout seconds.");
DEFINE_string(server, "localhost:50051", "Address of server.");
DEFINE_int32(stats_port, 50052,
             "Port to expose peer distribution stats service.");

using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCredentials;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
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

  void RpcCompleted(int request_id, const std::string& peer) {
    if (start_id_ <= request_id && request_id < end_id_) {
      {
        std::lock_guard<std::mutex> lk(m_);
        if (peer.empty()) {
          no_remote_peer_++;
        } else {
          rpcs_by_peer_[peer]++;
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
      response->set_num_failures(no_remote_peer_ + rpcs_needed_);
    }
  }

 private:
  int start_id_;
  int end_id_;
  int rpcs_needed_;
  std::map<std::string, int> rpcs_by_peer_;
  int no_remote_peer_;
  std::mutex m_;
  std::condition_variable cv_;
};

class TestClient {
 public:
  TestClient(const std::shared_ptr<Channel>& channel)
      : stub_(TestService::NewStub(channel)) {}

  void UnaryCall() {
    SimpleResponse response;
    ClientContext context;

    int saved_request_id;
    {
      std::lock_guard<std::mutex> lk(mu);
      saved_request_id = ++global_request_id;
    }
    std::chrono::system_clock::time_point deadline =
        std::chrono::system_clock::now() +
        std::chrono::seconds(FLAGS_rpc_timeout_sec);
    context.set_deadline(deadline);
    Status status = stub_->UnaryCall(
        &context, SimpleRequest::default_instance(), &response);

    {
      std::lock_guard<std::mutex> lk(mu);
      for (auto watcher : watchers) {
        watcher->RpcCompleted(saved_request_id, response.hostname());
      }
    }

    if (FLAGS_print_response) {
      if (status.ok()) {
        std::cout << "Greeting: Hello world, this is " << response.hostname()
                  << ", from " << context.peer() << std::endl;
      } else {
        std::cout << "RPC failed: " << status.error_code() << ": "
                  << status.error_message() << std::endl;
      }
    }
  }

 private:
  std::unique_ptr<TestService::Stub> stub_;
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
  TestClient client(
      grpc::CreateChannel(server, grpc::InsecureChannelCredentials()));
  std::chrono::time_point<std::chrono::system_clock> start =
      std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed;

  while (true) {
    elapsed = std::chrono::system_clock::now() - start;
    if (elapsed > duration_per_query) {
      start = std::chrono::system_clock::now();
      client.UnaryCall();
    }
  }
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

  std::chrono::duration<double> duration_per_query =
      std::chrono::nanoseconds(std::chrono::seconds(1)) / FLAGS_qps;

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
