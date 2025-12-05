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

#include <grpc/support/port_platform.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server_builder.h>

#include <string>

#include "src/core/telemetry/metrics.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/end2end/connection_attempt_injector.h"
#include "gtest/gtest.h"

namespace grpc {
namespace testing {
namespace {

bool WaitForChannelState(
    Channel* channel,
    absl::AnyInvocable<bool(grpc_connectivity_state)> predicate,
    bool try_to_connect = false, int timeout_seconds = 3) {
  const gpr_timespec deadline =
      grpc_timeout_seconds_to_deadline(timeout_seconds);
  while (true) {
    grpc_connectivity_state state = channel->GetState(try_to_connect);
    if (predicate(state)) break;
    if (!channel->WaitForStateChange(state, deadline)) return false;
  }
  return true;
}

std::shared_ptr<Channel> CreateChannelWithBackoff(const std::string& target,
                                                  int backoff_ms) {
  ChannelArguments args;
  args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, backoff_ms);
  args.SetInt(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, backoff_ms);
  args.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, backoff_ms);
  return grpc::CreateCustomChannel("ipv4:" + target,
                                   grpc::InsecureChannelCredentials(), args);
}

class MinimalEchoService : public grpc::testing::EchoTestService::Service {
 public:
  grpc::Status Echo(grpc::ServerContext* context,
                    const grpc::testing::EchoRequest* request,
                    grpc::testing::EchoResponse* response) override {
    response->set_message(request->message());
    return grpc::Status::OK;
  }
};

class SubchannelMetricsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    stats_plugin_ = grpc_core::FakeStatsPluginBuilder()
                        .UseDisabledByDefaultMetrics(true)
                        .BuildAndRegister();
    kConnectionAttemptsSucceeded_ =
        grpc_core::GlobalInstrumentsRegistryTestPeer::
            FindUInt64CounterHandleByName(
                "grpc.subchannel.connection_attempts_succeeded")
                .value();
    kOpenConnections_ = grpc_core::GlobalInstrumentsRegistryTestPeer::
                            FindInt64UpDownCounterHandleByName(
                                "grpc.subchannel.open_connections")
                                .value();
    kDisconnections_ =
        grpc_core::GlobalInstrumentsRegistryTestPeer::
            FindUInt64CounterHandleByName("grpc.subchannel.disconnections")
                .value();
    kConnectionAttemptsFailed_ =
        grpc_core::GlobalInstrumentsRegistryTestPeer::
            FindUInt64CounterHandleByName(
                "grpc.subchannel.connection_attempts_failed")
                .value();
  }
  void TearDown() override {
    grpc_core::GlobalStatsPluginRegistryTestPeer::
        ResetGlobalStatsPluginRegistry();
  }
  std::shared_ptr<grpc_core::FakeStatsPlugin> stats_plugin_;
  grpc_core::GlobalInstrumentsRegistry::GlobalInstrumentHandle
      kConnectionAttemptsSucceeded_;
  grpc_core::GlobalInstrumentsRegistry::GlobalInstrumentHandle
      kDisconnections_;
  grpc_core::GlobalInstrumentsRegistry::GlobalInstrumentHandle
      kConnectionAttemptsFailed_;
  grpc_core::GlobalInstrumentsRegistry::GlobalInstrumentHandle
      kOpenConnections_;
};

TEST_F(SubchannelMetricsTest, SubchannelMetricsBasic) {
  ConnectionAttemptInjector injector;
  const int port = grpc_pick_unused_port_or_die();
  std::string target = "127.0.0.1:" + std::to_string(port);
  ServerBuilder builder;
  builder.AddListeningPort("127.0.0.1:" + std::to_string(port),
                           grpc::InsecureServerCredentials());
  auto service = std::make_unique<MinimalEchoService>();
  builder.RegisterService(service.get());
  auto server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto channel =
      grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  auto hold = injector.AddHold(port);
  channel->GetState(true);
  hold->Wait();
  hold->Resume();
  EXPECT_TRUE(
      WaitForChannelState(channel.get(), [](grpc_connectivity_state state) {
        return state == GRPC_CHANNEL_READY;
      }));
  grpc::testing::EchoRequest request;
  request.set_message("test");
  grpc::testing::EchoResponse response;
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::seconds(1));
  grpc::Status status = stub->Echo(&context, request, &response);
  ASSERT_TRUE(status.ok()) << "RPC failed: " << status.error_message();
  EXPECT_THAT(stats_plugin_->GetUInt64CounterValue(
                  kConnectionAttemptsSucceeded_, {target}, {"", ""}),
              ::testing::Optional(1));
  EXPECT_THAT(stats_plugin_->GetInt64UpDownCounterValue(
                  kOpenConnections_, {target}, {"unknown", "", ""}),
              ::testing::Optional(1));
  server->Shutdown();
  EXPECT_THAT(stats_plugin_->GetUInt64CounterValue(kDisconnections_, {target},
                                                   {"", "", "unknown"}),
              ::testing::Optional(1));
  EXPECT_THAT(stats_plugin_->GetInt64UpDownCounterValue(
                  kOpenConnections_, {target}, {"unknown", "", ""}),
              ::testing::Optional(0));
}

TEST_F(SubchannelMetricsTest, ConnectionAttemptsFailed) {
  ConnectionAttemptInjector injector;
  const int port = grpc_pick_unused_port_or_die();
  std::string target = "127.0.0.1:" + std::to_string(port);
  auto channel = CreateChannelWithBackoff(target, 1000);
  auto hold = injector.AddHold(port);
  EXPECT_EQ(channel->GetState(true), GRPC_CHANNEL_IDLE);
  hold->Wait();
  hold->Fail(absl::UnavailableError("injected failure"));
  EXPECT_TRUE(
      WaitForChannelState(channel.get(), [](grpc_connectivity_state state) {
        return state == GRPC_CHANNEL_TRANSIENT_FAILURE;
      }));
  EXPECT_THAT(stats_plugin_->GetUInt64CounterValue(kConnectionAttemptsFailed_,
                                                   {target}, {"", ""}),
              ::testing::Optional(1));
}

TEST_F(SubchannelMetricsTest, MultipleConnectionAttemptsFailed) {
  ConnectionAttemptInjector injector;
  const int port = grpc_pick_unused_port_or_die();
  std::string target = "127.0.0.1:" + std::to_string(port);
  auto channel = CreateChannelWithBackoff(target, 1000);
  std::vector<std::unique_ptr<ConnectionAttemptInjector::Hold>> holds;
  constexpr int kConnecionAttempts = 3;
  for (int i = 0; i < kConnecionAttempts; ++i) {
    holds.push_back(injector.AddHold(port));
  }
  channel->GetState(true);
  for (auto& hold : holds) {
    hold->Wait();
    hold->Fail(absl::UnavailableError("test failure"));
  }
  absl::SleepFor(absl::Milliseconds(50));
  EXPECT_THAT(stats_plugin_->GetUInt64CounterValue(kConnectionAttemptsFailed_,
                                                   {target}, {"", ""}),
              ::testing::Optional(kConnecionAttempts));
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  grpc::testing::ConnectionAttemptInjector::Init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
