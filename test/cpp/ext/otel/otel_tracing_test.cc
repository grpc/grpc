//
//
// Copyright 2025 gRPC authors.
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

#include <grpcpp/ext/otel_plugin.h>
#include <grpcpp/grpcpp.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/exporters/memory/in_memory_span_exporter_factory.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "src/core/config/core_configuration.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {
namespace {

class OTelTracingTest : public ::testing::Test {
 public:
  void SetUp() override {
    grpc_core::CoreConfiguration::Reset();
    grpc_init();
    data_ =
        std::make_shared<opentelemetry::exporter::memory::InMemorySpanData>(10);
    // Register OTel plugin for tracing with an in memory exporter
    auto tracer_provider =
        std::make_shared<opentelemetry::sdk::trace::TracerProvider>(
            opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(
                opentelemetry::exporter::memory::InMemorySpanExporterFactory::
                    Create(data_)));
    ASSERT_TRUE(OpenTelemetryPluginBuilder()
                    .SetTracerProvider(std::move(tracer_provider))
                    .SetTextMapPropagator(
                        grpc::experimental::MakeGrpcTraceBinTextMapPropagator())
                    .BuildAndRegisterGlobal()
                    .ok());
    grpc::ServerBuilder builder;
    int port;
    // Use IPv4 here because it's less flaky than IPv6 ("[::]:0") on Travis.
    builder.AddListeningPort("0.0.0.0:0", grpc::InsecureServerCredentials(),
                             &port);
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    std::string server_address = absl::StrCat("localhost:", port);
    auto channel =
        grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
    stub_ = EchoTestService::NewStub(channel);
  }

  void TearDown() override {
    server_->Shutdown();
    grpc_shutdown_blocking();
    grpc_core::ServerCallTracerFactory::TestOnlyReset();
    grpc_core::GlobalStatsPluginRegistryTestPeer::
        ResetGlobalStatsPluginRegistry();
  }

  void SendRPC() {
    EchoRequest request;
    request.set_message("foo");
    EchoResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->Echo(&context, request, &response);
  }

 private:
  std::shared_ptr<opentelemetry::exporter::memory::InMemorySpanData> data_;
  CallbackTestServiceImpl service_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<EchoTestService::Stub> stub_;
};

TEST_F(OTelTracingTest, Basic) { SendRPC(); }

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}