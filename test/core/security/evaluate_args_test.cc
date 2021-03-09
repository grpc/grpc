// Copyright 2020 gRPC authors.
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

#include "absl/strings/string_view.h"

#include "src/core/lib/security/authorization/evaluate_args.h"
#include "test/core/util/mock_eval_args_endpoint.h"
#include "test/core/util/test_config.h"

namespace grpc_core {

namespace {

constexpr char kIpv4LocalUri[] = "ipv4:255.255.255.255:123";
constexpr char kIpv4LocalAddress[] = "255.255.255.255";
constexpr int kIpv4LocalPort = 123;
constexpr char kIpv4PeerUri[] = "ipv4:128.128.128.128:321";
constexpr char kIpv4PeerAddress[] = "128.128.128.128";
constexpr int kIpv4PeerPort = 321;
constexpr char kIpv6LocalUri[] =
    "ipv6:[2001:0db8:85a3:0000:0000:8a2e:0370:7334]:456";
constexpr char kIpv6LocalAddress[] = "2001:0db8:85a3:0000:0000:8a2e:0370:7334";
constexpr int kIpv6LocalPort = 456;
constexpr char kIpv6PeerUri[] = "ipv6:[2001:db8::1]:654";
constexpr char kIpv6PeerAddress[] = "2001:db8::1";
constexpr int kIpv6PeerPort = 654;

}  // namespace

TEST(EvaluateArgsEndpointTest, TestEvaluateArgsIpv4LocalAddress) {
  MockEvalArgsEndpoint endpoint(kIpv4LocalUri, kIpv4PeerUri);
  EvaluateArgs args(nullptr, nullptr, &endpoint);
  EXPECT_EQ(args.GetLocalAddress(), kIpv4LocalAddress);
}

TEST(EvaluateArgsEndpointTest, TestEvaluateArgsIpv4LocalPort) {
  MockEvalArgsEndpoint endpoint(kIpv4LocalUri, kIpv4PeerUri);
  EvaluateArgs args(nullptr, nullptr, &endpoint);
  EXPECT_EQ(args.GetLocalPort(), kIpv4LocalPort);
}

TEST(EvaluateArgsEndpointTest, TestEvaluateArgsIpv4PeerAddress) {
  MockEvalArgsEndpoint endpoint(kIpv4LocalUri, kIpv4PeerUri);
  EvaluateArgs args(nullptr, nullptr, &endpoint);
  EXPECT_EQ(args.GetPeerAddress(), kIpv4PeerAddress);
}

TEST(EvaluateArgsEndpointTest, TestEvaluateArgsIpv4PeerPort) {
  MockEvalArgsEndpoint endpoint(kIpv4LocalUri, kIpv4PeerUri);
  EvaluateArgs args(nullptr, nullptr, &endpoint);
  EXPECT_EQ(args.GetPeerPort(), kIpv4PeerPort);
}

TEST(EvaluateArgsEndpointTest, TestEvaluateArgsIpv6LocalAddress) {
  MockEvalArgsEndpoint endpoint(kIpv6LocalUri, kIpv6PeerUri);
  EvaluateArgs args(nullptr, nullptr, &endpoint);
  EXPECT_EQ(args.GetLocalAddress(), kIpv6LocalAddress);
}

TEST(EvaluateArgsEndpointTest, TestEvaluateArgsIpv6LocalPort) {
  MockEvalArgsEndpoint endpoint(kIpv6LocalUri, kIpv6PeerUri);
  EvaluateArgs args(nullptr, nullptr, &endpoint);
  EXPECT_EQ(args.GetLocalPort(), kIpv6LocalPort);
}

TEST(EvaluateArgsEndpointTest, TestEvaluateArgsIpv6PeerAddress) {
  MockEvalArgsEndpoint endpoint(kIpv6LocalUri, kIpv6PeerUri);
  EvaluateArgs args(nullptr, nullptr, &endpoint);
  EXPECT_EQ(args.GetPeerAddress(), kIpv6PeerAddress);
}

TEST(EvaluateArgsEndpointTest, TestEvaluateArgsIpv6PeerPort) {
  MockEvalArgsEndpoint endpoint(kIpv6LocalUri, kIpv6PeerUri);
  EvaluateArgs args(nullptr, nullptr, &endpoint);
  EXPECT_EQ(args.GetPeerPort(), kIpv6PeerPort);
}

TEST(EvaluateArgsMetadataTest, HandlesNullMetadata) {
  EvaluateArgs eval_args(nullptr, nullptr, nullptr);
  EXPECT_EQ(eval_args.GetPath(), nullptr);
  EXPECT_EQ(eval_args.GetMethod(), nullptr);
  EXPECT_EQ(eval_args.GetHost(), nullptr);
  EXPECT_THAT(eval_args.GetHeaders(), ::testing::ElementsAre());
  EXPECT_EQ(eval_args.GetHeaderValue("some_key", nullptr), absl::nullopt);
}

TEST(EvaluateArgsMetadataTest, HandlesEmptyMetadata) {
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  EvaluateArgs eval_args(&metadata, nullptr, nullptr);
  EXPECT_EQ(eval_args.GetPath(), nullptr);
  EXPECT_EQ(eval_args.GetMethod(), nullptr);
  EXPECT_EQ(eval_args.GetHost(), nullptr);
  EXPECT_THAT(eval_args.GetHeaders(), ::testing::ElementsAre());
  EXPECT_EQ(eval_args.GetHeaderValue("some_key", nullptr), absl::nullopt);
  grpc_metadata_batch_destroy(&metadata);
}

TEST(EvaluateArgsMetadataTest, GetPathSuccess) {
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
  EvaluateArgs eval_args(&metadata, nullptr, nullptr);
  EXPECT_EQ(eval_args.GetPath(), kPath);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsMetadataTest, GetHostSuccess) {
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
  EvaluateArgs eval_args(&metadata, nullptr, nullptr);
  EXPECT_EQ(eval_args.GetHost(), kHost);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsMetadataTest, GetMethodSuccess) {
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
  EvaluateArgs eval_args(&metadata, nullptr, nullptr);
  EXPECT_EQ(eval_args.GetMethod(), kMethod);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsMetadataTest, GetHeadersSuccess) {
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
  EvaluateArgs eval_args(&metadata, nullptr, nullptr);
  EXPECT_THAT(
      eval_args.GetHeaders(),
      ::testing::UnorderedElementsAre(
          ::testing::Pair(StringViewFromSlice(GRPC_MDSTR_HOST), kHost),
          ::testing::Pair(StringViewFromSlice(GRPC_MDSTR_PATH), kPath)));
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsMetadataTest, GetHeaderValueSuccess) {
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
  EvaluateArgs eval_args(&metadata, nullptr, nullptr);
  std::string concatenated_value;
  absl::optional<absl::string_view> value =
      eval_args.GetHeaderValue(kKey, &concatenated_value);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), kValue);
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(EvaluateArgsAuthContextTest, HandlesNullAuthContext) {
  EvaluateArgs eval_args(nullptr, nullptr, nullptr);
  EXPECT_EQ(eval_args.GetSpiffeId(), nullptr);
  EXPECT_EQ(eval_args.GetCommonNameInPeerCert(), nullptr);
  EXPECT_EQ(eval_args.GetTransportSecurityType(), nullptr);
}

TEST(EvaluateArgsAuthContextTest, HandlesEmptyAuthCtx) {
  grpc_auth_context auth_context(nullptr);
  EvaluateArgs eval_args(nullptr, &auth_context, nullptr);
  EXPECT_EQ(eval_args.GetSpiffeId(), nullptr);
  EXPECT_EQ(eval_args.GetCommonNameInPeerCert(), nullptr);
}

TEST(EvaluateArgsAuthContextTest, GetSpiffeIdSuccessOneProperty) {
  grpc_auth_context auth_context(nullptr);
  const char* kId = "spiffeid";
  auth_context.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, kId);
  EvaluateArgs eval_args(nullptr, &auth_context, nullptr);
  EXPECT_EQ(eval_args.GetSpiffeId(), kId);
}

TEST(EvaluateArgsAuthContextTest, GetSpiffeIdFailDuplicateProperty) {
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id1");
  auth_context.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id2");
  EvaluateArgs eval_args(nullptr, &auth_context, nullptr);
  EXPECT_EQ(eval_args.GetSpiffeId(), nullptr);
}

TEST(EvaluateArgsAuthContextTest, GetCommonNameInPeerCertSuccessOneProperty) {
  grpc_auth_context auth_context(nullptr);
  const char* kServer = "server";
  auth_context.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, kServer);
  EvaluateArgs eval_args(nullptr, &auth_context, nullptr);
  EXPECT_EQ(eval_args.GetCommonNameInPeerCert(), kServer);
}

TEST(EvaluateArgsAuthContextTest,
     GetCommonNameInPeerCertFailDuplicateProperty) {
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, "server1");
  auth_context.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, "server2");
  EvaluateArgs eval_args(nullptr, &auth_context, nullptr);
  EXPECT_EQ(eval_args.GetCommonNameInPeerCert(), nullptr);
}

TEST(EvaluateArgsAuthContextTest, GetTransportSecurityTypeSuccessOneProperty) {
  grpc_auth_context auth_context(nullptr);
  const char* kType = "ssl";
  auth_context.add_cstring_property(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                    kType);
  EvaluateArgs eval_args(nullptr, &auth_context, nullptr);
  EXPECT_EQ(eval_args.GetTransportSecurityType(), kType);
}

TEST(EvaluateArgsAuthContextTest,
     GetTransportSecurityTypeFailDuplicateProperty) {
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                    "type1");
  auth_context.add_cstring_property(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                    "type2");
  EvaluateArgs eval_args(nullptr, &auth_context, nullptr);
  EXPECT_EQ(eval_args.GetTransportSecurityType(), nullptr);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
