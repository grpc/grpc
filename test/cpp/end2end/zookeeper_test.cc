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
    // Require zookeeper server running in grpc-jenkins-master
    zookeeper_address = "localhost:2181";
    char* addr = gpr_getenv("GRPC_ZOOKEEPER_SERVER_TEST");
    if (addr != NULL) {
      string addr_str(addr);
      zookeeper_address = addr_str;
      gpr_free(addr);
    }
    ZookeeperSetUp(zookeeper_address.c_str(), port);

    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_, InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  void ZookeeperSetUp(const char* zookeeper_address, int port) {
    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
    gpr_log(GPR_DEBUG, zookeeper_address);
    zookeeper_handle_ = zookeeper_init(zookeeper_address, NULL, 15000, 0, 0, 0);
    GPR_ASSERT(zookeeper_handle_ != NULL);

    // Register service /test in zookeeper
    char service_path[] = "/test";
    char service_value[] = "test";
    int status = zoo_exists(zookeeper_handle_, service_path, 0, NULL);
    if (status != 0) {
      status = zoo_create(zookeeper_handle_, service_path, service_value,
                          strlen(service_value), &ZOO_OPEN_ACL_UNSAFE, 0,
                          service_path, sizeof(service_path));
      GPR_ASSERT(status == 0);
    }

    // Register service instance /test/1 in zookeeper
    char instance_path[] = "/test/1";
    string instance_value =
        "{\"host\":\"localhost\",\"port\":\"" + std::to_string(port) + "\"}";
    status = zoo_exists(zookeeper_handle_, instance_path, 0, NULL);
    if (status == ZNONODE) {
      status =
          zoo_create(zookeeper_handle_, instance_path, instance_value.c_str(),
                     instance_value.size(), &ZOO_OPEN_ACL_UNSAFE, 0,
                     instance_path, sizeof(instance_path));
      GPR_ASSERT(status == 0);
    } else {
      status = zoo_set(zookeeper_handle_, instance_path, instance_value.c_str(),
                       instance_value.size(), -1);
      GPR_ASSERT(status == 0);
    }
    GPR_ASSERT(status == 0);

    // Register zookeeper name resolver in grpc
    grpc_zookeeper_register();
  }

  void ZookeeperStateChange() {
    char instance_path[] = "/test/2";
    string instance_value = "2222";

    int status = zoo_exists(zookeeper_handle_, instance_path, 0, NULL);
    if (status == ZNONODE) {
      status =
          zoo_create(zookeeper_handle_, instance_path, instance_value.c_str(),
                     instance_value.size(), &ZOO_OPEN_ACL_UNSAFE, 0,
                     instance_path, sizeof(instance_path));
      GPR_ASSERT(status == 0);
    } else {
      status = zoo_delete(zookeeper_handle_, instance_path, -1);
      GPR_ASSERT(status == 0);
    }
  }

  void TearDown() GRPC_OVERRIDE {
    server_->Shutdown();
    zookeeper_close(zookeeper_handle_);
  }

  void ResetStub() {
    string target = "zookeeper://" + zookeeper_address + "/test";
    channel_ = CreateChannel(target, InsecureCredentials(), ChannelArguments());
    stub_ = std::move(grpc::cpp::test::util::TestService::NewStub(channel_));
  }

  std::shared_ptr<ChannelInterface> channel_;
  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  string server_address_;
  ZookeeperTestServiceImpl service_;
  zhandle_t* zookeeper_handle_;
  string zookeeper_address;
};

// Test zookeeper state change between two RPCs
TEST_F(ZookeeperTest, ZookeeperStateChangeTwoRpc) {
  ResetStub();

  // First RPC
  EchoRequest request1;
  EchoResponse response1;
  ClientContext context1;
  context1.set_authority("test");
  request1.set_message("Hello");
  Status s1 = stub_->Echo(&context1, request1, &response1);
  EXPECT_EQ(response1.message(), request1.message());
  EXPECT_TRUE(s1.ok());

  // Zookeeper state change
  ZookeeperStateChange();
  sleep(1);

  // Second RPC
  EchoRequest request2;
  EchoResponse response2;
  ClientContext context2;
  context2.set_authority("test");
  request2.set_message("Hello");
  Status s2 = stub_->Echo(&context2, request2, &response2);
  EXPECT_EQ(response2.message(), request2.message());
  EXPECT_TRUE(s2.ok());
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
