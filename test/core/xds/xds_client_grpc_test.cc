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

#include <grpc/grpc.h>

#include <memory>

#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

std::shared_ptr<GrpcXdsBootstrap> MakeBootstrap() {
  auto bootstrap = GrpcXdsBootstrap::Create(
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
