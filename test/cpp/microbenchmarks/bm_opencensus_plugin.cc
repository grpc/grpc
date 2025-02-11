//
//
// Copyright 2018 gRPC authors.
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

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/opencensus.h>

#include <string>
#include <thread>  // NOLINT

#include "absl/base/call_once.h"
#include "absl/strings/str_cat.h"
#include "opencensus/stats/stats.h"
#include "src/core/config/core_configuration.h"
#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"

absl::once_flag once;
void RegisterOnce() { absl::call_once(once, grpc::RegisterOpenCensusPlugin); }

class EchoServer final : public grpc::testing::EchoTestService::Service {
  grpc::Status Echo(grpc::ServerContext* /*context*/,
                    const grpc::testing::EchoRequest* request,
                    grpc::testing::EchoResponse* response) override {
    if (request->param().expected_error().code() == 0) {
      response->set_message(request->message());
      return grpc::Status::OK;
    } else {
      return grpc::Status(static_cast<grpc::StatusCode>(
                              request->param().expected_error().code()),
                          "");
    }
  }
};

// An EchoServerThread object creates an EchoServer on a separate thread and
// shuts down the server and thread when it goes out of scope.
class EchoServerThread final {
 public:
  EchoServerThread() {
    grpc::ServerBuilder builder;
    int port;
    builder.AddListeningPort("[::]:0", grpc::InsecureServerCredentials(),
                             &port);
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    if (server_ == nullptr || port == 0) {
      std::abort();
    }
    server_address_ = absl::StrCat("[::]:", port);
    server_thread_ = std::thread(&EchoServerThread::RunServerLoop, this);
  }

  ~EchoServerThread() {
    server_->Shutdown();
    server_thread_.join();
  }

  const std::string& address() { return server_address_; }

 private:
  void RunServerLoop() { server_->Wait(); }

  std::string server_address_;
  EchoServer service_;
  std::unique_ptr<grpc::Server> server_;
  std::thread server_thread_;
};

static void BM_E2eLatencyCensusDisabled(benchmark::State& state) {
  grpc_core::CoreConfiguration::Reset();
  grpc::testing::TestGrpcScope grpc_scope;
  EchoServerThread server;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(grpc::CreateChannel(
          server.address(), grpc::InsecureChannelCredentials()));

  grpc::testing::EchoResponse response;
  for (auto _ : state) {
    grpc::testing::EchoRequest request;
    grpc::ClientContext context;
    grpc::Status status = stub->Echo(&context, request, &response);
  }
}
BENCHMARK(BM_E2eLatencyCensusDisabled);

static void BM_E2eLatencyCensusEnabled(benchmark::State& state) {
  grpc_core::CoreConfiguration::Reset();
  // Now start the test by registering the plugin (once in the execution)
  RegisterOnce();
  // This we can safely repeat, and doing so clears accumulated data to avoid
  // initialization costs varying between runs.
  grpc::RegisterOpenCensusViewsForExport();

  grpc::testing::TestGrpcScope grpc_scope;
  EchoServerThread server;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(grpc::CreateChannel(
          server.address(), grpc::InsecureChannelCredentials()));

  grpc::testing::EchoResponse response;
  for (auto _ : state) {
    grpc::testing::EchoRequest request;
    grpc::ClientContext context;
    grpc::Status status = stub->Echo(&context, request, &response);
  }
}
BENCHMARK(BM_E2eLatencyCensusEnabled);

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::benchmark::Initialize(&argc, argv);
  if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
  ::benchmark::RunSpecifiedBenchmarks();
}
