/*
 *
 * Copyright 2016 gRPC authors.
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

#include <memory>
#include <mutex>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

#include <gtest/gtest.h>

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;
using std::chrono::system_clock;

namespace grpc {
namespace testing {
namespace {

// Subclass of TestServiceImpl that increments a request counter for
// every call to the Echo RPC.
class MyTestServiceImpl : public TestServiceImpl {
 public:
  MyTestServiceImpl() : request_count_(0) {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    {
      std::unique_lock<std::mutex> lock(mu_);
      ++request_count_;
    }
    return TestServiceImpl::Echo(context, request, response);
  }

  int request_count() {
    std::unique_lock<std::mutex> lock(mu_);
    return request_count_;
  }

 private:
  std::mutex mu_;
  int request_count_;
};

class RoundRobinEnd2endTest : public ::testing::Test {
 protected:
  RoundRobinEnd2endTest() : server_host_("localhost") {}

  void StartServers(size_t num_servers,
                    std::vector<int> ports = std::vector<int>()) {
    for (size_t i = 0; i < num_servers; ++i) {
      int port = 0;
      if (ports.size() == num_servers) port = ports[i];
      servers_.emplace_back(new ServerData(server_host_, port));
    }
  }

  void TearDown() override {
    for (size_t i = 0; i < servers_.size(); ++i) {
      servers_[i]->Shutdown();
    }
  }

  void ResetStub(bool round_robin) {
    ChannelArguments args;
    if (round_robin) args.SetLoadBalancingPolicyName("round_robin");
    std::ostringstream uri;
    uri << "ipv4:///";
    for (size_t i = 0; i < servers_.size() - 1; ++i) {
      uri << "127.0.0.1:" << servers_[i]->port_ << ",";
    }
    uri << "127.0.0.1:" << servers_[servers_.size() - 1]->port_;
    channel_ =
        CreateCustomChannel(uri.str(), InsecureChannelCredentials(), args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  void SendRpc(int num_rpcs, bool expect_ok = true) {
    EchoRequest request;
    EchoResponse response;
    request.set_message("Live long and prosper.");
    for (int i = 0; i < num_rpcs; i++) {
      ClientContext context;
      Status status = stub_->Echo(&context, request, &response);
      if (expect_ok) {
        EXPECT_TRUE(status.ok());
        EXPECT_EQ(response.message(), request.message());
      } else {
        EXPECT_FALSE(status.ok());
      }
    }
  }

  struct ServerData {
    int port_;
    std::unique_ptr<Server> server_;
    MyTestServiceImpl service_;

    explicit ServerData(const grpc::string& server_host, int port = 0) {
      port_ = port > 0 ? port : grpc_pick_unused_port_or_die();
      gpr_log(GPR_INFO, "starting server on port %d", port_);
      std::ostringstream server_address;
      server_address << server_host << ":" << port_;
      ServerBuilder builder;
      builder.AddListeningPort(server_address.str(),
                               InsecureServerCredentials());
      builder.RegisterService(&service_);
      server_ = builder.BuildAndStart();
      gpr_log(GPR_INFO, "server startup complete");
    }

    void Shutdown() { server_->Shutdown(); }
  };

  const grpc::string server_host_;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::vector<std::unique_ptr<ServerData>> servers_;
};

TEST_F(RoundRobinEnd2endTest, PickFirst) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ResetStub(false /* round_robin */);
  SendRpc(kNumServers);
  // All requests should have gone to a single server.
  bool found = false;
  for (size_t i = 0; i < servers_.size(); ++i) {
    const int request_count = servers_[i]->service_.request_count();
    if (request_count == kNumServers) {
      found = true;
    } else {
      EXPECT_EQ(0, request_count);
    }
  }
  EXPECT_TRUE(found);
  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel_->GetLoadBalancingPolicyName());
}

TEST_F(RoundRobinEnd2endTest, RoundRobin) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ResetStub(true /* round_robin */);
  // Send one RPC per backend and make sure they are used in order.
  // Note: This relies on the fact that the subchannels are reported in
  // state READY in the order in which the addresses are specified,
  // which is only true because the backends are all local.
  for (size_t i = 0; i < servers_.size(); ++i) {
    SendRpc(1);
    EXPECT_EQ(1, servers_[i]->service_.request_count()) << "for backend #" << i;
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel_->GetLoadBalancingPolicyName());
}

TEST_F(RoundRobinEnd2endTest, RoundRobinReconnect) {
  // Start servers and send one RPC per server.
  const int kNumServers = 1;
  std::vector<int> ports;
  ports.push_back(grpc_pick_unused_port_or_die());
  StartServers(kNumServers, ports);
  ResetStub(true /* round_robin */);
  // Send one RPC per backend and make sure they are used in order.
  // Note: This relies on the fact that the subchannels are reported in
  // state READY in the order in which the addresses are specified,
  // which is only true because the backends are all local.
  for (size_t i = 0; i < servers_.size(); ++i) {
    SendRpc(1);
    EXPECT_EQ(1, servers_[i]->service_.request_count()) << "for backend #" << i;
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel_->GetLoadBalancingPolicyName());

  // Kill all servers
  for (size_t i = 0; i < servers_.size(); ++i) {
    servers_[i]->Shutdown();
  }
  // Client request should fail.
  SendRpc(1, false);

  // Bring servers back up on the same port (we aren't recreating the channel).
  StartServers(kNumServers, ports);

  // Client request should succeed.
  SendRpc(1);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
