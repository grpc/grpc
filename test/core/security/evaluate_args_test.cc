// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/security/authorization/evaluate_args.h"
#include "test/core/util/mock_authorization_endpoint.h"
#include "test/core/util/test_config.h"

namespace grpc_core {

TEST(EvaluateHeaderArgsTest, HandlesNullMetadata) {
  EvaluateArgs args(nullptr, nullptr, nullptr);
  EXPECT_EQ(args.GetPath(), nullptr);
  EXPECT_EQ(args.GetMethod(), nullptr);
  EXPECT_EQ(args.GetHost(), nullptr);
  EXPECT_THAT(args.GetHeaders(), ::testing::ElementsAre());
  EXPECT_EQ(args.GetHeaderValue("some_key", nullptr), absl::nullopt);
}

TEST(EvaluateArgsTest, HandlesEmptyMetadata) {
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  EvaluateArgs args(nullptr, nullptr, &metadata);
  EXPECT_EQ(args.GetPath(), nullptr);
  EXPECT_EQ(args.GetMethod(), nullptr);
  EXPECT_EQ(args.GetHost(), nullptr);
  EXPECT_THAT(args.GetHeaders(), ::testing::ElementsAre());
  EXPECT_EQ(args.GetHeaderValue("some_key", nullptr), absl::nullopt);
  grpc_metadata_batch_destroy(&metadata);
}

TEST(EvaluateArgsTest, GetPathSuccess) {
  grpc_init();
  const char* kPath = "/some/path";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_slice fake_val = grpc_slice_intern(grpc_slice_from_static_string(kPath));
  grpc_mdelem fake_val_md = grpc_mdelem_from_slices(GRPC_MDSTR_PATH, fake_val);
  grpc_linked_mdelem storage;
  storage.md = fake_val_md;
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage),
            GRPC_ERROR_NONE);
  EvaluateArgs args(nullptr, nullptr, &metadata);
  EXPECT_EQ(args.GetPath(), kPath);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsTest, GetHostSuccess) {
  grpc_init();
  const char* kHost = "host";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_slice fake_val = grpc_slice_intern(grpc_slice_from_static_string(kHost));
  grpc_mdelem fake_val_md = grpc_mdelem_from_slices(GRPC_MDSTR_HOST, fake_val);
  grpc_linked_mdelem storage;
  storage.md = fake_val_md;
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage),
            GRPC_ERROR_NONE);
  EvaluateArgs args(nullptr, nullptr, &metadata);
  EXPECT_EQ(args.GetHost(), kHost);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsTest, GetMethodSuccess) {
  grpc_init();
  const char* kMethod = "GET";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_slice fake_val =
      grpc_slice_intern(grpc_slice_from_static_string(kMethod));
  grpc_mdelem fake_val_md =
      grpc_mdelem_from_slices(GRPC_MDSTR_METHOD, fake_val);
  grpc_linked_mdelem storage;
  storage.md = fake_val_md;
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage),
            GRPC_ERROR_NONE);
  EvaluateArgs args(nullptr, nullptr, &metadata);
  EXPECT_EQ(args.GetMethod(), kMethod);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsTest, GetHeadersSuccess) {
  grpc_init();
  const char* kPath = "/some/path";
  const char* kHost = "host";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_slice fake_path =
      grpc_slice_intern(grpc_slice_from_static_string(kPath));
  grpc_mdelem fake_path_md =
      grpc_mdelem_from_slices(GRPC_MDSTR_PATH, fake_path);
  grpc_linked_mdelem storage;
  storage.md = fake_path_md;
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage, GRPC_BATCH_PATH),
            GRPC_ERROR_NONE);
  grpc_slice fake_host =
      grpc_slice_intern(grpc_slice_from_static_string(kHost));
  grpc_mdelem fake_host_md =
      grpc_mdelem_from_slices(GRPC_MDSTR_HOST, fake_host);
  grpc_linked_mdelem storage2;
  storage2.md = fake_host_md;
  ASSERT_EQ(
      grpc_metadata_batch_link_tail(&metadata, &storage2, GRPC_BATCH_HOST),
      GRPC_ERROR_NONE);
  EvaluateArgs args(nullptr, nullptr, &metadata);
  EXPECT_THAT(
      args.GetHeaders(),
      ::testing::UnorderedElementsAre(
          ::testing::Pair(StringViewFromSlice(GRPC_MDSTR_HOST), kHost),
          ::testing::Pair(StringViewFromSlice(GRPC_MDSTR_PATH), kPath)));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsTest, GetHeaderValueSuccess) {
  grpc_init();
  const char* kKey = "some_key";
  const char* kValue = "some_value";
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  storage.md = grpc_mdelem_from_slices(
      grpc_slice_intern(grpc_slice_from_static_string(kKey)),
      grpc_slice_intern(grpc_slice_from_static_string(kValue)));
  ASSERT_EQ(grpc_metadata_batch_link_head(&metadata, &storage),
            GRPC_ERROR_NONE);
  EvaluateArgs args(nullptr, nullptr, &metadata);
  std::string concatenated_value;
  absl::optional<absl::string_view> value =
      args.GetHeaderValue(kKey, &concatenated_value);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), kValue);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsTest, HandlesNullEndpoint) {
  EvaluateArgs args(nullptr, nullptr, nullptr);
  EXPECT_TRUE(args.GetLocalAddress().empty());
  EXPECT_EQ(args.GetLocalPort(), 0);
  EXPECT_TRUE(args.GetPeerAddress().empty());
  EXPECT_EQ(args.GetPeerPort(), 0);
}

TEST(EvaluateArgsTest, TestIpv4LocalAddressAndPort) {
  MockAuthorizationEndpoint endpoint("ipv4:255.255.255.255:123", "");
  EvaluateArgs args(nullptr, &endpoint, nullptr);
  EXPECT_EQ(args.GetLocalAddress(), "255.255.255.255");
  EXPECT_EQ(args.GetLocalPort(), 123);
}

TEST(EvaluateArgsTest, TestIpv4PeerAddressAndPort) {
  MockAuthorizationEndpoint endpoint("", "ipv4:128.128.128.128:321");
  EvaluateArgs args(nullptr, &endpoint, nullptr);
  EXPECT_EQ(args.GetPeerAddress(), "128.128.128.128");
  EXPECT_EQ(args.GetPeerPort(), 321);
}

TEST(EvaluateArgsTest, TestIpv6LocalAddressAndPort) {
  MockAuthorizationEndpoint endpoint(
      "ipv6:[2001:0db8:85a3:0000:0000:8a2e:0370:7334]:456", "");
  EvaluateArgs args(nullptr, &endpoint, nullptr);
  EXPECT_EQ(args.GetLocalAddress(), "2001:0db8:85a3:0000:0000:8a2e:0370:7334");
  EXPECT_EQ(args.GetLocalPort(), 456);
}

TEST(EvaluateArgsTest, TestIpv6PeerAddressAndPort) {
  MockAuthorizationEndpoint endpoint("", "ipv6:[2001:db8::1]:654");
  EvaluateArgs args(nullptr, &endpoint, nullptr);
  EXPECT_EQ(args.GetPeerAddress(), "2001:db8::1");
  EXPECT_EQ(args.GetPeerPort(), 654);
}

TEST(EvaluateArgsTest, HandlesNullAuthContext) {
  EvaluateArgs args(nullptr, nullptr, nullptr);
  EXPECT_TRUE(args.GetTransportSecurityType().empty());
  EXPECT_TRUE(args.GetSpiffeId().empty());
  EXPECT_TRUE(args.GetCommonName().empty());
}

TEST(EvaluateArgsTest, HandlesEmptyAuthContext) {
  grpc_auth_context auth_context(nullptr);
  EvaluateArgs args(&auth_context, nullptr, nullptr);
  EXPECT_TRUE(args.GetTransportSecurityType().empty());
  EXPECT_TRUE(args.GetSpiffeId().empty());
  EXPECT_TRUE(args.GetCommonName().empty());
}

TEST(EvaluateArgsTest, GetTransportSecurityTypeSuccessOneProperty) {
  grpc_auth_context auth_context(nullptr);
  const char* kType = "ssl";
  auth_context.add_cstring_property(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                    kType);
  EvaluateArgs args(&auth_context, nullptr, nullptr);
  EXPECT_EQ(args.GetTransportSecurityType(), kType);
}

TEST(EvaluateArgsTest, GetTransportSecurityTypeFailDuplicateProperty) {
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                    "type1");
  auth_context.add_cstring_property(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                    "type2");
  EvaluateArgs args(&auth_context, nullptr, nullptr);
  EXPECT_TRUE(args.GetTransportSecurityType().empty());
}

TEST(EvaluateArgsTest, GetSpiffeIdSuccessOneProperty) {
  grpc_auth_context auth_context(nullptr);
  const char* kId = "spiffeid";
  auth_context.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, kId);
  EvaluateArgs args(&auth_context, nullptr, nullptr);
  EXPECT_EQ(args.GetSpiffeId(), kId);
}

TEST(EvaluateArgsTest, GetSpiffeIdFailDuplicateProperty) {
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id1");
  auth_context.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id2");
  EvaluateArgs args(&auth_context, nullptr, nullptr);
  EXPECT_TRUE(args.GetSpiffeId().empty());
}

TEST(EvaluateArgsTest, GetCommonNameSuccessOneProperty) {
  grpc_auth_context auth_context(nullptr);
  const char* kServer = "server";
  auth_context.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, kServer);
  EvaluateArgs args(&auth_context, nullptr, nullptr);
  EXPECT_EQ(args.GetCommonName(), kServer);
}

TEST(EvaluateArgsTest, GetCommonNameFailDuplicateProperty) {
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, "server1");
  auth_context.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, "server2");
  EvaluateArgs args(&auth_context, nullptr, nullptr);
  EXPECT_TRUE(args.GetCommonName().empty());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
