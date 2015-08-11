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

#include "test/core/util/test_config.h"
#include "test/core/util/port.h"
#include "test/cpp/util/echo.grpc.pb.h"
#include "src/core/support/env.h"
#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/server_credentials.h>
#include <grpc++/status.h>
#include <gtest/gtest.h>
#include <grpc/grpc.h>
#include <grpc/grpc_etcd.h>

using grpc::cpp::test::util::EchoRequest;
using grpc::cpp::test::util::EchoResponse;

namespace grpc {
namespace testing {

class EtcdTestServiceImpl
    : public ::grpc::cpp::test::util::TestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) GRPC_OVERRIDE {
    response->set_message(request->message());
    return Status::OK;
  }
};

class EtcdTest : public ::testing::Test {
 protected:
  EtcdTest() {}

  void SetUp() GRPC_OVERRIDE {
    SetUpEtcd();

    // Setup two servers
    int port1 = grpc_pick_unused_port_or_die();
    int port2 = grpc_pick_unused_port_or_die();
    server1_ = SetUpServer(port1);
    server2_ = SetUpServer(port2);

    // Register service /test in zookeeper
    RegisterService("/test", "test");

    // Register service instance /test/1 in etcd
    string value =
        "{\"host\":\"localhost\",\"port\":\"" + std::to_string(port1) + "\"}";
    RegisterService("/test/1", value);

    // Register service instance /test/2 in etcd
    value =
        "{\"host\":\"localhost\",\"port\":\"" + std::to_string(port2) + "\"}";
    RegisterService("/test/2", value);
  }

  std::unique_ptr<Server> SetUpServer(int port) {
    string server_address = "localhost:" + std::to_string(port);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, InsecureServerCredentials());
    builder.RegisterService(&service_);
    std::unique_ptr<Server> server = builder.BuildAndStart();
    return server;
  }

  // Require etcd server running beforehand
  void SetUpEtcd() {
    // Find etcd server address in environment
    // Default is localhost:4001
    etcd_address_ = "localhost:4001";
    char* addr = gpr_getenv("GRPC_ETCD_SERVER_TEST");
    if (addr != NULL) {
      string addr_str(addr);
      etcd_address_ = addr_str;
      gpr_free(addr);
    }
    gpr_log(GPR_DEBUG, etcd_address_.c_str());

    // Register etcd name resolver in grpc
    grpc_etcd_register();
  }

  void RegisterService(string name, string value) {}

  void TearDown() GRPC_OVERRIDE {
    server1_->Shutdown();
    server2_->Shutdown();
  }

  void ResetStub() {
    string target = "etcd://" + etcd_address_ + "/test";
    channel_ = CreateChannel(target, InsecureCredentials(), ChannelArguments());
    stub_ = std::move(grpc::cpp::test::util::TestService::NewStub(channel_));
  }

  std::shared_ptr<ChannelInterface> channel_;
  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub_;
  std::unique_ptr<Server> server1_;
  std::unique_ptr<Server> server2_;
  EtcdTestServiceImpl service_;
  string etcd_address_;
};

// Simple echo RPC
TEST_F(EtcdTest, SimpleRpc) {
  ResetStub();

  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_authority("test");
  request.set_message("Hello");
  Status s = stub_->Echo(&context, request, &response);
  gpr_log(GPR_DEBUG, "response: %s", response.message().c_str());
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
