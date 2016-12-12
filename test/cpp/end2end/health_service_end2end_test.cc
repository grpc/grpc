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
#include <thread>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/health_check_service_interface.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/proto/grpc/health/v1/health.grpc.pb.h"
#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

using grpc::health::v1::Health;
using grpc::health::v1::HealthCheckRequest;
using grpc::health::v1::HealthCheckResponse;

namespace grpc {
namespace testing {
namespace {

class HealthServiceEnd2endTest : public ::testing::Test {
 protected:
  HealthServiceEnd2endTest() {}

  void SetUpServer(grpc::Service* explicit_health_service) {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;

    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             grpc::InsecureServerCredentials());
    // Register a sync service.
    builder.RegisterService(&echo_test_service_);
    server_ = builder.BuildAndStart();
  }

  void TearDown() override {
    if (server_) {
      server_->Shutdown();
    }
  }

  void ResetStubs() {
    std::shared_ptr<Channel> channel =
        CreateChannel(server_address_.str(), InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel);
    hc_stub_ = grpc::health::v1::Health::NewStub(channel);
  }

  void SendHealthCheckRpc(
      const grpc::string& service_name, const Status& expected_status,
      HealthCheckResponse::ServingStatus expected_serving_status) {
    HealthCheckRequest request;
    request.set_service(service_name);
    HealthCheckResponse response;
    ClientContext context;
    Status s = hc_stub_->Check(&context, request, &response);
    EXPECT_EQ(expected_status.error_code(), s.error_code());
    //    EXPECT_EQ(expected_status.error_details(), s.error_details());
    if (s.ok()) {
      EXPECT_EQ(expected_serving_status, response.status());
    }
  }

  TestServiceImpl echo_test_service_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Health::Stub> hc_stub_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
};

TEST_F(HealthServiceEnd2endTest, DefaultHealthServiceDisabled) {
  EnableDefaultHealthCheckService(false);
  EXPECT_FALSE(DefaultHealthCheckServiceEnabled());
  SetUpServer(nullptr);
  HealthCheckServiceInterface* default_service =
      server_->GetHealthCheckService();
  EXPECT_TRUE(default_service == nullptr);
}

TEST_F(HealthServiceEnd2endTest, DefaultHealthService) {
  EnableDefaultHealthCheckService(true);
  EXPECT_TRUE(DefaultHealthCheckServiceEnabled());
  SetUpServer(nullptr);
  HealthCheckServiceInterface* default_service =
      server_->GetHealthCheckService();
  EXPECT_TRUE(default_service != nullptr);
  const grpc::string kHealthyService("healthy_service");
  const grpc::string kUnhealthyService("unhealthy_service");
  default_service->SetServingStatus(kHealthyService, true);
  default_service->SetServingStatus(kUnhealthyService, false);

  ResetStubs();

  SendHealthCheckRpc("", Status::OK, HealthCheckResponse::SERVING);
  SendHealthCheckRpc(kHealthyService, Status::OK, HealthCheckResponse::SERVING);
  SendHealthCheckRpc(kUnhealthyService, Status::OK,
                     HealthCheckResponse::NOT_SERVING);

  default_service->SetServingStatus(false);
  SendHealthCheckRpc("", Status::OK, HealthCheckResponse::NOT_SERVING);
  SendHealthCheckRpc(kHealthyService, Status::OK,
                     HealthCheckResponse::NOT_SERVING);
  SendHealthCheckRpc(kUnhealthyService, Status::OK,
                     HealthCheckResponse::NOT_SERVING);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
