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
#include <grpc/grpc_zookeeper.h>
#include <zookeeper/zookeeper.h>

using grpc::cpp::test::util::EchoRequest;
using grpc::cpp::test::util::EchoResponse;

namespace grpc {
namespace testing {

class ZookeeperTestServiceImpl
    : public ::grpc::cpp::test::util::TestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) GRPC_OVERRIDE {
    response->set_message(request->message());
    return Status::OK;
  }
};

class ZookeeperTest : public ::testing::Test {
 protected:
  ZookeeperTest() {}

  void SetUp() GRPC_OVERRIDE {
    int port = grpc_pick_unused_port_or_die();
    server_address_ = "localhost:" + std::to_string(port);

    // Setup zookeeper
    // Require zookeeper server running in Jenkins master
    const char* zookeeper_address = "localhost:2181";
    ZookeeperSetUp(zookeeper_address, port);

    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_, InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  void ZookeeperSetUp(const char* zookeeper_address, int port) {
    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
    zookeeper_handle = zookeeper_init(zookeeper_address, NULL, 15000, 0, 0, 0);
    GPR_ASSERT(zookeeper_handle != NULL);

    char service_path[] = "/test";
    char service_value[] = "test";

    int status = zoo_exists(zookeeper_handle, service_path, 0, NULL);
    if (status != 0) {
      status = zoo_create(zookeeper_handle, service_path, service_value,
                          strlen(service_value), &ZOO_OPEN_ACL_UNSAFE, 0,
                          service_path, strlen(service_path));
      GPR_ASSERT(status == 0);
    }

    char instance_path[] = "/test/1";
    string instance_value =
        "{\"host\":\"localhost\",\"port\":\"" + std::to_string(port) + "\"}";
    status = zoo_create(zookeeper_handle, instance_path, instance_value.c_str(),
                        instance_value.size(), &ZOO_OPEN_ACL_UNSAFE,
                        ZOO_EPHEMERAL, instance_path, sizeof(instance_path));
    GPR_ASSERT(status == 0);

    grpc_zookeeper_register();
  }

  void TearDown() GRPC_OVERRIDE {
    server_->Shutdown();
    zookeeper_close(zookeeper_handle);
  }

  void ResetStub() {
    channel_ = CreateChannel("zookeeper://localhost:2181/test",
                             InsecureCredentials(), ChannelArguments());
    stub_ = std::move(grpc::cpp::test::util::TestService::NewStub(channel_));
  }

  std::shared_ptr<ChannelInterface> channel_;
  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::string server_address_;
  ZookeeperTestServiceImpl service_;
  zhandle_t* zookeeper_handle;
};

// Send a simple echo RPC
TEST_F(ZookeeperTest, SimpleRpc) {
  ResetStub();
  // Normal stub.
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
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
