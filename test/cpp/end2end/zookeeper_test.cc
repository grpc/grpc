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

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/grpc_zookeeper.h>
#include <gtest/gtest.h>
#include <zookeeper/zookeeper.h>

#include "src/core/lib/support/env.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;

namespace grpc {
namespace testing {

class ZookeeperTestServiceImpl
    : public ::grpc::testing::EchoTestService::Service {
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
    SetUpZookeeper();

    // Sets up two servers
    int port1 = grpc_pick_unused_port_or_die();
    server1_ = SetUpServer(port1);

    int port2 = grpc_pick_unused_port_or_die();
    server2_ = SetUpServer(port2);

    // Registers service /test in zookeeper
    RegisterService("/test", "test");

    // Registers service instance /test/1 in zookeeper
    string value =
        "{\"host\":\"localhost\",\"port\":\"" + to_string(port1) + "\"}";
    RegisterService("/test/1", value);

    // Registers service instance /test/2 in zookeeper
    value = "{\"host\":\"localhost\",\"port\":\"" + to_string(port2) + "\"}";
    RegisterService("/test/2", value);
  }

  // Requires zookeeper server running
  void SetUpZookeeper() {
    // Finds zookeeper server address in environment
    // Default is localhost:2181
    zookeeper_address_ = "localhost:2181";
    char* addr = gpr_getenv("GRPC_ZOOKEEPER_SERVER_TEST");
    if (addr != NULL) {
      string addr_str(addr);
      zookeeper_address_ = addr_str;
      gpr_free(addr);
    }
    gpr_log(GPR_DEBUG, "%s", zookeeper_address_.c_str());

    // Connects to zookeeper server
    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
    zookeeper_handle_ =
        zookeeper_init(zookeeper_address_.c_str(), NULL, 15000, 0, 0, 0);
    GPR_ASSERT(zookeeper_handle_ != NULL);

    // Registers zookeeper name resolver in grpc
    grpc_zookeeper_register();
  }

  std::unique_ptr<Server> SetUpServer(const int port) {
    string server_address = "localhost:" + to_string(port);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, InsecureServerCredentials());
    builder.RegisterService(&service_);
    std::unique_ptr<Server> server = builder.BuildAndStart();
    return server;
  }

  void RegisterService(const string& name, const string& value) {
    char* path = (char*)gpr_malloc(name.size());

    int status = zoo_exists(zookeeper_handle_, name.c_str(), 0, NULL);
    if (status == ZNONODE) {
      status =
          zoo_create(zookeeper_handle_, name.c_str(), value.c_str(),
                     value.size(), &ZOO_OPEN_ACL_UNSAFE, 0, path, name.size());
    } else {
      status = zoo_set(zookeeper_handle_, name.c_str(), value.c_str(),
                       value.size(), -1);
    }
    gpr_free(path);
    GPR_ASSERT(status == 0);
  }

  void DeleteService(const string& name) {
    int status = zoo_delete(zookeeper_handle_, name.c_str(), -1);
    GPR_ASSERT(status == 0);
  }

  void ChangeZookeeperState() {
    server1_->Shutdown();
    DeleteService("/test/1");
  }

  void TearDown() GRPC_OVERRIDE {
    server1_->Shutdown();
    server2_->Shutdown();
    zookeeper_close(zookeeper_handle_);
  }

  void ResetStub() {
    string target = "zookeeper://" + zookeeper_address_ + "/test";
    channel_ = CreateChannel(target, InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  string to_string(const int number) {
    std::stringstream strs;
    strs << number;
    return strs.str();
  }

  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server1_;
  std::unique_ptr<Server> server2_;
  ZookeeperTestServiceImpl service_;
  zhandle_t* zookeeper_handle_;
  string zookeeper_address_;
};

// Tests zookeeper state change between two RPCs
// TODO(ctiller): leaked objects in this test
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

  // Zookeeper state changes
  gpr_log(GPR_DEBUG, "Zookeeper state change");
  ChangeZookeeperState();
  // Waits for re-resolving addresses
  // TODO(ctiller): RPC will probably fail if not waiting
  sleep(1);

  // Second RPC
  EchoRequest request2;
  EchoResponse response2;
  ClientContext context2;
  context2.set_authority("test");
  request2.set_message("World");
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
