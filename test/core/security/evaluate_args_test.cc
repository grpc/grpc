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
#include "test/core/util/authorization_args.h"
#include "test/core/util/test_config.h"

namespace grpc_core {

TEST(EvaluateArgsTest, EmptyMetadata) {
  AuthorizationArgs args;
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_EQ(eval_args.GetPath(), nullptr);
  EXPECT_EQ(eval_args.GetMethod(), nullptr);
  EXPECT_EQ(eval_args.GetHost(), nullptr);
  EXPECT_THAT(eval_args.GetHeaders(), ::testing::ElementsAre());
  EXPECT_EQ(eval_args.GetHeaderValue("some_key", nullptr), absl::nullopt);
}

TEST(EvaluateArgsTest, GetPathSuccess) {
  AuthorizationArgs args;
  args.AddPairToMetadata(":path", "/expected/path");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_EQ(eval_args.GetPath(), "/expected/path");
}

TEST(EvaluateArgsTest, GetHostSuccess) {
  AuthorizationArgs args;
  args.AddPairToMetadata("host", "host123");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_EQ(eval_args.GetHost(), "host123");
}

TEST(EvaluateArgsTest, GetMethodSuccess) {
  AuthorizationArgs args;
  args.AddPairToMetadata(":method", "GET");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_EQ(eval_args.GetMethod(), "GET");
}

TEST(EvaluateArgsTest, GetHeadersSuccess) {
  AuthorizationArgs args;
  args.AddPairToMetadata("host", "host123");
  args.AddPairToMetadata(":path", "/expected/path");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_THAT(eval_args.GetHeaders(),
              ::testing::UnorderedElementsAre(
                  ::testing::Pair("host", "host123"),
                  ::testing::Pair(":path", "/expected/path")));
}

TEST(EvaluateArgsTest, GetHeaderValueSuccess) {
  AuthorizationArgs args;
  args.AddPairToMetadata("key123", "value123");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  std::string concatenated_value;
  absl::optional<absl::string_view> value =
      eval_args.GetHeaderValue("key123", &concatenated_value);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), "value123");
}

TEST(EvaluateArgsTest, TestIpv4LocalAddressAndPort) {
  AuthorizationArgs args;
  args.SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_EQ(eval_args.GetLocalAddress(), "255.255.255.255");
  EXPECT_EQ(eval_args.GetLocalPort(), 123);
}

TEST(EvaluateArgsTest, TestIpv4PeerAddressAndPort) {
  AuthorizationArgs args;
  args.SetPeerEndpoint("ipv4:128.128.128.128:321");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_EQ(eval_args.GetPeerAddress(), "128.128.128.128");
  EXPECT_EQ(eval_args.GetPeerPort(), 321);
}

TEST(EvaluateArgsTest, TestIpv6LocalAddressAndPort) {
  AuthorizationArgs args;
  args.SetLocalEndpoint("ipv6:[2001:0db8:85a3:0000:0000:8a2e:0370:7334]:456");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_EQ(eval_args.GetLocalAddress(),
            "2001:0db8:85a3:0000:0000:8a2e:0370:7334");
  EXPECT_EQ(eval_args.GetLocalPort(), 456);
}

TEST(EvaluateArgsTest, TestIpv6PeerAddressAndPort) {
  AuthorizationArgs args;
  args.SetPeerEndpoint("ipv6:[2001:db8::1]:654");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_EQ(eval_args.GetPeerAddress(), "2001:db8::1");
  EXPECT_EQ(eval_args.GetPeerPort(), 654);
}

TEST(EvaluateArgsTest, EmptyAuthContext) {
  AuthorizationArgs args;
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_TRUE(eval_args.GetTransportSecurityType().empty());
  EXPECT_TRUE(eval_args.GetSpiffeId().empty());
  EXPECT_TRUE(eval_args.GetCommonName().empty());
}

TEST(EvaluateArgsTest, GetTransportSecurityTypeSuccessOneProperty) {
  AuthorizationArgs args;
  args.AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                "ssl");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_EQ(eval_args.GetTransportSecurityType(), "ssl");
}

TEST(EvaluateArgsTest, GetTransportSecurityTypeFailDuplicateProperty) {
  AuthorizationArgs args;
  args.AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                "type1");
  args.AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                "type2");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_TRUE(eval_args.GetTransportSecurityType().empty());
}

TEST(EvaluateArgsTest, GetSpiffeIdSuccessOneProperty) {
  AuthorizationArgs args;
  args.AddPropertyToAuthContext(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id123");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_EQ(eval_args.GetSpiffeId(), "id123");
}

TEST(EvaluateArgsTest, GetSpiffeIdFailDuplicateProperty) {
  AuthorizationArgs args;
  args.AddPropertyToAuthContext(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id123");
  args.AddPropertyToAuthContext(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id456");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_TRUE(eval_args.GetSpiffeId().empty());
}

TEST(EvaluateArgsTest, GetCommonNameSuccessOneProperty) {
  AuthorizationArgs args;
  args.AddPropertyToAuthContext(GRPC_X509_CN_PROPERTY_NAME, "server123");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_EQ(eval_args.GetCommonName(), "server123");
}

TEST(EvaluateArgsTest, GetCommonNameFailDuplicateProperty) {
  AuthorizationArgs args;
  args.AddPropertyToAuthContext(GRPC_X509_CN_PROPERTY_NAME, "server123");
  args.AddPropertyToAuthContext(GRPC_X509_CN_PROPERTY_NAME, "server456");
  EvaluateArgs eval_args = args.MakeEvaluateArgs();
  EXPECT_TRUE(eval_args.GetCommonName().empty());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
