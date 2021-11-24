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

#include "src/core/lib/security/authorization/evaluate_args.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "test/core/util/evaluate_args_test_util.h"
#include "test/core/util/test_config.h"

namespace grpc_core {

class EvaluateArgsTest : public ::testing::Test {
 protected:
  EvaluateArgsTestUtil util_;
};

TEST_F(EvaluateArgsTest, EmptyMetadata) {
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_EQ(args.GetPath(), nullptr);
  EXPECT_EQ(args.GetMethod(), nullptr);
  EXPECT_EQ(args.GetHost(), nullptr);
  EXPECT_THAT(args.GetHeaders(), ::testing::ElementsAre());
  EXPECT_EQ(args.GetHeaderValue("some_key", nullptr), absl::nullopt);
}

TEST_F(EvaluateArgsTest, GetPathSuccess) {
  util_.AddPairToMetadata(":path", "/expected/path");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_EQ(args.GetPath(), "/expected/path");
}

TEST_F(EvaluateArgsTest, GetHostSuccess) {
  util_.AddPairToMetadata("host", "host123");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_EQ(args.GetHost(), "host123");
}

TEST_F(EvaluateArgsTest, GetMethodSuccess) {
  util_.AddPairToMetadata(":method", "GET");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_EQ(args.GetMethod(), "GET");
}

TEST_F(EvaluateArgsTest, GetHeadersSuccess) {
  util_.AddPairToMetadata("host", "host123");
  util_.AddPairToMetadata(":path", "/expected/path");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_THAT(args.GetHeaders(),
              ::testing::UnorderedElementsAre(
                  ::testing::Pair("host", "host123"),
                  ::testing::Pair(":path", "/expected/path")));
}

TEST_F(EvaluateArgsTest, GetHeaderValueSuccess) {
  util_.AddPairToMetadata("key123", "value123");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  std::string concatenated_value;
  absl::optional<absl::string_view> value =
      args.GetHeaderValue("key123", &concatenated_value);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), "value123");
}

TEST_F(EvaluateArgsTest, TestLocalAddressAndPort) {
  util_.SetLocalEndpoint("ipv6:[2001:0db8:85a3:0000:0000:8a2e:0370:7334]:456");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  grpc_resolved_address local_address = args.GetLocalAddress();
  EXPECT_EQ(grpc_sockaddr_to_uri(&local_address),
            "ipv6:[2001:db8:85a3::8a2e:370:7334]:456");
  EXPECT_EQ(args.GetLocalAddressString(),
            "2001:0db8:85a3:0000:0000:8a2e:0370:7334");
  EXPECT_EQ(args.GetLocalPort(), 456);
}

TEST_F(EvaluateArgsTest, TestPeerAddressAndPort) {
  util_.SetPeerEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  grpc_resolved_address peer_address = args.GetPeerAddress();
  EXPECT_EQ(grpc_sockaddr_to_uri(&peer_address), "ipv4:255.255.255.255:123");
  EXPECT_EQ(args.GetPeerAddressString(), "255.255.255.255");
  EXPECT_EQ(args.GetPeerPort(), 123);
}

TEST_F(EvaluateArgsTest, EmptyAuthContext) {
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_TRUE(args.GetTransportSecurityType().empty());
  EXPECT_TRUE(args.GetSpiffeId().empty());
  EXPECT_TRUE(args.GetUriSans().empty());
  EXPECT_TRUE(args.GetDnsSans().empty());
  EXPECT_TRUE(args.GetCommonName().empty());
}

TEST_F(EvaluateArgsTest, GetTransportSecurityTypeSuccessOneProperty) {
  util_.AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                 "ssl");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_EQ(args.GetTransportSecurityType(), "ssl");
}

TEST_F(EvaluateArgsTest, GetTransportSecurityTypeFailDuplicateProperty) {
  util_.AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                 "type1");
  util_.AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                 "type2");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_TRUE(args.GetTransportSecurityType().empty());
}

TEST_F(EvaluateArgsTest, GetSpiffeIdSuccessOneProperty) {
  util_.AddPropertyToAuthContext(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id123");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_EQ(args.GetSpiffeId(), "id123");
}

TEST_F(EvaluateArgsTest, GetSpiffeIdFailDuplicateProperty) {
  util_.AddPropertyToAuthContext(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id123");
  util_.AddPropertyToAuthContext(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, "id456");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_TRUE(args.GetSpiffeId().empty());
}

TEST_F(EvaluateArgsTest, GetUriSanSuccessMultipleProperties) {
  util_.AddPropertyToAuthContext(GRPC_PEER_URI_PROPERTY_NAME, "foo");
  util_.AddPropertyToAuthContext(GRPC_PEER_URI_PROPERTY_NAME, "bar");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_THAT(args.GetUriSans(), ::testing::ElementsAre("foo", "bar"));
}

TEST_F(EvaluateArgsTest, GetDnsSanSuccessMultipleProperties) {
  util_.AddPropertyToAuthContext(GRPC_PEER_DNS_PROPERTY_NAME, "foo");
  util_.AddPropertyToAuthContext(GRPC_PEER_DNS_PROPERTY_NAME, "bar");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_THAT(args.GetDnsSans(), ::testing::ElementsAre("foo", "bar"));
}

TEST_F(EvaluateArgsTest, GetCommonNameSuccessOneProperty) {
  util_.AddPropertyToAuthContext(GRPC_X509_CN_PROPERTY_NAME, "server123");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_EQ(args.GetCommonName(), "server123");
}

TEST_F(EvaluateArgsTest, GetCommonNameFailDuplicateProperty) {
  util_.AddPropertyToAuthContext(GRPC_X509_CN_PROPERTY_NAME, "server123");
  util_.AddPropertyToAuthContext(GRPC_X509_CN_PROPERTY_NAME, "server456");
  EvaluateArgs args = util_.MakeEvaluateArgs();
  EXPECT_TRUE(args.GetCommonName().empty());
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
