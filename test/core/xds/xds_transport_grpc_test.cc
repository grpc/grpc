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

#include "src/core/xds/grpc/xds_transport_grpc.h"

#include "src/core/config/core_configuration.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/synchronization/notification.h"

namespace grpc_core {
namespace testing {
namespace {

class GrpcXdsTransportTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto json = JsonParse("{\"channel_creds\": [{\"type\": \"insecure\"}]}");
    ASSERT_TRUE(json.ok()) << json.status().ToString();
    ValidationErrors errors;
    channel_creds_config_ =
        ParseXdsBootstrapChannelCreds(*json, JsonArgs(), &errors);
    ASSERT_TRUE(errors.ok());
    ASSERT_NE(channel_creds_config_, nullptr);
    factory_ = MakeRefCounted<GrpcXdsTransportFactory>(ChannelArgs(), nullptr);
  }
  RefCountedPtr<const ChannelCredsConfig> channel_creds_config_;
  RefCountedPtr<GrpcXdsTransportFactory> factory_;
};

TEST_F(GrpcXdsTransportTest, IdenticalTargetsShareTransport) {
  ExecCtx exec_ctx;
  auto target = std::make_shared<GrpcXdsServerTarget>(
      "localhost:1234", channel_creds_config_,
      std::vector<RefCountedPtr<const CallCredsConfig>>{},
      std::vector<std::pair<std::string, std::string>>{{"key1", "val1"}},
      Duration::Seconds(10));
  absl::Status status1;
  auto transport1 = factory_->GetTransport(*target, &status1);
  ASSERT_TRUE(status1.ok()) << status1.ToString();
  absl::Status status2;
  auto transport2 = factory_->GetTransport(*target, &status2);
  ASSERT_TRUE(status2.ok()) << status2.ToString();
  EXPECT_EQ(transport1, transport2);
}

TEST_F(GrpcXdsTransportTest, DifferingMetadataSharesChannel) {
  ExecCtx exec_ctx;
  auto target1 = std::make_shared<GrpcXdsServerTarget>(
      "localhost:1234", channel_creds_config_,
      std::vector<RefCountedPtr<const CallCredsConfig>>{},
      std::vector<std::pair<std::string, std::string>>{{"key1", "val1"}},
      Duration::Seconds(10));
  auto target2 = std::make_shared<GrpcXdsServerTarget>(
      "localhost:1234", channel_creds_config_,
      std::vector<RefCountedPtr<const CallCredsConfig>>{},
      std::vector<std::pair<std::string, std::string>>{{"key2", "val2"}},
      Duration::Seconds(10));
  EXPECT_NE(target1->Key(), target2->Key());
  absl::Status status1;
  auto transport1 = factory_->GetTransport(*target1, &status1);
  ASSERT_TRUE(status1.ok()) << status1.ToString();
  absl::Status status2;
  auto transport2 = factory_->GetTransport(*target2, &status2);
  ASSERT_TRUE(status2.ok()) << status2.ToString();
  EXPECT_NE(transport1, transport2);
  auto* grpc_transport1 =
      DownCast<GrpcXdsTransportFactory::GrpcXdsTransport*>(transport1.get());
  auto* grpc_transport2 =
      DownCast<GrpcXdsTransportFactory::GrpcXdsTransport*>(transport2.get());
  EXPECT_EQ(grpc_transport1->channel(), grpc_transport2->channel());
}

TEST_F(GrpcXdsTransportTest, DifferingTimeoutSharesChannel) {
  ExecCtx exec_ctx;
  auto target1 = std::make_shared<GrpcXdsServerTarget>(
      "localhost:1234", channel_creds_config_,
      std::vector<RefCountedPtr<const CallCredsConfig>>{},
      std::vector<std::pair<std::string, std::string>>{{"key1", "val1"}},
      Duration::Seconds(10));
  auto target2 = std::make_shared<GrpcXdsServerTarget>(
      "localhost:1234", channel_creds_config_,
      std::vector<RefCountedPtr<const CallCredsConfig>>{},
      std::vector<std::pair<std::string, std::string>>{{"key1", "val1"}},
      Duration::Seconds(20));
  EXPECT_NE(target1->Key(), target2->Key());
  absl::Status status1;
  auto transport1 = factory_->GetTransport(*target1, &status1);
  ASSERT_TRUE(status1.ok()) << status1.ToString();
  absl::Status status2;
  auto transport2 = factory_->GetTransport(*target2, &status2);
  ASSERT_TRUE(status2.ok()) << status2.ToString();
  EXPECT_NE(transport1, transport2);
  auto* grpc_transport1 =
      DownCast<GrpcXdsTransportFactory::GrpcXdsTransport*>(transport1.get());
  auto* grpc_transport2 =
      DownCast<GrpcXdsTransportFactory::GrpcXdsTransport*>(transport2.get());
  EXPECT_EQ(grpc_transport1->channel(), grpc_transport2->channel());
}

TEST_F(GrpcXdsTransportTest, DifferingServerUriDoesNotShareChannel) {
  ExecCtx exec_ctx;
  auto target1 = std::make_shared<GrpcXdsServerTarget>(
      "localhost:1234", channel_creds_config_,
      std::vector<RefCountedPtr<const CallCredsConfig>>{},
      std::vector<std::pair<std::string, std::string>>{{"key1", "val1"}},
      Duration::Seconds(10));
  auto target2 = std::make_shared<GrpcXdsServerTarget>(
      "localhost:5678", channel_creds_config_,
      std::vector<RefCountedPtr<const CallCredsConfig>>{},
      std::vector<std::pair<std::string, std::string>>{{"key1", "val1"}},
      Duration::Seconds(10));
  EXPECT_NE(target1->Key(), target2->Key());
  absl::Status status1;
  auto transport1 = factory_->GetTransport(*target1, &status1);
  ASSERT_TRUE(status1.ok()) << status1.ToString();
  absl::Status status2;
  auto transport2 = factory_->GetTransport(*target2, &status2);
  ASSERT_TRUE(status2.ok()) << status2.ToString();
  EXPECT_NE(transport1, transport2);
  auto* grpc_transport1 =
      DownCast<GrpcXdsTransportFactory::GrpcXdsTransport*>(transport1.get());
  auto* grpc_transport2 =
      DownCast<GrpcXdsTransportFactory::GrpcXdsTransport*>(transport2.get());
  EXPECT_NE(grpc_transport1->channel(), grpc_transport2->channel());
}

TEST_F(GrpcXdsTransportTest, ChannelGarbageCollectedWhenNoTransportsRemain) {
  ExecCtx exec_ctx;
  auto target = std::make_shared<GrpcXdsServerTarget>(
      "localhost:1234", channel_creds_config_,
      std::vector<RefCountedPtr<const CallCredsConfig>>{},
      std::vector<std::pair<std::string, std::string>>{{"key1", "val1"}},
      Duration::Seconds(10));
  absl::Status status1;
  auto transport1 = factory_->GetTransport(*target, &status1);
  ASSERT_TRUE(status1.ok()) << status1.ToString();
  auto* grpc_transport1 =
      DownCast<GrpcXdsTransportFactory::GrpcXdsTransport*>(transport1.get());
  Channel* channel1 = grpc_transport1->channel();
  transport1.reset();
  absl::Notification notification;
  grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
      [&notification]() { notification.Notify(); });
  notification.WaitForNotification();
  absl::Status status2;
  auto transport2 = factory_->GetTransport(*target, &status2);
  ASSERT_TRUE(status2.ok()) << status2.ToString();
  auto* grpc_transport2 =
      DownCast<GrpcXdsTransportFactory::GrpcXdsTransport*>(transport2.get());
  Channel* channel2 = grpc_transport2->channel();
  EXPECT_NE(channel1, channel2);
}

TEST(GrpcXdsServerTargetTest, KeyAndEquals) {
  auto json = JsonParse("{\"channel_creds\": [{\"type\": \"insecure\"}]}");
  ASSERT_TRUE(json.ok()) << json.status().ToString();
  ValidationErrors errors;
  auto creds = ParseXdsBootstrapChannelCreds(*json, JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok());
  GrpcXdsServerTarget target("localhost:1234", creds, {}, {{"k1", "v1"}},
                             Duration::Seconds(10));
  EXPECT_EQ(target, GrpcXdsServerTarget("localhost:1234", creds, {},
                                        {{"k1", "v1"}}, Duration::Seconds(10)));
  EXPECT_EQ(target.Key(),
            GrpcXdsServerTarget("localhost:1234", creds, {}, {{"k1", "v1"}},
                                Duration::Seconds(10))
                .Key());
  EXPECT_NE(target, GrpcXdsServerTarget("localhost:5678", creds, {},
                                        {{"k1", "v1"}}, Duration::Seconds(10)));
  EXPECT_NE(target.Key(),
            GrpcXdsServerTarget("localhost:5678", creds, {}, {{"k1", "v1"}},
                                Duration::Seconds(10))
                .Key());
  EXPECT_NE(target, GrpcXdsServerTarget("localhost:1234", creds, {},
                                        {{"k2", "v2"}}, Duration::Seconds(10)));
  EXPECT_NE(target.Key(),
            GrpcXdsServerTarget("localhost:1234", creds, {}, {{"k2", "v2"}},
                                Duration::Seconds(10))
                .Key());
  EXPECT_NE(target, GrpcXdsServerTarget("localhost:1234", creds, {},
                                        {{"k1", "v1"}}, Duration::Seconds(20)));
  EXPECT_NE(target.Key(),
            GrpcXdsServerTarget("localhost:1234", creds, {}, {{"k1", "v1"}},
                                Duration::Seconds(20))
                .Key());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
