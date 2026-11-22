//
// Copyright 2017 gRPC authors.
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

#include "src/core/resolver/google_c2p/google_c2p_resolver.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>
#include <stddef.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "src/core/util/env.h"
#include "test/core/test_util/fake_udp_and_tcp_server.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

namespace {

void TryConnectAndDestroy(const char* fake_metadata_server_address) {
  grpc::ChannelArguments args;
  std::string target = "google-c2p:///servername_not_used";
  args.SetInt("grpc.testing.google_c2p_resolver_pretend_running_on_gcp", 1);
  args.SetString("grpc.testing.google_c2p_resolver_metadata_server_override",
                 fake_metadata_server_address);
  auto channel = grpc::CreateCustomChannel(
      target, grpc::InsecureChannelCredentials(), args);
  // Start connecting, and give some time for the google-c2p resolver to begin
  // resolution and start trying to contact the metadata server.
  channel->GetState(true /* try_to_connect */);
  ASSERT_FALSE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  channel.reset();
};

// Exercise the machinery involved with shutting down the C2P resolver while
// it's waiting for its initial metadata server queries to finish.
TEST(DestroyGoogleC2pChannelWithActiveConnectStressTest,
     LoopTryConnectAndDestroyWithHangingMetadataServer) {
  // Create a fake metadata server which hangs.
  grpc_core::testing::FakeUdpAndTcpServer fake_metadata_server(
      grpc_core::testing::FakeUdpAndTcpServer::AcceptMode::
          kWaitForClientToSendFirstBytes,
      grpc_core::testing::FakeUdpAndTcpServer::CloseSocketUponCloseFromPeer);
  std::vector<std::unique_ptr<std::thread>> threads;
  const int kNumThreads = 10;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        new std::thread(TryConnectAndDestroy, fake_metadata_server.address()));
  }
  for (size_t i = 0; i < threads.size(); i++) {
    threads[i]->join();
  }
}

// Exercise the machinery involved with shutting down the C2P resolver while
// it's waiting for its initial metadata server queries to finish.
TEST(DestroyGoogleC2pChannelWithActiveConnectStressTest,
     LoopTryConnectAndDestroyWithFastFailingMetadataServer) {
  // Create a fake metadata server address which rejects connections
  int port = grpc_pick_unused_port_or_die();
  std::string address = absl::StrFormat("[::1]:%d", port);
  std::vector<std::unique_ptr<std::thread>> threads;
  const int kNumThreads = 10;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        new std::thread(TryConnectAndDestroy, address.c_str()));
  }
  for (size_t i = 0; i < threads.size(); i++) {
    threads[i]->join();
  }
}

class GoogleC2PUniverseDomainTest : public ::testing::Test {
 protected:
  void SetUp() override {
    saved_td_uri_ = grpc_core::GetEnv(
        "GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI");
    grpc_core::ResetGoogleC2PResolverForTesting();
  }

  void TearDown() override {
    grpc_core::ResetGoogleC2PResolverForTesting();
    if (saved_td_uri_.has_value()) {
      grpc_core::SetEnv(
          "GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI",
          *saved_td_uri_);
    } else {
      grpc_core::UnsetEnv(
          "GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI");
    }
  }

  std::optional<std::string> saved_td_uri_;
};

TEST_F(GoogleC2PUniverseDomainTest, SetUniverseDomainBeforeChannelCreation) {
  EXPECT_EQ(grpc_core::GetGoogleC2PUniverseDomainForTesting(), std::nullopt);
  absl::Status empty_status = grpc::GoogleC2PSetUniverseDomain("");
  EXPECT_EQ(empty_status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_FALSE(empty_status.message().empty());
  EXPECT_EQ(grpc_core::GetGoogleC2PUniverseDomainForTesting(), std::nullopt);

  EXPECT_TRUE(grpc::GoogleC2PSetUniverseDomain("example.com").ok());
  EXPECT_EQ(grpc_core::GetGoogleC2PUniverseDomainForTesting(), "example.com");
  // Idempotent when called again with the same universe domain.
  EXPECT_TRUE(grpc::GoogleC2PSetUniverseDomain("example.com").ok());
  // Changing to a different universe domain fails.
  absl::Status conflict_status =
      grpc::GoogleC2PSetUniverseDomain("other.example.com");
  EXPECT_EQ(conflict_status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(conflict_status.message().find("example.com"), std::string::npos);
  EXPECT_EQ(grpc_core::GetGoogleC2PUniverseDomainForTesting(), "example.com");
}

TEST_F(GoogleC2PUniverseDomainTest, CApiValidationAndIdempotency) {
  EXPECT_EQ(grpc_google_c2p_set_universe_domain(nullptr, nullptr),
            GRPC_STATUS_INVALID_ARGUMENT);

  char* error_message = nullptr;
  EXPECT_EQ(grpc_google_c2p_set_universe_domain(nullptr, &error_message),
            GRPC_STATUS_INVALID_ARGUMENT);
  ASSERT_NE(error_message, nullptr);
  gpr_free(error_message);
  error_message = nullptr;

  EXPECT_EQ(grpc_google_c2p_set_universe_domain("", &error_message),
            GRPC_STATUS_INVALID_ARGUMENT);
  ASSERT_NE(error_message, nullptr);
  gpr_free(error_message);

  char sentinel = '\0';
  error_message = &sentinel;
  EXPECT_EQ(grpc_google_c2p_set_universe_domain("example.com", &error_message),
            GRPC_STATUS_OK);
  EXPECT_EQ(error_message, nullptr);

  EXPECT_EQ(grpc_google_c2p_set_universe_domain("example.com", nullptr),
            GRPC_STATUS_OK);

  EXPECT_EQ(
      grpc_google_c2p_set_universe_domain("other.example.com", &error_message),
      GRPC_STATUS_FAILED_PRECONDITION);
  ASSERT_NE(error_message, nullptr);
  gpr_free(error_message);
}

TEST_F(GoogleC2PUniverseDomainTest, DefaultsToGoogleapisComOnChannelCreation) {
  int port = grpc_pick_unused_port_or_die();
  std::string fake_mds = absl::StrFormat("[::1]:%d", port);
  grpc::ChannelArguments args;
  args.SetInt("grpc.testing.google_c2p_resolver_pretend_running_on_gcp", 1);
  args.SetString("grpc.testing.google_c2p_resolver_metadata_server_override",
                 fake_mds);
  auto channel = grpc::CreateCustomChannel(
      "google-c2p:///servername_not_used",
      grpc::experimental::LocalCredentials(LOCAL_TCP), args);
  // Trigger resolver instantiation and wait briefly for ExitIdle.
  channel->GetState(true /* try_to_connect */);
  ASSERT_FALSE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  // Creating a channel with the google-c2p resolver on GCP locks the default
  // universe domain to "googleapis.com" even when the test env var overrides
  // the TD URI.
  EXPECT_EQ(grpc_core::GetGoogleC2PUniverseDomainForTesting(),
            "googleapis.com");
  EXPECT_TRUE(grpc::GoogleC2PSetUniverseDomain("googleapis.com").ok());
  EXPECT_EQ(grpc::GoogleC2PSetUniverseDomain("example.com").code(),
            absl::StatusCode::kFailedPrecondition);
  channel.reset();
}

TEST_F(GoogleC2PUniverseDomainTest, BootstrapUsesConfiguredUniverseDomain) {
  grpc_core::UnsetEnv(
      "GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI");
  ASSERT_TRUE(grpc::GoogleC2PSetUniverseDomain("example.com").ok());
  grpc::ChannelArguments args;
  args.SetString("grpc.internal.xds_client_key_for_test",
                 "universe_domain_configured_test");
  auto channel = grpc::CreateCustomChannel(
      "google-c2p:///universe_domain_configured_test?force-xds",
      grpc::experimental::LocalCredentials(LOCAL_TCP), args);
  channel->GetState(true /* try_to_connect */);
  ASSERT_FALSE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  EXPECT_EQ(grpc_core::GetGoogleC2PBootstrapServerUriForTesting(),
            "directpath-pa.example.com");
  // Verify the constructed C2P bootstrap was passed to GrpcXdsClient.
  grpc_slice xds_configs = grpc_dump_xds_configs();
  std::string dumped(
      reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(xds_configs)),
      GRPC_SLICE_LENGTH(xds_configs));
  grpc_slice_unref(xds_configs);
  EXPECT_NE(dumped.find("universe_domain_configured_test"), std::string::npos);
  EXPECT_NE(dumped.find("C2P-"), std::string::npos);
  // Subsequent resolvers reuse the cached process-global bootstrap.
  grpc_core::SetEnv("GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI",
                    "unused-override.example.com:443");
  auto channel2 = grpc::CreateCustomChannel(
      "google-c2p:///universe_domain_configured_test_2?force-xds",
      grpc::experimental::LocalCredentials(LOCAL_TCP), args);
  channel2->GetState(true /* try_to_connect */);
  ASSERT_FALSE(
      channel2->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  EXPECT_EQ(grpc_core::GetGoogleC2PBootstrapServerUriForTesting(),
            "directpath-pa.example.com");
  channel2.reset();
  channel.reset();
}

TEST_F(GoogleC2PUniverseDomainTest, BootstrapUsesDefaultUniverseDomain) {
  grpc_core::UnsetEnv(
      "GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI");
  grpc::ChannelArguments args;
  args.SetString("grpc.internal.xds_client_key_for_test",
                 "universe_domain_default_test");
  auto channel = grpc::CreateCustomChannel(
      "google-c2p:///servername_not_used?force-xds",
      grpc::experimental::LocalCredentials(LOCAL_TCP), args);
  channel->GetState(true /* try_to_connect */);
  ASSERT_FALSE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  EXPECT_EQ(grpc_core::GetGoogleC2PUniverseDomainForTesting(),
            "googleapis.com");
  EXPECT_EQ(grpc_core::GetGoogleC2PBootstrapServerUriForTesting(),
            "directpath-pa.googleapis.com");
  channel.reset();
}

TEST_F(GoogleC2PUniverseDomainTest, EnvVarOverridesConfiguredUniverseDomain) {
  grpc_core::SetEnv("GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI",
                    "custom-td.example.com:443");
  grpc::ChannelArguments args;
  args.SetString("grpc.internal.xds_client_key_for_test",
                 "universe_domain_env_override_test");
  auto channel = grpc::CreateCustomChannel(
      "google-c2p:///servername_not_used?force-xds",
      grpc::experimental::LocalCredentials(LOCAL_TCP), args);
  channel->GetState(true /* try_to_connect */);
  ASSERT_FALSE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  EXPECT_EQ(grpc_core::GetGoogleC2PUniverseDomainForTesting(),
            "googleapis.com");
  EXPECT_EQ(grpc_core::GetGoogleC2PBootstrapServerUriForTesting(),
            "custom-td.example.com:443");
  channel.reset();
}

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int result;
  {
    grpc_core::testing::FakeUdpAndTcpServer fake_xds_server(
        grpc_core::testing::FakeUdpAndTcpServer::AcceptMode::
            kWaitForClientToSendFirstBytes,
        grpc_core::testing::FakeUdpAndTcpServer::
            CloseSocketUponReceivingBytesFromPeer);
    grpc_core::SetEnv("GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI",
                      fake_xds_server.address());
    result = RUN_ALL_TESTS();
  }
  grpc_shutdown();
  return result;
}
