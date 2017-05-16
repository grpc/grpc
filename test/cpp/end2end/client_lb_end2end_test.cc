/*
 *
 * Copyright 2016, Google Inc.
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

#include <memory>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

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
}

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

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

  void StartServers(int num_servers) {
    for (int i = 0; i < num_servers; ++i) {
      servers_.emplace_back(new ServerData(server_host_));
    }
  }

  struct AddressData {
    int port;
    bool is_balancer;
    grpc::string balancer_name;
  };

  void SetNextResolution(const std::vector<AddressData>& address_data) {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_lb_addresses* addresses =
        grpc_lb_addresses_create(address_data.size(), NULL);
    for (size_t i = 0; i < address_data.size(); ++i) {
      char* lb_uri_str;
      gpr_asprintf(&lb_uri_str, "ipv4:127.0.0.1:%d", address_data[i].port);
      grpc_uri* lb_uri = grpc_uri_parse(&exec_ctx, lb_uri_str, true);
      GPR_ASSERT(lb_uri != NULL);
      grpc_lb_addresses_set_address_from_uri(
          addresses, i, lb_uri, address_data[i].is_balancer,
          address_data[i].balancer_name.c_str(), NULL);
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

  void ResetStub(const grpc::string& lb_policy_name) {
    ChannelArguments args;
    args.SetLoadBalancingPolicyName(lb_policy_name);
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

  void SendRpc() {
    EchoRequest request;
    EchoResponse response;
    request.set_message("Live long and prosper.");
    ClientContext context;
    Status status = stub_->Echo(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.message(), request.message());
  }

  struct ServerData {
    int port_;
    std::unique_ptr<Server> server_;
    MyTestServiceImpl service_;
    std::unique_ptr<std::thread> thread_;

    explicit ServerData(const grpc::string& server_host) {
      port_ = grpc_pick_unused_port_or_die();
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

    void Shutdown() {
      server_->Shutdown();
      thread_->join();
    }
  };

  int WaitForServer(size_t server_idx, int start_count) {
    do {
      SendRpc();
    } while (servers_[server_idx]->service_.request_count() == start_count);
    return servers_[server_idx]->service_.request_count();
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
  ResetStub("pick_first");
  std::vector<AddressData> addresses;
  for (size_t i = 0; i < servers_.size(); ++i) {
    addresses.emplace_back(AddressData{servers_[i]->port_, false, ""});
  }
  SetNextResolution(addresses);
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
  ResetStub("pick_first");
  std::vector<AddressData> addresses;

  // Perform one RPC against the first server.
  addresses.emplace_back(AddressData{servers_[0]->port_, false, ""});
  SetNextResolution(addresses);
  gpr_log(GPR_INFO, "****** SET [0] *******");
  SendRpc();

  // An empty update doesn't trigger a policy update. The RPC will go to
  // servers_[0]
  addresses.clear();
  SetNextResolution(addresses);
  gpr_log(GPR_INFO, "****** SET none *******");
  SendRpc();
  EXPECT_EQ(2, servers_[0]->service_.request_count());

  // Next update will replace servers_[0] with servers_[1], forcing the update
  // of the policy's selected subchannel.
  addresses.clear();
  addresses.emplace_back(AddressData{servers_[1]->port_, false, ""});
  SetNextResolution(addresses);
  gpr_log(GPR_INFO, "****** SET [1] *******");
  for (int i = 0; i < 10; ++i) SendRpc();
  EXPECT_GT(servers_[1]->service_.request_count(), 0);
  EXPECT_EQ(servers_[0]->service_.request_count() +
                servers_[1]->service_.request_count(),
            12);

  // And again for servers_[2]
  addresses.clear();
  addresses.emplace_back(AddressData{servers_[2]->port_, false, ""});
  SetNextResolution(addresses);
  gpr_log(GPR_INFO, "****** SET [2] *******");
  for (int i = 0; i < 10; ++i) SendRpc();
  EXPECT_GT(servers_[2]->service_.request_count(), 0);
  EXPECT_EQ(servers_[0]->service_.request_count() +
                servers_[1]->service_.request_count() +
                servers_[2]->service_.request_count(),
            22);

  // Check LB policy name for the channel.
  EXPECT_EQ("pick_first", channel_->GetLoadBalancingPolicyName());
}

TEST_F(ClientLbEnd2endTest, RoundRobin) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ResetStub("round_robin");
  std::vector<AddressData> addresses;
  for (const auto& server : servers_) {
    addresses.emplace_back(AddressData{server->port_, false, ""});
  }
  SetNextResolution(addresses);
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
  std::vector<AddressData> addresses;
  std::vector<int> server_expectations(servers_.size(), 0);
  std::vector<int> base_counts(servers_.size(), 0);

  // Start with a single server.
  addresses.emplace_back(AddressData{servers_[0]->port_, false, ""});
  SetNextResolution(addresses);
  // Send RPCs. They should all go servers_[0]
  for (size_t i = 0; i < 10; ++i) SendRpc();
  EXPECT_EQ(10, servers_[0]->service_.request_count());
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  EXPECT_EQ(0, servers_[2]->service_.request_count());
  server_expectations[0] = 10;

  // And now for the second server.
  addresses.clear();
  addresses.emplace_back(AddressData{servers_[1]->port_, false, ""});
  SetNextResolution(addresses);

  // Wait until update has been processed, as signaled by the second backend
  // receiving a request.
  EXPECT_EQ(0, servers_[1]->service_.request_count());
  base_counts[1] = WaitForServer(1, 0);

  for (size_t i = 0; i < 10; ++i) SendRpc();
  // One request should have gone to each server.
  EXPECT_EQ(server_expectations[0], servers_[0]->service_.request_count());
  EXPECT_EQ(base_counts[1] + 10, servers_[1]->service_.request_count());
  EXPECT_EQ(0, servers_[2]->service_.request_count());
  server_expectations[1] = base_counts[1] + 10;

  // ... and for the last server.
  addresses.clear();
  addresses.emplace_back(AddressData{servers_[2]->port_, false, ""});
  SetNextResolution(addresses);

  EXPECT_EQ(0, servers_[2]->service_.request_count());
  base_counts[2] = WaitForServer(2, 0);

  for (size_t i = 0; i < 10; ++i) SendRpc();
  EXPECT_EQ(server_expectations[0], servers_[0]->service_.request_count());
  EXPECT_EQ(server_expectations[1], servers_[1]->service_.request_count());
  EXPECT_EQ(base_counts[2] + 10, servers_[2]->service_.request_count());
  server_expectations[2] = base_counts[2] + 10;

  // Back to all servers.
  addresses.clear();
  addresses.emplace_back(AddressData{servers_[0]->port_, false, ""});
  addresses.emplace_back(AddressData{servers_[1]->port_, false, ""});
  addresses.emplace_back(AddressData{servers_[2]->port_, false, ""});
  SetNextResolution(addresses);

  base_counts[0] = WaitForServer(0, server_expectations[0]);
  base_counts[1] = WaitForServer(1, server_expectations[1]);
  base_counts[2] = WaitForServer(2, server_expectations[2]);

  // Send three RPCs, one per server.
  for (size_t i = 0; i < 3; ++i) SendRpc();
  EXPECT_EQ(base_counts[0] + 1, servers_[0]->service_.request_count());
  EXPECT_EQ(base_counts[1] + 1, servers_[1]->service_.request_count());
  EXPECT_EQ(base_counts[2] + 1, servers_[2]->service_.request_count());

  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel_->GetLoadBalancingPolicyName());
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
