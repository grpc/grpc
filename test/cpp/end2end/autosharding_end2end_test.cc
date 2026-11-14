//
// Copyright 2026 gRPC authors.
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

// End-to-end tests for the autosharding_experimental LB policy (gRFC A119)
// with a fake AutoshardingService and a channel factory from channel args.

#include <grpc/channel_factory.h>
#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/fake/fake_resolver.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/uri.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/autosharding_test_server.h"
#include "test/cpp/end2end/counted_service.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace grpc {
namespace testing {
namespace {

constexpr absl::string_view kChannelFactoryKey = "autosharding_server";
constexpr absl::string_view kAutoshardingTarget = "my_autosharding_target";
constexpr absl::string_view kKeyHeaderName = "x-slice-key";

// A backend that counts the Echo RPCs that it receives.
class BackendServiceImpl : public CountedService<TestServiceImpl> {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    IncreaseRequestCount();
    return TestServiceImpl::Echo(context, request, response);
  }

  void Start() {}
  void Shutdown() {}
};

// Returns a channel to the autosharding server for kChannelFactoryKey, and a
// lame channel for any other key.
class TestChannelFactory final
    : public grpc_core::experimental::ChannelFactory {
 public:
  explicit TestChannelFactory(std::string target)
      : target_(std::move(target)),
        creds_(grpc_insecure_credentials_create()) {}

  ~TestChannelFactory() override { grpc_channel_credentials_release(creds_); }

  std::unique_ptr<ChannelHandle> CreateChannel(absl::string_view key) override {
    if (key != kChannelFactoryKey) {
      return CreateLameChannel(key, absl::UnavailableError(absl::StrCat(
                                        "unknown channel factory key: ", key)));
    }
    return CreateCoreChannel(target_, creds_);
  }

 private:
  const std::string target_;
  grpc_channel_credentials* const creds_;
};

class AutoshardingEnd2endTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_init();
    autosharding_server_ =
        std::make_unique<ServerThread<AutoshardingServiceImpl>>("autosharding");
    autosharding_server_->Start();
    response_generator_ =
        grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
    channel_factory_ = std::make_shared<TestChannelFactory>(
        grpc_core::LocalIpAndPort(autosharding_server_->port()));
    ChannelArguments args;
    args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                    response_generator_.get());
    args.SetPointerWithVtable(
        GRPC_ARG_CHANNEL_FACTORY, &channel_factory_,
        grpc_core::experimental::ChannelFactory::ChannelArgVtable());
    channel_ = CreateCustomChannel("fake:///autosharding.test",
                                   InsecureChannelCredentials(), args);
    stub_ = EchoTestService::NewStub(channel_);
  }

  void TearDown() override {
    stub_.reset();
    channel_.reset();
    for (auto& backend : backends_) backend->Shutdown();
    autosharding_server_->Shutdown();
    channel_factory_.reset();
    grpc_shutdown();
  }

  void StartBackends(size_t num_backends) {
    for (size_t i = 0; i < num_backends; ++i) {
      backends_.push_back(
          std::make_unique<ServerThread<BackendServiceImpl>>("backend"));
      backends_.back()->Start();
    }
  }

  // The name that the autosharding server uses for backend i.  The resolver
  // sets it as the hostname of the endpoint (gRFC A81).
  static std::string BackendName(size_t i) {
    return absl::StrCat("backend", i);
  }

  static std::string ServiceConfig(
      bool enable_fallback, absl::string_view initial_assignment_timeout) {
    return absl::StrFormat(
        "{\"loadBalancingConfig\":[{\"autosharding_experimental\":{"
        "\"channelFactoryKey\":\"%s\",\"autoshardingTarget\":\"%s\","
        "\"keyHeaderName\":\"%s\",\"enableFallback\":%s,"
        "\"initialAssignmentTimeout\":\"%s\"}}]}",
        kChannelFactoryKey, kAutoshardingTarget, kKeyHeaderName,
        enable_fallback ? "true" : "false", initial_assignment_timeout);
  }

  // Sends a resolver result with all backends and service_config.
  void SetNextResolution(absl::string_view service_config) {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    grpc_core::EndpointAddressesList endpoints;
    for (size_t i = 0; i < backends_.size(); ++i) {
      absl::StatusOr<grpc_core::URI> uri =
          grpc_core::URI::Parse(grpc_core::LocalIpUri(backends_[i]->port()));
      GRPC_CHECK_OK(uri);
      grpc_resolved_address address;
      GRPC_CHECK(grpc_parse_uri(*uri, &address));
      endpoints.emplace_back(
          address,
          grpc_core::ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, BackendName(i)));
    }
    result.addresses = std::move(endpoints);
    result.service_config =
        grpc_core::ServiceConfigImpl::Create(result.args, service_config);
    ASSERT_TRUE(result.service_config.ok()) << result.service_config.status();
    response_generator_->SetResponseSynchronously(std::move(result));
  }

  Status SendRpc(absl::string_view key) {
    ClientContext context;
    context.AddMetadata(std::string(kKeyHeaderName), std::string(key));
    context.set_deadline(grpc_timeout_seconds_to_deadline(5));
    EchoRequest request;
    request.set_message("hello");
    EchoResponse response;
    return stub_->Echo(&context, request, &response);
  }

  // Waits until the channel reports state, or until the deadline.  Returns
  // true if the channel reported state.
  bool WaitForChannelState(grpc_connectivity_state state,
                           gpr_timespec deadline) {
    while (true) {
      grpc_connectivity_state current = channel_->GetState(false);
      if (current == state) return true;
      if (!channel_->WaitForStateChange(current, deadline)) return false;
    }
  }

  std::unique_ptr<ServerThread<AutoshardingServiceImpl>> autosharding_server_;
  std::vector<std::unique_ptr<ServerThread<BackendServiceImpl>>> backends_;
  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator_;
  std::shared_ptr<grpc_core::experimental::ChannelFactory> channel_factory_;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<EchoTestService::Stub> stub_;
};

TEST_F(AutoshardingEnd2endTest, KeyRoutesToAssignedBackend) {
  StartBackends(2);
  // Keys before "m" go to backend 0, and the rest go to backend 1.
  autosharding_server_->service().SetAssignment(
      {{"m", {0}}, {"", {1}}}, {BackendName(0), BackendName(1)},
      /*generation=*/1);
  SetNextResolution(ServiceConfig(/*enable_fallback=*/false, "10s"));
  EXPECT_EQ(SendRpc("a").error_code(), StatusCode::OK);
  EXPECT_EQ(backends_[0]->service().request_count(), 1u);
  EXPECT_EQ(backends_[1]->service().request_count(), 0u);
  EXPECT_EQ(SendRpc("z").error_code(), StatusCode::OK);
  EXPECT_EQ(backends_[0]->service().request_count(), 1u);
  EXPECT_EQ(backends_[1]->service().request_count(), 1u);
  EXPECT_THAT(autosharding_server_->service().targets(),
              ::testing::ElementsAre(kAutoshardingTarget));
}

TEST_F(AutoshardingEnd2endTest, FallbackWhenAutoshardingServerIsDown) {
  StartBackends(2);
  autosharding_server_->Shutdown();
  // The stream to the autosharding server uses wait_for_ready, so it waits
  // for the server instead of failing.  When the 1s initial assignment timer
  // fires, the client reports an error, and picks use all backends.
  SetNextResolution(ServiceConfig(/*enable_fallback=*/true, "1s"));
  EXPECT_EQ(SendRpc("a").error_code(), StatusCode::OK);
  EXPECT_EQ(backends_[0]->service().request_count() +
                backends_[1]->service().request_count(),
            1u);
}

TEST_F(AutoshardingEnd2endTest, NewAssignmentMovesTraffic) {
  StartBackends(2);
  autosharding_server_->service().SetAssignment({{"", {0}}}, {BackendName(0)},
                                                /*generation=*/1);
  SetNextResolution(ServiceConfig(/*enable_fallback=*/false, "10s"));
  EXPECT_EQ(SendRpc("a").error_code(), StatusCode::OK);
  EXPECT_EQ(backends_[0]->service().request_count(), 1u);
  // The new assignment moves all keys to backend 1.
  autosharding_server_->service().SetAssignment({{"", {0}}}, {BackendName(1)},
                                                /*generation=*/2);
  const gpr_timespec deadline = grpc_timeout_seconds_to_deadline(10);
  while (backends_[1]->service().request_count() == 0) {
    ASSERT_LT(gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline), 0)
        << "traffic did not move to backend 1";
    EXPECT_EQ(SendRpc("a").error_code(), StatusCode::OK);
  }
  backends_[0]->service().ResetCounters();
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(SendRpc("a").error_code(), StatusCode::OK);
  }
  EXPECT_EQ(backends_[0]->service().request_count(), 0u);
}

TEST_F(AutoshardingEnd2endTest, GetStateTryToConnectConnectsWhenIdle) {
  StartBackends(1);
  autosharding_server_->service().SetAssignment({{"", {0}}}, {BackendName(0)},
                                                /*generation=*/1);
  SetNextResolution(ServiceConfig(/*enable_fallback=*/false, "10s"));
  EXPECT_EQ(SendRpc("a").error_code(), StatusCode::OK);
  EXPECT_EQ(channel_->GetState(false), GRPC_CHANNEL_READY);
  // When the backend goes away, its connection goes IDLE, and so does the
  // channel.
  backends_[0]->Shutdown();
  ASSERT_TRUE(WaitForChannelState(GRPC_CHANNEL_IDLE,
                                  grpc_timeout_seconds_to_deadline(10)));
  // GetState(true) asks the LB policy to exit IDLE, which connects to the
  // backend without an RPC.
  backends_[0]->Start();
  EXPECT_EQ(channel_->GetState(/*try_to_connect=*/true), GRPC_CHANNEL_IDLE);
  EXPECT_TRUE(WaitForChannelState(GRPC_CHANNEL_READY,
                                  grpc_timeout_seconds_to_deadline(10)));
  EXPECT_EQ(backends_[0]->service().request_count(), 1u);
}

TEST_F(AutoshardingEnd2endTest, ReportsConnectingBeforeFirstAssignment) {
  StartBackends(1);
  SetNextResolution(ServiceConfig(/*enable_fallback=*/false, "10s"));
  EXPECT_EQ(channel_->GetState(/*try_to_connect=*/true), GRPC_CHANNEL_IDLE);
  ASSERT_TRUE(WaitForChannelState(GRPC_CHANNEL_CONNECTING,
                                  grpc_timeout_seconds_to_deadline(10)));
  // The autosharding server has not sent an assignment, so the channel stays
  // in CONNECTING.
  EXPECT_FALSE(channel_->WaitForStateChange(
      GRPC_CHANNEL_CONNECTING, grpc_timeout_milliseconds_to_deadline(500)));
  // After the assignment, the policy connects without an RPC.
  autosharding_server_->service().SetAssignment({{"", {0}}}, {BackendName(0)},
                                                /*generation=*/1);
  EXPECT_TRUE(WaitForChannelState(GRPC_CHANNEL_READY,
                                  grpc_timeout_seconds_to_deadline(10)));
  EXPECT_EQ(backends_[0]->service().request_count(), 0u);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
