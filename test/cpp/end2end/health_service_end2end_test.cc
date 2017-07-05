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
#include <vector>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/ext/health_check_service_server_builder_option.h>
#include <grpc++/health_check_service_interface.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/proto/grpc/health/v1/health.grpc.pb.h"
#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

#include <gtest/gtest.h>

using grpc::health::v1::Health;
using grpc::health::v1::HealthCheckRequest;
using grpc::health::v1::HealthCheckResponse;

namespace grpc {
namespace testing {
namespace {

// A sample sync implementation of the health checking service. This does the
// same thing as the default one.
class HealthCheckServiceImpl : public ::grpc::health::v1::Health::Service {
 public:
  Status Check(ServerContext* context, const HealthCheckRequest* request,
               HealthCheckResponse* response) override {
    std::lock_guard<std::mutex> lock(mu_);
    auto iter = status_map_.find(request->service());
    if (iter == status_map_.end()) {
      return Status(StatusCode::NOT_FOUND, "");
    }
    response->set_status(iter->second);
    return Status::OK;
  }

  void SetStatus(const grpc::string& service_name,
                 HealthCheckResponse::ServingStatus status) {
    std::lock_guard<std::mutex> lock(mu_);
    status_map_[service_name] = status;
  }

  void SetAll(HealthCheckResponse::ServingStatus status) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto iter = status_map_.begin(); iter != status_map_.end(); ++iter) {
      iter->second = status;
    }
  }

 private:
  std::mutex mu_;
  std::map<const grpc::string, HealthCheckResponse::ServingStatus> status_map_;
};

// A custom implementation of the health checking service interface. This is
// used to test that it prevents the server from creating a default service and
// also serves as an example of how to override the default service.
class CustomHealthCheckService : public HealthCheckServiceInterface {
 public:
  explicit CustomHealthCheckService(HealthCheckServiceImpl* impl)
      : impl_(impl) {
    impl_->SetStatus("", HealthCheckResponse::SERVING);
  }
  void SetServingStatus(const grpc::string& service_name,
                        bool serving) override {
    impl_->SetStatus(service_name, serving ? HealthCheckResponse::SERVING
                                           : HealthCheckResponse::NOT_SERVING);
  }

  void SetServingStatus(bool serving) override {
    impl_->SetAll(serving ? HealthCheckResponse::SERVING
                          : HealthCheckResponse::NOT_SERVING);
  }

 private:
  HealthCheckServiceImpl* impl_;  // not owned
};

void LoopCompletionQueue(ServerCompletionQueue* cq) {
  void* tag;
  bool ok;
  while (cq->Next(&tag, &ok)) {
    abort();  // Nothing should come out of the cq.
  }
}

class HealthServiceEnd2endTest : public ::testing::Test {
 protected:
  HealthServiceEnd2endTest() {}

  void SetUpServer(bool register_sync_test_service, bool add_async_cq,
                   bool explicit_health_service,
                   std::unique_ptr<HealthCheckServiceInterface> service) {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;

    bool register_sync_health_service_impl =
        explicit_health_service && service != nullptr;

    // Setup server
    ServerBuilder builder;
    if (explicit_health_service) {
      std::unique_ptr<ServerBuilderOption> option(
          new HealthCheckServiceServerBuilderOption(std::move(service)));
      builder.SetOption(std::move(option));
    }
    builder.AddListeningPort(server_address_.str(),
                             grpc::InsecureServerCredentials());
    if (register_sync_test_service) {
      // Register a sync service.
      builder.RegisterService(&echo_test_service_);
    }
    if (register_sync_health_service_impl) {
      builder.RegisterService(&health_check_service_impl_);
    }
    if (add_async_cq) {
      cq_ = builder.AddCompletionQueue();
    }
    server_ = builder.BuildAndStart();
  }

  void TearDown() override {
    if (server_) {
      server_->Shutdown();
      if (cq_ != nullptr) {
        cq_->Shutdown();
      }
      if (cq_thread_.joinable()) {
        cq_thread_.join();
      }
    }
  }

  void ResetStubs() {
    std::shared_ptr<Channel> channel =
        CreateChannel(server_address_.str(), InsecureChannelCredentials());
    hc_stub_ = grpc::health::v1::Health::NewStub(channel);
  }

  // When the expected_status is NOT OK, we do not care about the response.
  void SendHealthCheckRpc(const grpc::string& service_name,
                          const Status& expected_status) {
    EXPECT_FALSE(expected_status.ok());
    SendHealthCheckRpc(service_name, expected_status,
                       HealthCheckResponse::UNKNOWN);
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
    if (s.ok()) {
      EXPECT_EQ(expected_serving_status, response.status());
    }
  }

  void VerifyHealthCheckService() {
    HealthCheckServiceInterface* service = server_->GetHealthCheckService();
    EXPECT_TRUE(service != nullptr);
    const grpc::string kHealthyService("healthy_service");
    const grpc::string kUnhealthyService("unhealthy_service");
    const grpc::string kNotRegisteredService("not_registered");
    service->SetServingStatus(kHealthyService, true);
    service->SetServingStatus(kUnhealthyService, false);

    ResetStubs();

    SendHealthCheckRpc("", Status::OK, HealthCheckResponse::SERVING);
    SendHealthCheckRpc(kHealthyService, Status::OK,
                       HealthCheckResponse::SERVING);
    SendHealthCheckRpc(kUnhealthyService, Status::OK,
                       HealthCheckResponse::NOT_SERVING);
    SendHealthCheckRpc(kNotRegisteredService,
                       Status(StatusCode::NOT_FOUND, ""));

    service->SetServingStatus(false);
    SendHealthCheckRpc("", Status::OK, HealthCheckResponse::NOT_SERVING);
    SendHealthCheckRpc(kHealthyService, Status::OK,
                       HealthCheckResponse::NOT_SERVING);
    SendHealthCheckRpc(kUnhealthyService, Status::OK,
                       HealthCheckResponse::NOT_SERVING);
    SendHealthCheckRpc(kNotRegisteredService,
                       Status(StatusCode::NOT_FOUND, ""));
  }

  TestServiceImpl echo_test_service_;
  HealthCheckServiceImpl health_check_service_impl_;
  std::unique_ptr<Health::Stub> hc_stub_;
  std::unique_ptr<ServerCompletionQueue> cq_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  std::thread cq_thread_;
};

TEST_F(HealthServiceEnd2endTest, DefaultHealthServiceDisabled) {
  EnableDefaultHealthCheckService(false);
  EXPECT_FALSE(DefaultHealthCheckServiceEnabled());
  SetUpServer(true, false, false, nullptr);
  HealthCheckServiceInterface* default_service =
      server_->GetHealthCheckService();
  EXPECT_TRUE(default_service == nullptr);

  ResetStubs();

  SendHealthCheckRpc("", Status(StatusCode::UNIMPLEMENTED, ""));
}

TEST_F(HealthServiceEnd2endTest, DefaultHealthService) {
  EnableDefaultHealthCheckService(true);
  EXPECT_TRUE(DefaultHealthCheckServiceEnabled());
  SetUpServer(true, false, false, nullptr);
  VerifyHealthCheckService();

  // The default service has a size limit of the service name.
  const grpc::string kTooLongServiceName(201, 'x');
  SendHealthCheckRpc(kTooLongServiceName,
                     Status(StatusCode::INVALID_ARGUMENT, ""));
}

// The server has no sync service.
TEST_F(HealthServiceEnd2endTest, DefaultHealthServiceAsyncOnly) {
  EnableDefaultHealthCheckService(true);
  EXPECT_TRUE(DefaultHealthCheckServiceEnabled());
  SetUpServer(false, true, false, nullptr);
  cq_thread_ = std::thread(LoopCompletionQueue, cq_.get());

  HealthCheckServiceInterface* default_service =
      server_->GetHealthCheckService();
  EXPECT_TRUE(default_service == nullptr);

  ResetStubs();

  SendHealthCheckRpc("", Status(StatusCode::UNIMPLEMENTED, ""));
}

// Provide an empty service to disable the default service.
TEST_F(HealthServiceEnd2endTest, ExplicitlyDisableViaOverride) {
  EnableDefaultHealthCheckService(true);
  EXPECT_TRUE(DefaultHealthCheckServiceEnabled());
  std::unique_ptr<HealthCheckServiceInterface> empty_service;
  SetUpServer(true, false, true, std::move(empty_service));
  HealthCheckServiceInterface* service = server_->GetHealthCheckService();
  EXPECT_TRUE(service == nullptr);

  ResetStubs();

  SendHealthCheckRpc("", Status(StatusCode::UNIMPLEMENTED, ""));
}

// Provide an explicit override of health checking service interface.
TEST_F(HealthServiceEnd2endTest, ExplicitlyOverride) {
  EnableDefaultHealthCheckService(true);
  EXPECT_TRUE(DefaultHealthCheckServiceEnabled());
  std::unique_ptr<HealthCheckServiceInterface> override_service(
      new CustomHealthCheckService(&health_check_service_impl_));
  HealthCheckServiceInterface* underlying_service = override_service.get();
  SetUpServer(false, false, true, std::move(override_service));
  HealthCheckServiceInterface* service = server_->GetHealthCheckService();
  EXPECT_TRUE(service == underlying_service);

  ResetStubs();

  VerifyHealthCheckService();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
