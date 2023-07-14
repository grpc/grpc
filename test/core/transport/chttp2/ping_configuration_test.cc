// Copyright 2023 gRPC authors.
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

#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/ping_abuse_policy.h"
#include "src/core/ext/transport/chttp2/transport/ping_rate_policy.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/util/mock_endpoint.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

class ConfigurationTest : public ::testing::Test {
 protected:
  ConfigurationTest() {
    mock_endpoint_ = grpc_mock_endpoint_create(DiscardWrite);
    args_ = args_.SetObject(ResourceQuota::Default());
    args_ = args_.SetObject(
        grpc_event_engine::experimental::GetDefaultEventEngine());
  }

  grpc_endpoint* mock_endpoint_ = nullptr;
  ChannelArgs args_;

 private:
  static void DiscardWrite(grpc_slice /*slice*/) {}
};

TEST_F(ConfigurationTest, ClientKeepaliveDefaults) {
  ExecCtx exec_ctx;
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(
      grpc_create_chttp2_transport(args_, mock_endpoint_, /*is_client=*/true));
  EXPECT_EQ(t->keepalive_time, Duration::Infinity());
  EXPECT_EQ(t->keepalive_timeout, Duration::Seconds(20));
  EXPECT_EQ(t->keepalive_permit_without_calls, false);
  EXPECT_EQ(t->ping_rate_policy.TestOnlyMaxPingsWithoutData(), 2);
  grpc_transport_destroy(&t->base);
}

TEST_F(ConfigurationTest, ClientKeepaliveExplicitArgs) {
  ExecCtx exec_ctx;
  args_ = args_.Set(GRPC_ARG_KEEPALIVE_TIME_MS, 20000);
  args_ = args_.Set(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 10000);
  args_ = args_.Set(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, true);
  args_ = args_.Set(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 3);
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(
      grpc_create_chttp2_transport(args_, mock_endpoint_, /*is_client=*/true));
  EXPECT_EQ(t->keepalive_time, Duration::Seconds(20));
  EXPECT_EQ(t->keepalive_timeout, Duration::Seconds(10));
  EXPECT_EQ(t->keepalive_permit_without_calls, true);
  EXPECT_EQ(t->ping_rate_policy.TestOnlyMaxPingsWithoutData(), 3);
  grpc_transport_destroy(&t->base);
}

TEST_F(ConfigurationTest, ServerKeepaliveDefaults) {
  ExecCtx exec_ctx;
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(
      grpc_create_chttp2_transport(args_, mock_endpoint_, /*is_client=*/false));
  EXPECT_EQ(t->keepalive_time, Duration::Hours(2));
  EXPECT_EQ(t->keepalive_timeout, Duration::Seconds(20));
  EXPECT_EQ(t->keepalive_permit_without_calls, false);
  EXPECT_EQ(t->ping_rate_policy.TestOnlyMaxPingsWithoutData(), 2);
  EXPECT_EQ(t->ping_abuse_policy.TestOnlyMinPingIntervalWithoutData(),
            Duration::Minutes(5));
  EXPECT_EQ(t->ping_abuse_policy.TestOnlyMaxPingStrikes(), 2);
  grpc_transport_destroy(&t->base);
}

TEST_F(ConfigurationTest, ServerKeepaliveExplicitArgs) {
  ExecCtx exec_ctx;
  args_ = args_.Set(GRPC_ARG_KEEPALIVE_TIME_MS, 20000);
  args_ = args_.Set(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 10000);
  args_ = args_.Set(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, true);
  args_ = args_.Set(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 3);
  args_ =
      args_.Set(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 20000);
  args_ = args_.Set(GRPC_ARG_HTTP2_MAX_PING_STRIKES, 0);
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(
      grpc_create_chttp2_transport(args_, mock_endpoint_, /*is_client=*/false));
  EXPECT_EQ(t->keepalive_time, Duration::Seconds(20));
  EXPECT_EQ(t->keepalive_timeout, Duration::Seconds(10));
  EXPECT_EQ(t->keepalive_permit_without_calls, true);
  EXPECT_EQ(t->ping_rate_policy.TestOnlyMaxPingsWithoutData(), 3);
  EXPECT_EQ(t->ping_abuse_policy.TestOnlyMinPingIntervalWithoutData(),
            Duration::Seconds(20));
  EXPECT_EQ(t->ping_abuse_policy.TestOnlyMaxPingStrikes(), 0);
  grpc_transport_destroy(&t->base);
}

// This test modifies the defaults of the client side settings, so it would
// affect any test that is run after this.
// TODO(yashykt): If adding more client side tests after this, add a reset to
// defaults function.
TEST_F(ConfigurationTest, ModifyClientDefaults) {
  ExecCtx exec_ctx;
  // Note that we are creating a new args object to override the defaults.
  ChannelArgs args = args_.Set(GRPC_ARG_KEEPALIVE_TIME_MS, 20000);
  args = args.Set(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 10000);
  args = args.Set(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, true);
  args = args.Set(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 3);
  grpc_chttp2_config_default_keepalive_args(args, /*is_client=*/true);
  // Note that we are using the original args_ object for creating the transport
  // which does not override the defaults.
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(
      grpc_create_chttp2_transport(args_, mock_endpoint_, /*is_client=*/true));
  EXPECT_EQ(t->keepalive_time, Duration::Seconds(20));
  EXPECT_EQ(t->keepalive_timeout, Duration::Seconds(10));
  EXPECT_EQ(t->keepalive_permit_without_calls, true);
  EXPECT_EQ(t->ping_rate_policy.TestOnlyMaxPingsWithoutData(), 3);
  grpc_transport_destroy(&t->base);
}

// This test modifies the defaults of the client side settings, so it would
// affect any test that is run after this.
// TODO(yashykt): If adding more server side tests after this, add a reset to
// defaults function.
TEST_F(ConfigurationTest, ModifyServerDefaults) {
  ExecCtx exec_ctx;
  // Note that we are creating a new args object to override the defaults.
  ChannelArgs args = args_.Set(GRPC_ARG_KEEPALIVE_TIME_MS, 20000);
  args = args.Set(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 10000);
  args = args.Set(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, true);
  args = args.Set(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 3);
  args = args.Set(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 20000);
  args = args.Set(GRPC_ARG_HTTP2_MAX_PING_STRIKES, 0);
  grpc_chttp2_config_default_keepalive_args(args, /*is_client=*/false);
  // Note that we are using the original args_ object for creating the transport
  // which does not override the defaults.
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(
      grpc_create_chttp2_transport(args_, mock_endpoint_, /*is_client=*/false));
  EXPECT_EQ(t->keepalive_time, Duration::Seconds(20));
  EXPECT_EQ(t->keepalive_timeout, Duration::Seconds(10));
  EXPECT_EQ(t->keepalive_permit_without_calls, true);
  EXPECT_EQ(t->ping_rate_policy.TestOnlyMaxPingsWithoutData(), 3);
  EXPECT_EQ(t->ping_abuse_policy.TestOnlyMinPingIntervalWithoutData(),
            Duration::Seconds(20));
  EXPECT_EQ(t->ping_abuse_policy.TestOnlyMaxPingStrikes(), 0);
  grpc_transport_destroy(&t->base);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_core::ForceEnableExperiment("keepalive_fix", true);
  grpc_init();
  auto ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
