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
#include <gtest/gtest.h>

extern "C" {
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/end2end/fake_resolver.h"
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

class RoundRobinEnd2endTest : public ::testing::Test {
 protected:
  struct AddressData {
    bool drop = false;
    int port = 0;
  };

  RoundRobinEnd2endTest() : server_host_("localhost") {}

  void StartServers(int num_servers) {
    for (int i = 0; i < num_servers; ++i) {
      servers_.emplace_back(new ServerData(server_host_));
    }
  }

  void SetUp() override {
    response_generator_ = grpc_fake_resolver_response_generator_create();
  }

  void TearDown() override {
    for (size_t i = 0; i < servers_.size(); ++i) {
      servers_[i]->Shutdown();
    }
    grpc_fake_resolver_response_generator_unref(response_generator_);
  }

  void ResetStub(bool round_robin, int num_drop_addresses) {
    ChannelArguments args;
    if (round_robin) args.SetLoadBalancingPolicyName("round_robin");
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_);
    channel_ = CreateCustomChannel("test:///not_used",
                                   InsecureChannelCredentials(), args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
    std::vector<AddressData> address_data;
    for (int i = 0; i < num_drop_addresses; ++i) {
      AddressData address;
      address.drop = true;
      address_data.push_back(address);
    }
    for (const auto& server : servers_) {
      AddressData address;
      address.port = server->port_;
      address_data.push_back(address);
    }
    SetNextResolution(address_data);
  }

  void SetNextResolution(const std::vector<AddressData>& address_data) {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_lb_addresses* addresses =
        grpc_lb_addresses_create(address_data.size(), nullptr);
    for (size_t i = 0; i < address_data.size(); ++i) {
      if (address_data[i].drop) {
        grpc_lb_addresses_set_drop_address(addresses, i,
                                           nullptr /* user_data */);
      } else {
        char* uri_str;
        gpr_asprintf(&uri_str, "ipv4:127.0.0.1:%d", address_data[i].port);
        grpc_uri* uri = grpc_uri_parse(&exec_ctx, uri_str, true);
        GPR_ASSERT(uri != nullptr);
        grpc_lb_addresses_set_address_from_uri(
            addresses, i, uri, false /* is_balancer */,
            nullptr /* balancer_name */, nullptr /* user_data */);
        grpc_uri_destroy(uri);
        gpr_free(uri_str);
      }
    }
    grpc_arg fake_addresses_arg =
        grpc_lb_addresses_create_channel_arg(addresses);
    grpc_channel_args fake_result = {1, &fake_addresses_arg};
    grpc_fake_resolver_response_generator_set_response(
        &exec_ctx, response_generator_, &fake_result);
    grpc_lb_addresses_destroy(&exec_ctx, addresses);
    grpc_exec_ctx_finish(&exec_ctx);
  }

  void SendRpc(int num_rpcs, bool expect_drop) {
    EchoRequest request;
    EchoResponse response;
    request.set_message("Live long and prosper.");
    for (int i = 0; i < num_rpcs; i++) {
      ClientContext context;
      Status status = stub_->Echo(&context, request, &response);
      gpr_log(GPR_INFO, "sent RPC to peer %s", context.peer().c_str());
      if (expect_drop) {
        EXPECT_FALSE(status.ok());
        EXPECT_EQ("Call dropped by load balancing policy",
                  status.error_message());
      } else {
        EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                                 << " message=" << status.error_message();
        EXPECT_EQ(response.message(), request.message());
      }
    }
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

  const grpc::string server_host_;
  CompletionQueue cli_cq_;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::vector<std::unique_ptr<ServerData>> servers_;
  grpc_fake_resolver_response_generator* response_generator_;
};

TEST_F(RoundRobinEnd2endTest, PickFirst) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ResetStub(false /* round_robin */, 0 /* num_drop_addresses */);
  SendRpc(kNumServers, false /* expect_drop */);
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

TEST_F(RoundRobinEnd2endTest, PickFirstWithDropAddress) {
  // Start servers and send one RPC per server.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ResetStub(false /* round_robin */, 1 /* num_drop_addresses */);
  SendRpc(kNumServers, false /* expect_drop */);
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
  ResetStub(true /* round_robin */, 0 /* num_drop_addresses */);
  SendRpc(kNumServers, false /* expect_drop */);
  // One request should have gone to each server.
  for (size_t i = 0; i < servers_.size(); ++i) {
    EXPECT_EQ(1, servers_[i]->service_.request_count());
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel_->GetLoadBalancingPolicyName());
}

TEST_F(RoundRobinEnd2endTest, RoundRobinWithDropAddress) {
  // Start servers and send one RPC per server, plus one for the drop address.
  const int kNumServers = 3;
  StartServers(kNumServers);
  ResetStub(true /* round_robin */, 1 /* num_drop_addresses */);
  // Try to connect before sending RPCs, to avoid non-determinism caused
  // by races between connection establishment and RPCs being sent.
  channel_->GetState(true /* try_to_connect */);
  SendRpc(1, true /* expect_drop */);
  SendRpc(kNumServers, false /* expect_drop */);
  // One request should have gone to each server.
  for (size_t i = 0; i < servers_.size(); ++i) {
    EXPECT_EQ(1, servers_[i]->service_.request_count());
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("round_robin", channel_->GetLoadBalancingPolicyName());
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_init();
  grpc_test_init(argc, argv);
  grpc_fake_resolver_init();
  ::testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
