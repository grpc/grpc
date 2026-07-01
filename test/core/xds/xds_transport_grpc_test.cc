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

#include <grpc/grpc.h>

#include <utility>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/certificate_provider_store.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

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
    auto store = MakeRefCounted<CertificateProviderStore>(
        CertificateProviderStore::PluginDefinitionMap{});
    factory_ = MakeRefCounted<GrpcXdsTransportFactory>(ChannelArgs(),
                                                       std::move(store));
  }
  RefCountedPtr<const ChannelCredsConfig> channel_creds_config_;
  RefCountedPtr<GrpcXdsTransportFactory> factory_;
};

TEST_F(GrpcXdsTransportTest, IdenticalTargetsShareTransport) {
  ExecCtx exec_ctx;
  GrpcXdsServerTarget target("localhost:1234", channel_creds_config_,
                             /*call_creds_configs=*/{},
                             /*initial_metadata=*/{{"key1", "val1"}},
                             Duration::Seconds(10));
  absl::Status status1;
  auto transport1 = factory_->GetTransport(target, &status1);
  ASSERT_TRUE(status1.ok()) << status1.ToString();
  absl::Status status2;
  auto transport2 = factory_->GetTransport(target, &status2);
  ASSERT_TRUE(status2.ok()) << status2.ToString();
  EXPECT_EQ(transport1, transport2);
}

TEST_F(GrpcXdsTransportTest, DifferingMetadataSharesChannel) {
  ExecCtx exec_ctx;
  GrpcXdsServerTarget target1("localhost:1234", channel_creds_config_,
                              /*call_creds_configs=*/{},
                              /*initial_metadata=*/{{"key1", "val1"}},
                              Duration::Seconds(10));
  GrpcXdsServerTarget target2("localhost:1234", channel_creds_config_,
                              /*call_creds_configs=*/{},
                              /*initial_metadata=*/{{"key2", "val2"}},
                              Duration::Seconds(10));
  EXPECT_NE(target1.Key(), target2.Key());
  absl::Status status1;
  auto transport1 = factory_->GetTransport(target1, &status1);
  ASSERT_TRUE(status1.ok()) << status1.ToString();
  absl::Status status2;
  auto transport2 = factory_->GetTransport(target2, &status2);
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
  GrpcXdsServerTarget target1("localhost:1234", channel_creds_config_,
                              /*call_creds_configs=*/{},
                              /*initial_metadata=*/{{"key1", "val1"}},
                              Duration::Seconds(10));
  GrpcXdsServerTarget target2("localhost:1234", channel_creds_config_,
                              /*call_creds_configs=*/{},
                              /*initial_metadata=*/{{"key1", "val1"}},
                              Duration::Seconds(20));
  EXPECT_NE(target1.Key(), target2.Key());
  absl::Status status1;
  auto transport1 = factory_->GetTransport(target1, &status1);
  ASSERT_TRUE(status1.ok()) << status1.ToString();
  absl::Status status2;
  auto transport2 = factory_->GetTransport(target2, &status2);
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
  GrpcXdsServerTarget target1("localhost:1234", channel_creds_config_,
                              /*call_creds_configs=*/{},
                              /*initial_metadata=*/{{"key1", "val1"}},
                              Duration::Seconds(10));
  GrpcXdsServerTarget target2("localhost:5678", channel_creds_config_,
                              /*call_creds_configs=*/{},
                              /*initial_metadata=*/{{"key1", "val1"}},
                              Duration::Seconds(10));
  EXPECT_NE(target1.Key(), target2.Key());
  absl::Status status1;
  auto transport1 = factory_->GetTransport(target1, &status1);
  ASSERT_TRUE(status1.ok()) << status1.ToString();
  absl::Status status2;
  auto transport2 = factory_->GetTransport(target2, &status2);
  ASSERT_TRUE(status2.ok()) << status2.ToString();
  EXPECT_NE(transport1, transport2);
  auto* grpc_transport1 =
      DownCast<GrpcXdsTransportFactory::GrpcXdsTransport*>(transport1.get());
  auto* grpc_transport2 =
      DownCast<GrpcXdsTransportFactory::GrpcXdsTransport*>(transport2.get());
  EXPECT_NE(grpc_transport1->channel(), grpc_transport2->channel());
}

class GrpcXdsServerTargetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto json = JsonParse("{\"channel_creds\": [{\"type\": \"insecure\"}]}");
    ASSERT_TRUE(json.ok()) << json.status().ToString();
    ValidationErrors errors;
    insecure_creds_ = ParseXdsBootstrapChannelCreds(*json, JsonArgs(), &errors);
    ASSERT_TRUE(errors.ok());
    ASSERT_NE(insecure_creds_, nullptr);
    auto json_gd =
        JsonParse("{\"channel_creds\": [{\"type\": \"google_default\"}]}");
    ASSERT_TRUE(json_gd.ok()) << json_gd.status().ToString();
    google_default_creds_ =
        ParseXdsBootstrapChannelCreds(*json_gd, JsonArgs(), &errors);
    ASSERT_TRUE(errors.ok());
    ASSERT_NE(google_default_creds_, nullptr);
    auto json_call = JsonParse(
        "{\"call_creds\": [{\"type\": \"jwt_token_file\", \"config\": "
        "{\"jwt_token_file\": \"/foo\"}}]}");
    ASSERT_TRUE(json_call.ok()) << json_call.status().ToString();
    call_creds_ = ParseXdsBootstrapCallCreds(*json_call, JsonArgs(), &errors);
    ASSERT_TRUE(errors.ok());
    ASSERT_FALSE(call_creds_.empty());
  }

  RefCountedPtr<const ChannelCredsConfig> insecure_creds_;
  RefCountedPtr<const ChannelCredsConfig> google_default_creds_;
  std::vector<RefCountedPtr<const CallCredsConfig>> call_creds_;
};

TEST_F(GrpcXdsServerTargetTest, IdenticalTargetsAreEqual) {
  GrpcXdsServerTarget target1("localhost:1234", insecure_creds_, call_creds_,
                              {{"k1", "v1"}}, Duration::Seconds(10));
  GrpcXdsServerTarget target2("localhost:1234", insecure_creds_, call_creds_,
                              {{"k1", "v1"}}, Duration::Seconds(10));
  EXPECT_EQ(target1, target2);
  EXPECT_EQ(target1.Key(), target2.Key());
}

TEST_F(GrpcXdsServerTargetTest, DifferentUriAreNotEqual) {
  GrpcXdsServerTarget target1("localhost:1234", insecure_creds_, call_creds_,
                              {{"k1", "v1"}}, Duration::Seconds(10));
  GrpcXdsServerTarget target2("localhost:5678", insecure_creds_, call_creds_,
                              {{"k1", "v1"}}, Duration::Seconds(10));
  EXPECT_NE(target1, target2);
  EXPECT_NE(target1.Key(), target2.Key());
}

TEST_F(GrpcXdsServerTargetTest, DifferentChannelCredsAreNotEqual) {
  GrpcXdsServerTarget target1("localhost:1234", insecure_creds_, call_creds_,
                              {{"k1", "v1"}}, Duration::Seconds(10));
  GrpcXdsServerTarget target2("localhost:1234", google_default_creds_,
                              call_creds_, {{"k1", "v1"}},
                              Duration::Seconds(10));
  EXPECT_NE(target1, target2);
  EXPECT_NE(target1.Key(), target2.Key());
}

TEST_F(GrpcXdsServerTargetTest, DifferentCallCredsAreNotEqual) {
  GrpcXdsServerTarget target1("localhost:1234", insecure_creds_, call_creds_,
                              {{"k1", "v1"}}, Duration::Seconds(10));
  GrpcXdsServerTarget target2("localhost:1234", insecure_creds_, {},
                              {{"k1", "v1"}}, Duration::Seconds(10));
  EXPECT_NE(target1, target2);
  EXPECT_NE(target1.Key(), target2.Key());
}

TEST_F(GrpcXdsServerTargetTest, DifferentInitialMetadataAreNotEqual) {
  GrpcXdsServerTarget target1("localhost:1234", insecure_creds_, call_creds_,
                              {{"k1", "v1"}}, Duration::Seconds(10));
  GrpcXdsServerTarget target2("localhost:1234", insecure_creds_, call_creds_,
                              {{"k2", "v2"}}, Duration::Seconds(10));
  EXPECT_NE(target1, target2);
  EXPECT_NE(target1.Key(), target2.Key());
}

TEST_F(GrpcXdsServerTargetTest, DifferentTimeoutAreNotEqual) {
  GrpcXdsServerTarget target1("localhost:1234", insecure_creds_, call_creds_,
                              {{"k1", "v1"}}, Duration::Seconds(10));
  GrpcXdsServerTarget target2("localhost:1234", insecure_creds_, call_creds_,
                              {{"k1", "v1"}}, Duration::Seconds(20));
  EXPECT_NE(target1, target2);
  EXPECT_NE(target1.Key(), target2.Key());
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
