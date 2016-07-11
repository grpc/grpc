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

#include <thread>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/sync.h>
#include <gtest/gtest.h>

#include "src/core/lib/support/env.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;

namespace grpc {
namespace testing {

class TestServiceImpl : public ::grpc::testing::EchoTestService::Service {
 public:
  explicit TestServiceImpl(gpr_event* ev) : ev_(ev) {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) GRPC_OVERRIDE {
    gpr_event_set(ev_, (void*)1);
    while (!context->IsCancelled()) {
    }
    return Status::OK;
  }

 private:
  gpr_event* ev_;
};

class ShutdownTest : public ::testing::Test {
 public:
  ShutdownTest() : shutdown_(false), service_(&ev_) { gpr_event_init(&ev_); }

  void SetUp() GRPC_OVERRIDE {
    port_ = grpc_pick_unused_port_or_die();
    server_ = SetUpServer(port_);
  }

  std::unique_ptr<Server> SetUpServer(const int port) {
    grpc::string server_address = "localhost:" + to_string(port);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, InsecureServerCredentials());
    builder.RegisterService(&service_);
    std::unique_ptr<Server> server = builder.BuildAndStart();
    return server;
  }

  void TearDown() GRPC_OVERRIDE { GPR_ASSERT(shutdown_); }

  void ResetStub() {
    string target = "dns:localhost:" + to_string(port_);
    channel_ = CreateChannel(target, InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  string to_string(const int number) {
    std::stringstream strs;
    strs << number;
    return strs.str();
  }

  void SendRequest() {
    EchoRequest request;
    EchoResponse response;
    request.set_message("Hello");
    ClientContext context;
    GPR_ASSERT(!shutdown_);
    Status s = stub_->Echo(&context, request, &response);
    GPR_ASSERT(shutdown_);
  }

 protected:
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  bool shutdown_;
  int port_;
  gpr_event ev_;
  TestServiceImpl service_;
};

// TODO(ctiller): leaked objects in this test
TEST_F(ShutdownTest, ShutdownTest) {
  ResetStub();

  // send the request in a background thread
  std::thread thr(std::bind(&ShutdownTest::SendRequest, this));

  // wait for the server to get the event
  gpr_event_wait(&ev_, gpr_inf_future(GPR_CLOCK_MONOTONIC));

  shutdown_ = true;

  // shutdown should trigger cancellation causing everything to shutdown
  auto deadline =
      std::chrono::system_clock::now() + std::chrono::microseconds(100);
  server_->Shutdown(deadline);
  EXPECT_GE(std::chrono::system_clock::now(), deadline);

  thr.join();
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
