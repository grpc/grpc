//
//
// Copyright 2024 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "test/cpp/interop/backend_metrics_lb_policy.h"

#include <grpc/grpc.h>
#include <grpcpp/ext/call_metric_recorder.h>
#include <grpcpp/ext/orca_service.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>

#include <memory>
#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/config/config_vars.h"
#include "src/core/util/sync.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace testing {
namespace {

class EchoServiceImpl : public grpc::testing::TestService::CallbackService {
 public:
  grpc::ServerUnaryReactor* UnaryCall(
      grpc::CallbackServerContext* context,
      const grpc::testing::SimpleRequest* /* request */,
      grpc::testing::SimpleResponse* /* response */) override {
    auto reactor = context->DefaultReactor();
    reactor->Finish(grpc::Status::OK);
    return reactor;
  }
};

class Server {
 public:
  Server() : port_(grpc_pick_unused_port_or_die()) {
    server_thread_ = std::thread(ServerLoop, this);
    grpc_core::MutexLock lock(&mu_);
    cond_.WaitWithTimeout(&mu_, absl::Seconds(1));
  }

  ~Server() {
    server_->Shutdown();
    server_thread_.join();
  }

  std::string address() const { return absl::StrCat("localhost:", port_); }

 private:
  static void ServerLoop(Server* server) { server->Run(); }

  void Run() {
    ServerBuilder builder;
    EchoServiceImpl service;
    auto server_metric_recorder =
        grpc::experimental::ServerMetricRecorder::Create();
    server_metric_recorder->SetCpuUtilization(.5f);
    grpc::experimental::OrcaService orca_service(
        server_metric_recorder.get(),
        grpc::experimental::OrcaService::Options().set_min_report_duration(
            absl::Seconds(1)));
    builder.RegisterService(&orca_service);
    builder.RegisterService(&service);
    builder.AddListeningPort(address(), InsecureServerCredentials());
    auto grpc_server = builder.BuildAndStart();
    server_ = grpc_server.get();
    {
      grpc_core::MutexLock lock(&mu_);
      cond_.SignalAll();
    }
    grpc_server->Wait();
  }

  int port_;
  grpc_core::Mutex mu_;
  grpc_core::CondVar cond_;
  std::thread server_thread_;
  grpc::Server* server_;
};

TEST(BackendMetricsLbPolicyTest, TestOobMetricsReceipt) {
  LoadReportTracker tracker;
  grpc_core::CoreConfiguration::RegisterBuilder(RegisterBackendMetricsLbPolicy);
  Server server;
  ChannelArguments args = tracker.GetChannelArguments();
  args.SetLoadBalancingPolicyName("test_backend_metrics_load_balancer");
  auto channel = grpc::CreateCustomChannel(server.address(),
                                           InsecureChannelCredentials(), args);
  auto stub = grpc::testing::TestService::Stub(channel);
  ClientContext ctx;
  SimpleRequest req;
  SimpleResponse res;
  grpc_core::Mutex mu;
  grpc_core::CondVar cond;
  std::optional<Status> status;

  stub.async()->UnaryCall(&ctx, &req, &res, [&](auto s) {
    grpc_core::MutexLock lock(&mu);
    status = s;
    cond.SignalAll();
  });
  // This report is sent on start, available immediately
  auto report = tracker.WaitForOobLoadReport(
      [](auto report) { return report.cpu_utilization() == 0.5; },
      absl::Seconds(5) * grpc_test_slowdown_factor(), 3);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->cpu_utilization(), 0.5);
  for (size_t i = 0; i < 3; i++) {
    // Wait for slightly more than 1 min
    report = tracker.WaitForOobLoadReport(
        [](auto report) { return report.cpu_utilization() == 0.5; },
        absl::Milliseconds(1500), 3);
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->cpu_utilization(), 0.5);
  }
  {
    grpc_core::MutexLock lock(&mu);
    if (!status.has_value()) {
      cond.Wait(&mu);
    }
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->error_code(), grpc::StatusCode::OK);
  }
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
