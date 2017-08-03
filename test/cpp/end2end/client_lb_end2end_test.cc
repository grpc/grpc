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

#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

extern "C" {
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/filters/client_channel/subchannel_index.h"
}

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

  void ResetCounters() {
    std::unique_lock<std::mutex> lock(mu_);
    request_count_ = 0;
  }

 private:
  std::mutex mu_;
  int request_count_;
};

class ClientLbEnd2endTest : public ::testing::Test {
 protected:
  ClientLbEnd2endTest() : server_host_("localhost") {}

  void SetUp() override {
    response_generator_ = grpc_fake_resolver_response_generator_create();
  }

  void TearDown() override {
    grpc_fake_resolver_response_generator_unref(response_generator_);
    for (size_t i = 0; i < servers_.size(); ++i) {
      servers_[i]->Shutdown();
    }
  }

  void StartServers(size_t num_servers,
                    std::vector<int> ports = std::vector<int>()) {
    for (size_t i = 0; i < num_servers; ++i) {
      int port = 0;
      if (ports.size() == num_servers) port = ports[i];
      servers_.emplace_back(new ServerData(server_host_, port));
    }
  }

  void SetNextResolution(const std::vector<int>& ports) {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_lb_addresses* addresses = grpc_lb_addresses_create(ports.size(), NULL);
    for (size_t i = 0; i < ports.size(); ++i) {
      char* lb_uri_str;
      gpr_asprintf(&lb_uri_str, "ipv4:127.0.0.1:%d", ports[i]);
      grpc_uri* lb_uri = grpc_uri_parse(&exec_ctx, lb_uri_str, true);
      GPR_ASSERT(lb_uri != NULL);
      grpc_lb_addresses_set_address_from_uri(addresses, i, lb_uri,
                                             false /* is balancer */,
                                             "" /* balancer name */, NULL);
      grpc_uri_destroy(lb_uri);
      gpr_free(lb_uri_str);
    }
    const grpc_arg fake_addresses =
        grpc_lb_addresses_create_channel_arg(addresses);
    grpc_channel_args* fake_result =
        grpc_channel_args_copy_and_add(NULL, &fake_addresses, 1);
    grpc_fake_resolver_response_generator_set_response(
        &exec_ctx, response_generator_, fake_result);
    grpc_channel_args_destroy(&exec_ctx, fake_result);
    grpc_lb_addresses_destroy(&exec_ctx, addresses);
    grpc_exec_ctx_finish(&exec_ctx);
  }

  void ResetStub(const grpc::string& lb_policy_name = "") {
    ChannelArguments args;
    if (lb_policy_name.size() > 0) {
      args.SetLoadBalancingPolicyName(lb_policy_name);
    }  // else, default to pick first
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_);
    std::ostringstream uri;
    uri << "fake:///";
    for (size_t i = 0; i < servers_.size() - 1; ++i) {
      uri << "127.0.0.1:" << servers_[i]->port_ << ",";
    }
    uri << "127.0.0.1:" << servers_[servers_.size() - 1]->port_;
    channel_ =
        CreateCustomChannel(uri.str(), InsecureChannelCredentials(), args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  void SendRpc(bool expect_ok = true) {
    EchoRequest request;
    EchoResponse response;
    request.set_message("Live long and prosper.");
    ClientContext context;
    Status status = stub_->Echo(&context, request, &response);
    if (expect_ok) {
      EXPECT_TRUE(status.ok());
      EXPECT_EQ(response.message(), request.message());
    } else {
      EXPECT_FALSE(status.ok());
    }
  }

  struct ServerData {
    int port_;
    std::unique_ptr<Server> server_;
    MyTestServiceImpl service_;
    std::unique_ptr<std::thread> thread_;

    explicit ServerData(const grpc::string& server_host, int port = 0) {
      port_ = port > 0 ? port : grpc_pick_unused_port_or_die();
      gpr_log(GPR_INFO, "starting server on port %d", port_);
      std::mutex mu;
      std::condition_variable cond;
      thread_.reset(new std::thread(
          std::bind(&ServerData::Start, this, server_host, &mu, &cond)));
      std::unique_lock<std::mutex> lock(mu);
      cond.wait(lock);
      gpr_log(GPR_INFO, "server startup complete");
    }

    void Start(const grpc::string& server_host, std::mutex* mu,
               std::condition_variable* cond) {
      std::ostringstream server_address;
      server_address << server_host << ":" << port_;
      ServerBuilder builder;
      builder.AddListeningPort(server_address.str(),
                               InsecureServerCredentials());
      builder.RegisterService(&service_);
      server_ = builder.BuildAndStart();
      std::lock_guard<std::mutex> lock(*mu);
      cond->notify_one();
    }

    void Shutdown(bool join = true) {
      server_->Shutdown();
      if (join) thread_->join();
    }
  };

  void ResetCounters() {
    for (const auto& server : servers_) server->service_.ResetCounters();
  }

  void WaitForServer(size_t server_idx) {
    do {
      SendRpc();
    } while (servers_[server_idx]->service_.request_count() == 0);
    ResetCounters();
  }

  const grpc::string server_host_;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::vector<std::unique_ptr<ServerData>> servers_;
  grpc_fake_resolver_response_generator* response_generator_;
};

TEST_F(ClientLbEnd2endTest, PickFirst) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ResetStub();  // implicit pick first
  std::vector<int> ports;
  for (size_t i = 0; i < servers_.size(); ++i) {
    ports.emplace_back(servers_[i]->port_);
  }
  SetNextResolution(ports);
  for (size_t i = 0; i < servers_.size(); ++i) {
    SendRpc();
  }
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

TEST_F(ClientLbEnd2endTest, PickFirstUpdates) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ResetStub();  // implicit pick first
  std::vector<int> ports;

  // Perform one RPC against the first server.
  ports.emplace_back(servers_[0]->port_);
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [0] *******");
  SendRpc();
  EXPECT_EQ(servers_[0]->service_.request_count(), 1);

  // An empty update will result in the channel going into TRANSIENT_FAILURE.
  ports.clear();
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET none *******");
  grpc_connectivity_state channel_state = GRPC_CHANNEL_INIT;
  do {
    channel_state = channel_->GetState(true /* try to connect */);
  } while (channel_state == GRPC_CHANNEL_READY);
  GPR_ASSERT(channel_state != GRPC_CHANNEL_READY);
  servers_[0]->service_.ResetCounters();

  // Next update introduces servers_[1], making the channel recover.
  ports.clear();
  ports.emplace_back(servers_[1]->port_);
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [1] *******");
  WaitForServer(1);
  EXPECT_EQ(servers_[0]->service_.request_count(), 0);

  // And again for servers_[2]
  ports.clear();
  ports.emplace_back(servers_[2]->port_);
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [2] *******");
  WaitForServer(2);
  EXPECT_EQ(servers_[0]->service_.request_count(), 0);
  EXPECT_EQ(servers_[1]->service_.request_count(), 0);

  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel_->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, PickFirstUpdateSuperset) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ResetStub();  // implicit pick first
  std::vector<int> ports;

  // Perform one RPC against the first server.
  ports.emplace_back(servers_[0]->port_);
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET [0] *******");
  SendRpc();
  EXPECT_EQ(servers_[0]->service_.request_count(), 1);
  servers_[0]->service_.ResetCounters();

  // Send and superset update
  ports.clear();
  ports.emplace_back(servers_[1]->port_);
  ports.emplace_back(servers_[0]->port_);
  SetNextResolution(ports);
  gpr_log(GPR_INFO, "****** SET superset *******");
  SendRpc();
  // We stick to the previously connected server.
  WaitForServer(0);
  EXPECT_EQ(0, servers_[1]->service_.request_count());

  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel_->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, PickFirstManyUpdates) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ResetStub();  // implicit pick first
  std::vector<int> ports;
  for (size_t i = 0; i < servers_.size(); ++i) {
    ports.emplace_back(servers_[i]->port_);
  }
  for (const bool force_creation : {true, false}) {
    grpc_subchannel_index_test_only_set_force_creation(force_creation);
    gpr_log(GPR_INFO, "Force subchannel creation: %d", force_creation);
    for (size_t i = 0; i < 1000; ++i) {
      std::random_shuffle(ports.begin(), ports.end());
      SetNextResolution(ports);
      if (i % 10 == 0) SendRpc();
    }
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel_->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, RoundRobin) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ResetStub("round_robin");
  std::vector<int> ports;
  for (const auto& server : servers_) {
    ports.emplace_back(server->port_);
  }
  SetNextResolution(ports);
  for (size_t i = 0; i < servers_.size(); ++i) {
    SendRpc();
  }
  // One request should have gone to each server.
  for (size_t i = 0; i < servers_.size(); ++i) {
    EXPECT_EQ(1, servers_[i]->service_.request_count());
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel_->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, RoundRobinUpdates) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ResetStub("round_robin");
  std::vector<int> ports;

  // Start with a single server.
  ports.emplace_back(servers_[0]->port_);
  SetNextResolution(ports);
  WaitForServer(0);
  // Send RPCs. They should all go servers_[0]
  for (size_t i = 0; i < 10; ++i) SendRpc();
  EXPECT_EQ(10, servers_[0]->service_.request_count());
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  EXPECT_EQ(0, servers_[2]->service_.request_count());
  servers_[0]->service_.ResetCounters();

  // And now for the second server.
  ports.clear();
  ports.emplace_back(servers_[1]->port_);
  SetNextResolution(ports);

  // Wait until update has been processed, as signaled by the second backend
  // receiving a request.
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  WaitForServer(1);

  for (size_t i = 0; i < 10; ++i) SendRpc();
  EXPECT_EQ(0, servers_[0]->service_.request_count());
  EXPECT_EQ(10, servers_[1]->service_.request_count());
  EXPECT_EQ(0, servers_[2]->service_.request_count());
  servers_[1]->service_.ResetCounters();

  // ... and for the last server.
  ports.clear();
  ports.emplace_back(servers_[2]->port_);
  SetNextResolution(ports);
  WaitForServer(2);

  for (size_t i = 0; i < 10; ++i) SendRpc();
  EXPECT_EQ(0, servers_[0]->service_.request_count());
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  EXPECT_EQ(10, servers_[2]->service_.request_count());
  servers_[2]->service_.ResetCounters();

  // Back to all servers.
  ports.clear();
  ports.emplace_back(servers_[0]->port_);
  ports.emplace_back(servers_[1]->port_);
  ports.emplace_back(servers_[2]->port_);
  SetNextResolution(ports);
  WaitForServer(0);
  WaitForServer(1);
  WaitForServer(2);

  // Send three RPCs, one per server.
  for (size_t i = 0; i < 3; ++i) SendRpc();
  EXPECT_EQ(1, servers_[0]->service_.request_count());
  EXPECT_EQ(1, servers_[1]->service_.request_count());
  EXPECT_EQ(1, servers_[2]->service_.request_count());

  // An empty update will result in the channel going into TRANSIENT_FAILURE.
  ports.clear();
  SetNextResolution(ports);
  grpc_connectivity_state channel_state = GRPC_CHANNEL_INIT;
  do {
    channel_state = channel_->GetState(true /* try to connect */);
  } while (channel_state == GRPC_CHANNEL_READY);
  GPR_ASSERT(channel_state != GRPC_CHANNEL_READY);
  servers_[0]->service_.ResetCounters();

  // Next update introduces servers_[1], making the channel recover.
  ports.clear();
  ports.emplace_back(servers_[1]->port_);
  SetNextResolution(ports);
  WaitForServer(1);
  channel_state = channel_->GetState(false /* try to connect */);
  GPR_ASSERT(channel_state == GRPC_CHANNEL_READY);

  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel_->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, RoundRobinManyUpdates) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ResetStub("round_robin");
  std::vector<int> ports;
  for (size_t i = 0; i < servers_.size(); ++i) {
    ports.emplace_back(servers_[i]->port_);
  }
  for (size_t i = 0; i < 1000; ++i) {
    std::random_shuffle(ports.begin(), ports.end());
    SetNextResolution(ports);
    if (i % 10 == 0) SendRpc();
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel_->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, RoundRobinConcurrentUpdates) {
  // TODO(dgq): replicate the way internal testing exercises the concurrent
  // update provisions of RR.
}

TEST_F(ClientLbEnd2endTest, RoundRobinReconnect) {
  // Start servers and send one RPC per server.
  const int kNumServers = 1;
  std::vector<int> ports;
  ports.push_back(grpc_pick_unused_port_or_die());
  StartServers(kNumServers, ports);
  ResetStub("round_robin");
  SetNextResolution(ports);
  // Send one RPC per backend and make sure they are used in order.
  // Note: This relies on the fact that the subchannels are reported in
  // state READY in the order in which the addresses are specified,
  // which is only true because the backends are all local.
  for (size_t i = 0; i < servers_.size(); ++i) {
    SendRpc();
    EXPECT_EQ(1, servers_[i]->service_.request_count()) << "for backend #" << i;
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel_->GetLoadBalancingPolicyName());

  // Kill all servers
  for (size_t i = 0; i < servers_.size(); ++i) {
    servers_[i]->Shutdown(false);
  }
  // Client request should fail.
  SendRpc(false);

  // Bring servers back up on the same port (we aren't recreating the channel).
  StartServers(kNumServers, ports);

  // Client request should succeed.
  SendRpc();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_test_init(argc, argv);
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
