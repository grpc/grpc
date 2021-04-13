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

#include "src/core/lib/security/authorization/evaluate_channel_args.h"
#include "test/core/util/mock_authorization_endpoint.h"
#include "test/core/util/test_config.h"

namespace grpc_core {

TEST(EvaluateChannelArgsTest, HandlesNullEndpoint) {
  EvaluateChannelArgs args(nullptr, nullptr);
  EXPECT_TRUE(args.GetLocalAddress().empty());
  EXPECT_EQ(args.GetLocalPort(), 0);
  EXPECT_TRUE(args.GetPeerAddress().empty());
  EXPECT_EQ(args.GetPeerPort(), 0);
}

TEST(EvaluateChannelArgsTest, TestIpv4LocalAddressAndPort) {
  MockAuthorizationEndpoint endpoint("ipv4:255.255.255.255:123", "");
  EvaluateChannelArgs args(nullptr, &endpoint);
  EXPECT_EQ(args.GetLocalAddress(), "255.255.255.255");
  EXPECT_EQ(args.GetLocalPort(), 123);
}

TEST(EvaluateChannelArgsTest, TestIpv4PeerAddressAndPort) {
  MockAuthorizationEndpoint endpoint("", "ipv4:128.128.128.128:321");
  EvaluateChannelArgs args(nullptr, &endpoint);
  EXPECT_EQ(args.GetPeerAddress(), "128.128.128.128");
  EXPECT_EQ(args.GetPeerPort(), 321);
}

TEST(EvaluateChannelArgsTest, TestIpv6LocalAddressAndPort) {
  MockAuthorizationEndpoint endpoint(
      "ipv6:[2001:0db8:85a3:0000:0000:8a2e:0370:7334]:456", "");
  EvaluateChannelArgs args(nullptr, &endpoint);
  EXPECT_EQ(args.GetLocalAddress(), "2001:0db8:85a3:0000:0000:8a2e:0370:7334");
  EXPECT_EQ(args.GetLocalPort(), 456);
}

TEST(EvaluateChannelArgsTest, TestIpv6PeerAddressAndPort) {
  MockAuthorizationEndpoint endpoint("", "ipv6:[2001:db8::1]:654");
  EvaluateChannelArgs args(nullptr, &endpoint);
  EXPECT_EQ(args.GetPeerAddress(), "2001:db8::1");
  EXPECT_EQ(args.GetPeerPort(), 654);
}

TEST(EvaluateChannelArgsTest, HandlesNullAuthContext) {
  EvaluateChannelArgs args(nullptr, nullptr);
  EXPECT_TRUE(args.GetTransportSecurityType().empty());
  EXPECT_TRUE(args.GetSpiffeId().empty());
  EXPECT_TRUE(args.GetCommonName().empty());
}

TEST(EvaluateChannelArgsTest, HandlesEmptyAuthContext) {
  grpc_auth_context auth_context(nullptr);
  EvaluateChannelArgs args(&auth_context, nullptr);
  EXPECT_TRUE(args.GetTransportSecurityType().empty());
  EXPECT_TRUE(args.GetSpiffeId().empty());
  EXPECT_TRUE(args.GetCommonName().empty());
}

TEST(EvaluateChannelArgsTest, TransportSecurityTypeSuccessOneProperty) {
  grpc_auth_context auth_context(nullptr);
  const char* kType = "ssl";
  auth_context.add_cstring_property(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                    kType);
  EvaluateChannelArgs args(&auth_context, nullptr);
  EXPECT_EQ(args.GetTransportSecurityType(), kType);
}

TEST(EvaluateChannelArgsTest, TransportSecurityTypeFailDuplicateProperty) {
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                    "type1");
  auth_context.add_cstring_property(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                    "type2");
  EvaluateChannelArgs args(&auth_context, nullptr);
  EXPECT_TRUE(args.GetTransportSecurityType().empty());
}

TEST(EvaluateChannelArgsTest, SpiffeIdSuccessOneProperty) {
  grpc_auth_context auth_context(nullptr);
  const char* kId = "spiffeid";
  auth_context.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, kId);
  EvaluateChannelArgs args(&auth_context, nullptr);
  EXPECT_EQ(args.GetSpiffeId(), kId);
}

TEST(EvaluateChannelArgsTest, SpiffeIdFailDuplicateProperty) {
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id1");
  auth_context.add_cstring_property(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id2");
  EvaluateChannelArgs args(&auth_context, nullptr);
  EXPECT_TRUE(args.GetSpiffeId().empty());
}

TEST(EvaluateChannelArgsTest, CommonNameSuccessOneProperty) {
  grpc_auth_context auth_context(nullptr);
  const char* kServer = "server";
  auth_context.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, kServer);
  EvaluateChannelArgs args(&auth_context, nullptr);
  EXPECT_EQ(args.GetCommonName(), kServer);
}

TEST(EvaluateChannelArgsTest, CommonNameFailDuplicateProperty) {
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, "server1");
  auth_context.add_cstring_property(GRPC_X509_CN_PROPERTY_NAME, "server2");
  EvaluateChannelArgs args(&auth_context, nullptr);
  EXPECT_TRUE(args.GetCommonName().empty());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
