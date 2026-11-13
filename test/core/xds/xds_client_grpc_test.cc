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

#include "src/core/xds/grpc/xds_client_grpc.h"

#include <grpc/channel_factory.h>
#include <grpc/grpc.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/client_channel/channel_factory.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc_builder.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/grpc/xds_transport_factory_grpc.h"
#include "src/core/xds/grpc/xds_transport_grpc.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

std::shared_ptr<GrpcXdsBootstrap> MakeBootstrap() {
  auto bootstrap = GrpcXdsBootstrapBuilder::Build(
      "{\"xds_servers\": ["
      "{\"server_uri\": \"dns://example.com\", "
      "\"channel_creds\": [{\"type\":\"insecure\"}]}"
      "]}");
  GRPC_CHECK(bootstrap.ok()) << bootstrap.status();
  return std::move(*bootstrap);
}

TEST(GrpcXdsClient, GetOrCreate) {
  constexpr absl::string_view kKey = "foo";
  auto bootstrap = MakeBootstrap();
  // Initial get will create the instance.
  auto xds_client =
      GrpcXdsClient::GetOrCreate(kKey, ChannelArgs(), "reason", bootstrap);
  ASSERT_TRUE(xds_client.ok()) << xds_client.status();
  // Getting again with the same parameters returns the same instance.
  EXPECT_EQ(
      GrpcXdsClient::GetOrCreate(kKey, ChannelArgs(), "reason", bootstrap),
      xds_client);
  // Unref the original instance.
  auto weak_ref = (*xds_client)->WeakRef();
  xds_client->reset();
  // Getting again will create a new instance.
  auto xds_client2 =
      GrpcXdsClient::GetOrCreate(kKey, ChannelArgs(), "reason", bootstrap);
  ASSERT_TRUE(xds_client2.ok()) << xds_client2.status();
  EXPECT_NE(xds_client2->get(), weak_ref.get());
}

// This test covers a bug whereby we replaced the map entry for the
// instance whose ref-count was zero but failed to replace the key,
// which pointed into the original (now deleted) instance.
TEST(GrpcXdsClient, GetOrCreateReplacesIfRefcountIsZero) {
  constexpr absl::string_view kKey = "foo";
  auto bootstrap = MakeBootstrap();
  // Initial get will create the instance.
  auto xds_client =
      GrpcXdsClient::GetOrCreate(kKey, ChannelArgs(), "reason", bootstrap);
  ASSERT_TRUE(xds_client.ok()) << xds_client.status();
  // Unref it, but inhibit removing it from the map.
  internal::SetInhibitXdsClientMapRemovalForTest(true);
  auto weak_ref = (*xds_client)->WeakRef();
  xds_client->reset();
  internal::SetInhibitXdsClientMapRemovalForTest(false);
  // Getting again with the same parameters will return a new instance,
  // since the old one has a ref-count of zero.  The map key should no
  // longer point into the original instance.
  auto xds_client2 =
      GrpcXdsClient::GetOrCreate(kKey, ChannelArgs(), "reason", bootstrap);
  ASSERT_TRUE(xds_client2.ok()) << xds_client2.status();
  // Now free the original instance.  If the map key was still pointing
  // into the original instance, this would cause it to be invalid.
  weak_ref.reset();
  // Getting again with the same parameters should return the same
  // instance again without crashing.
  EXPECT_EQ(
      GrpcXdsClient::GetOrCreate(kKey, ChannelArgs(), "reason", bootstrap),
      xds_client2);
}

// Returns the transport that a ChannelFactory handle wraps.
GrpcXdsTransport* GetTransport(
    const experimental::ChannelFactory::ChannelHandle& handle) {
  return DownCast<GrpcXdsTransport*>(
      DownCast<const ChannelHandleImpl&>(handle).transport().get());
}

TEST(GrpcXdsClient, CreateChannelFactory) {
  constexpr absl::string_view kKey = "channel_factory_key";
  constexpr absl::string_view kTargetUri = "dns:///autosharding.example.com";
  ExecCtx exec_ctx;
  auto xds_client = GrpcXdsClient::GetOrCreate("foo", ChannelArgs(), "reason",
                                               MakeBootstrap());
  ASSERT_TRUE(xds_client.ok()) << xds_client.status();
  ValidationErrors errors;
  auto channel_creds_config = ParseXdsBootstrapChannelCreds(
      Json::FromObject(
          {{"channel_creds", Json::FromArray({Json::FromObject(
                                 {{"type", Json::FromString("insecure")}})})}}),
      JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok());
  XdsTransportFactoryWrapper::TargetMap targets;
  targets.emplace(kKey,
                  std::make_shared<GrpcXdsServerTarget>(
                      std::string(kTargetUri), std::move(channel_creds_config),
                      std::vector<RefCountedPtr<const CallCredsConfig>>()));
  auto channel_factory =
      (*xds_client)->CreateChannelFactory(std::move(targets));
  ASSERT_NE(channel_factory, nullptr);
  auto handle1 = channel_factory->CreateChannel(kKey);
  ASSERT_NE(handle1, nullptr);
  EXPECT_FALSE(GetTransport(*handle1)->channel()->IsLame());
  EXPECT_EQ(GetTransport(*handle1)->channel()->target(), kTargetUri);
  // While handle1 is alive, the key gets the same transport.
  auto handle2 = channel_factory->CreateChannel(kKey);
  ASSERT_NE(handle2, nullptr);
  EXPECT_EQ(GetTransport(*handle1), GetTransport(*handle2));
  // An unknown key gets a lame channel.
  auto handle3 = channel_factory->CreateChannel("unknown");
  ASSERT_NE(handle3, nullptr);
  EXPECT_TRUE(GetTransport(*handle3)->channel()->IsLame());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
