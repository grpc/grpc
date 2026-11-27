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

#include <grpc/grpc.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/status.h>
#include <grpc/support/time.h>

#include <string>
#include <vector>

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v3/attribute_context.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"
#include "google/rpc/status.pb.h"
#include "src/core/call/evaluate_args.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/filter/ext_authz/ext_authz_messages.h"
#include "src/core/handshaker/endpoint_info/endpoint_info_handshaker.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/transport/auth_context.h"
#include "src/core/util/matchers.h"
#include "src/core/util/time.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

using ::envoy::config::core::v3::HeaderValueOption;
using ::envoy::service::auth::v3::CheckRequest;
using ::envoy::service::auth::v3::CheckResponse;

constexpr absl::string_view kPath = "/test.service/TestMethod";
constexpr absl::string_view kPathHeader = ":path";
constexpr absl::string_view kKey1 = "key1";
constexpr absl::string_view kVal1 = "val1";
constexpr absl::string_view kKey2 = "key2";
constexpr absl::string_view kVal2 = "val2";
constexpr absl::string_view kKey3 = "key3";
constexpr absl::string_view kVal3 = "val3";
constexpr absl::string_view kVal4 = "val4";
constexpr absl::string_view kCustomBinKey = "custom-bin";
constexpr absl::string_view kCustomBinVal{"\x00\x01\x02\xFF", 4};
constexpr absl::string_view kCustomBinValBase64 = "AAEC/w==";
constexpr absl::string_view kAllowMe = "allow-me";
constexpr absl::string_view kOther = "other";
constexpr absl::string_view kAllowPrefixFoo = "allow-prefix-foo";
constexpr absl::string_view kAllowPrefixDeny = "allow-prefix-deny";
constexpr absl::string_view kAllowPrefix = "allow-prefix-";
constexpr absl::string_view kMethodPost = "POST";
constexpr absl::string_view kProtocolHttp2 = "HTTP/2";
constexpr char kUriSan[] = "spiffe://foo.com/bar/baz";
constexpr char kDnsSan[] = "client.example.com";
constexpr char kSubject[] = "CN=client,O=Example,C=US";
constexpr char kLocalPrincipal[] = "spiffe://foo.com/server";
constexpr char kPeerAddressUri[] = "ipv4:192.168.1.100:54321";
constexpr absl::string_view kPeerAddressHost = "192.168.1.100";
constexpr int kPeerAddressPort = 54321;
constexpr char kLocalAddressUri[] = "ipv4:10.0.0.1:443";
constexpr absl::string_view kLocalAddressHost = "10.0.0.1";
constexpr int kLocalAddressPort = 443;
constexpr char kIpv6PeerAddressUri[] = "ipv6:[2001:db8::1]:54321";
constexpr absl::string_view kIpv6PeerAddressHost = "2001:db8::1";
constexpr int kIpv6PeerAddressPort = 54321;
constexpr char kIpv6LocalAddressUri[] = "ipv6:[::1]:443";
constexpr absl::string_view kIpv6LocalAddressHost = "::1";
constexpr int kIpv6LocalAddressPort = 443;
constexpr absl::string_view kEncodedCert =
    "-----BEGIN%20CERTIFICATE-----%0Aabc%3D%0A-----END%20CERTIFICATE-----%0A";

//
// CreateExtAuthzRequest() tests
//

// Per gRFC A102, when writing a HeaderValue, the raw_value field is always
// used and the value field never is.
MATCHER_P2(IsHeaderValue, key_matcher, raw_value_matcher, "") {
  return ::testing::ExplainMatchResult(key_matcher, arg.key(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(raw_value_matcher, arg.raw_value(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(::testing::IsEmpty(), arg.value(),
                                       result_listener);
}

MATCHER_P2(IsTcpSocketAddress, address_matcher, port_matcher, "") {
  return ::testing::ExplainMatchResult(
             ::testing::IsTrue(), arg.has_socket_address(), result_listener) &&
         ::testing::ExplainMatchResult(
             ::testing::Eq(envoy::config::core::v3::SocketAddress::TCP),
             arg.socket_address().protocol(), result_listener) &&
         ::testing::ExplainMatchResult(address_matcher,
                                       arg.socket_address().address(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(
             port_matcher, arg.socket_address().port_value(), result_listener);
}

class CreateExtAuthzRequestTest : public ::testing::Test {
 protected:
  CheckRequest ParseRequest(const std::string& serialized) {
    CheckRequest parsed;
    EXPECT_TRUE(parsed.ParseFromString(serialized));
    return parsed;
  }
};

//
// Server request tests
//

// On the server side, source and destination are omitted when no EvaluateArgs
// is supplied.
TEST_F(CreateExtAuthzRequestTest, WithoutEvaluateArgs) {
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  EXPECT_TRUE(request.has_attributes());
  const auto& attr = request.attributes();
  EXPECT_TRUE(attr.has_request());
  EXPECT_FALSE(attr.has_source());
  EXPECT_FALSE(attr.has_destination());
}

TEST_F(CreateExtAuthzRequestTest, SourceAndDestination) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, kUriSan);
  auth_context.add_cstring_property(GRPC_PEER_DNS_PROPERTY_NAME, kDnsSan);
  auth_context.add_cstring_property(GRPC_X509_SUBJECT_PROPERTY_NAME, kSubject);
  auth_context.add_cstring_property(GRPC_X509_LOCAL_PRINCIPAL_PROPERTY_NAME,
                                    kLocalPrincipal);
  EvaluateArgs::PerChannelArgs channel_args(
      &auth_context,
      ChannelArgs()
          .Set(GRPC_ARG_ENDPOINT_PEER_ADDRESS, kPeerAddressUri)
          .Set(GRPC_ARG_ENDPOINT_LOCAL_ADDRESS, kLocalAddressUri));
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& attr = request.attributes();
  EXPECT_THAT(attr.source().address(),
              IsTcpSocketAddress(kPeerAddressHost, kPeerAddressPort));
  // URI SAN takes precedence over DNS SAN and subject.
  EXPECT_THAT(attr.source().principal(), ::testing::StrEq(kUriSan));
  // Not requested, so not included.
  EXPECT_THAT(attr.source().certificate(), ::testing::IsEmpty());
  EXPECT_THAT(attr.destination().address(),
              IsTcpSocketAddress(kLocalAddressHost, kLocalAddressPort));
  // The destination principal comes from this endpoint's own certificate,
  // and is never the peer's identity.
  EXPECT_THAT(attr.destination().principal(),
              ::testing::StrEq(kLocalPrincipal));
}

// IPv6 addresses are reported without the surrounding brackets.
TEST_F(CreateExtAuthzRequestTest, Ipv6Address) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  EvaluateArgs::PerChannelArgs channel_args(
      &auth_context,
      ChannelArgs()
          .Set(GRPC_ARG_ENDPOINT_PEER_ADDRESS, kIpv6PeerAddressUri)
          .Set(GRPC_ARG_ENDPOINT_LOCAL_ADDRESS, kIpv6LocalAddressUri));
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& attr = request.attributes();
  EXPECT_THAT(attr.source().address(),
              IsTcpSocketAddress(kIpv6PeerAddressHost, kIpv6PeerAddressPort));
  EXPECT_THAT(attr.destination().address(),
              IsTcpSocketAddress(kIpv6LocalAddressHost, kIpv6LocalAddressPort));
}

// Non-IP addresses (such as Unix domain sockets) are omitted rather than
// populated as a SocketAddress.
TEST_F(CreateExtAuthzRequestTest, UnixDomainSocketAddressOmitted) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  EvaluateArgs::PerChannelArgs channel_args(
      &auth_context,
      ChannelArgs()
          .Set(GRPC_ARG_ENDPOINT_PEER_ADDRESS, "unix:/tmp/peer.sock")
          .Set(GRPC_ARG_ENDPOINT_LOCAL_ADDRESS, "unix:/tmp/local.sock"));
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& attr = request.attributes();
  EXPECT_TRUE(attr.has_source());
  EXPECT_FALSE(attr.source().has_address());
  EXPECT_TRUE(attr.has_destination());
  EXPECT_FALSE(attr.destination().has_address());
}

// The two sides are resolved independently: an unknown local address must not
// suppress the source address (and vice versa; see LocalAddressOnly).
TEST_F(CreateExtAuthzRequestTest, PeerAddressOnly) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  EvaluateArgs::PerChannelArgs channel_args(
      &auth_context,
      ChannelArgs().Set(GRPC_ARG_ENDPOINT_PEER_ADDRESS, kPeerAddressUri));
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& attr = request.attributes();
  EXPECT_THAT(attr.source().address(),
              IsTcpSocketAddress(kPeerAddressHost, kPeerAddressPort));
  EXPECT_TRUE(attr.has_destination());
  EXPECT_FALSE(attr.destination().has_address());
}

// An unknown peer address must not suppress the destination address.
TEST_F(CreateExtAuthzRequestTest, LocalAddressOnly) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  EvaluateArgs::PerChannelArgs channel_args(
      &auth_context,
      ChannelArgs().Set(GRPC_ARG_ENDPOINT_LOCAL_ADDRESS, kLocalAddressUri));
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& attr = request.attributes();
  EXPECT_TRUE(attr.has_source());
  EXPECT_FALSE(attr.source().has_address());
  EXPECT_THAT(attr.destination().address(),
              IsTcpSocketAddress(kLocalAddressHost, kLocalAddressPort));
}

TEST_F(CreateExtAuthzRequestTest, PrincipalFallbackToDnsSan) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_PEER_DNS_PROPERTY_NAME, kDnsSan);
  auth_context.add_cstring_property(GRPC_X509_SUBJECT_PROPERTY_NAME, kSubject);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  EXPECT_THAT(request.attributes().source().principal(),
              ::testing::StrEq(kDnsSan));
}

TEST_F(CreateExtAuthzRequestTest, PrincipalFallbackToSubject) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_X509_SUBJECT_PROPERTY_NAME, kSubject);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  EXPECT_THAT(request.attributes().source().principal(),
              ::testing::StrEq(kSubject));
}

// Empty SAN entries are skipped rather than being reported as the principal,
// whether they come before or after the first usable one.
TEST_F(CreateExtAuthzRequestTest, PrincipalSkipsEmptyUriSan) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, "");
  auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, kUriSan);
  auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, "");
  auth_context.add_cstring_property(GRPC_PEER_DNS_PROPERTY_NAME, kDnsSan);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  EXPECT_THAT(request.attributes().source().principal(),
              ::testing::StrEq(kUriSan));
}

// When multiple non-empty URI SANs are present, the first one is used.
TEST_F(CreateExtAuthzRequestTest, PrincipalMultipleUriSansUsesFirst) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, kUriSan);
  auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME,
                                    "spiffe://other.com/bar/baz");
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  EXPECT_THAT(request.attributes().source().principal(),
              ::testing::StrEq(kUriSan));
}

// Empty DNS SAN entries are skipped in favor of the first non-empty DNS SAN.
TEST_F(CreateExtAuthzRequestTest, PrincipalSkipsEmptyDnsSan) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_PEER_DNS_PROPERTY_NAME, "");
  auth_context.add_cstring_property(GRPC_PEER_DNS_PROPERTY_NAME, kDnsSan);
  auth_context.add_cstring_property(GRPC_X509_SUBJECT_PROPERTY_NAME, kSubject);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  EXPECT_THAT(request.attributes().source().principal(),
              ::testing::StrEq(kDnsSan));
}

// If every URI and DNS SAN is empty, we fall all the way through to the
// subject.
TEST_F(CreateExtAuthzRequestTest, PrincipalSkipsAllEmptySans) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, "");
  auth_context.add_cstring_property(GRPC_PEER_DNS_PROPERTY_NAME, "");
  auth_context.add_cstring_property(GRPC_X509_SUBJECT_PROPERTY_NAME, kSubject);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  EXPECT_THAT(request.attributes().source().principal(),
              ::testing::StrEq(kSubject));
}

// The peer's identity must never leak into destination.principal, and this
// endpoint's identity must never leak into source.principal.
TEST_F(CreateExtAuthzRequestTest, PrincipalsNotInterchanged) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, kUriSan);
  auth_context.add_cstring_property(GRPC_PEER_DNS_PROPERTY_NAME, kDnsSan);
  auth_context.add_cstring_property(GRPC_X509_SUBJECT_PROPERTY_NAME, kSubject);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& attr = request.attributes();
  EXPECT_THAT(attr.source().principal(), ::testing::StrEq(kUriSan));
  // Only peer properties are set, so there is no local identity to report.
  EXPECT_TRUE(attr.has_destination());
  EXPECT_THAT(attr.destination().principal(), ::testing::IsEmpty());
}

TEST_F(CreateExtAuthzRequestTest, SourcePrincipalIgnoresLocalPrincipal) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_X509_LOCAL_PRINCIPAL_PROPERTY_NAME,
                                    kLocalPrincipal);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& attr = request.attributes();
  EXPECT_TRUE(attr.has_source());
  EXPECT_THAT(attr.source().principal(), ::testing::IsEmpty());
  EXPECT_THAT(attr.destination().principal(),
              ::testing::StrEq(kLocalPrincipal));
}

// With an empty auth context and no endpoint addresses in the channel args,
// source and destination are still present (because EvaluateArgs is supplied),
// but have neither an address nor a principal.
TEST_F(CreateExtAuthzRequestTest, WithoutConnectionAttributes) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& attr = request.attributes();
  EXPECT_TRUE(attr.has_source());
  EXPECT_FALSE(attr.source().has_address());
  EXPECT_THAT(attr.source().principal(), ::testing::IsEmpty());
  EXPECT_TRUE(attr.has_destination());
  EXPECT_FALSE(attr.destination().has_address());
  EXPECT_THAT(attr.destination().principal(), ::testing::IsEmpty());
}

TEST_F(CreateExtAuthzRequestTest, WithPeerCertificate) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  params.peer_certificate = kEncodedCert;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  EXPECT_THAT(request.attributes().source().certificate(),
              ::testing::StrEq(kEncodedCert));
}

//
// Client request tests
//

// On the client side, source and destination are omitted and the peer
// certificate is ignored, even if connection attributes are available.
TEST_F(CreateExtAuthzRequestTest, OmitsSourceAndDestination) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, kUriSan);
  auth_context.add_cstring_property(GRPC_X509_LOCAL_PRINCIPAL_PROPERTY_NAME,
                                    kLocalPrincipal);
  EvaluateArgs::PerChannelArgs channel_args(
      &auth_context,
      ChannelArgs()
          .Set(GRPC_ARG_ENDPOINT_PEER_ADDRESS, kPeerAddressUri)
          .Set(GRPC_ARG_ENDPOINT_LOCAL_ADDRESS, kLocalAddressUri));
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.args = &args;
  params.peer_certificate = kEncodedCert;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  EXPECT_TRUE(request.has_attributes());
  const auto& attr = request.attributes();
  EXPECT_TRUE(attr.has_request());
  EXPECT_FALSE(attr.has_source());
  EXPECT_FALSE(attr.has_destination());
}

TEST_F(CreateExtAuthzRequestTest, HttpFields) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& http = request.attributes().request().http();
  EXPECT_THAT(http.method(), ::testing::StrEq(kMethodPost));
  EXPECT_THAT(http.path(), ::testing::StrEq(kPath));
  EXPECT_THAT(http.protocol(), ::testing::StrEq(kProtocolHttp2));
  EXPECT_THAT(http.size(), ::testing::Eq(-1));
  EXPECT_TRUE(http.has_header_map());
  EXPECT_THAT(http.header_map().headers(), ::testing::IsEmpty());
}

// The start time is far enough in the past that the test cannot pass by
// falling back to the current time.
TEST_F(CreateExtAuthzRequestTest, ExplicitStartTime) {
  const Timestamp start_time = Timestamp::Now() - Duration::Hours(1);
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.start_time = start_time;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  EXPECT_TRUE(request.attributes().request().has_time());
  // Converting to the realtime clock is not exact, so allow a small tolerance.
  EXPECT_NEAR(request.attributes().request().time().seconds(),
              start_time.as_timespec(GPR_CLOCK_REALTIME).tv_sec, 1);
}

// Without an explicit start time, the time at which the request is created is
// used.
TEST_F(CreateExtAuthzRequestTest, DefaultStartTime) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  const gpr_timespec before = gpr_now(GPR_CLOCK_REALTIME);
  auto serialized = CreateExtAuthzRequest(params);
  const gpr_timespec after = gpr_now(GPR_CLOCK_REALTIME);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  EXPECT_TRUE(request.attributes().request().has_time());
  EXPECT_GE(request.attributes().request().time().seconds(), before.tv_sec);
  EXPECT_LE(request.attributes().request().time().seconds(), after.tv_sec);
}

// kAllowPrefixDeny is matched by an allowed matcher, so it is excluded only
// because it is also disallowed. kOther is not disallowed, so it is excluded
// because no allowed matcher matches it.
TEST_F(CreateExtAuthzRequestTest, RequestHeadersBothAllowedAndDisallowedSet) {
  grpc_metadata_batch batch;
  batch.Append(kAllowMe, Slice::FromStaticString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kAllowPrefixDeny, Slice::FromStaticString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kOther, Slice::FromStaticString(kVal3),
               [](absl::string_view, const Slice&) {});
  batch.Append(kAllowPrefixFoo, Slice::FromStaticString(kVal4),
               [](absl::string_view, const Slice&) {});
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.metadata = &batch;
  const std::vector<StringMatcher> disallowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, kAllowPrefixDeny,
                            /*case_sensitive=*/false)
          .value(),
  };
  params.disallowed_headers = disallowed_headers;
  const std::vector<StringMatcher> allowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, kAllowMe,
                            /*case_sensitive=*/false)
          .value(),
      StringMatcher::Create(StringMatcher::Type::kPrefix, kAllowPrefix,
                            /*case_sensitive=*/false)
          .value(),
  };
  params.allowed_headers = allowed_headers;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& http = request.attributes().request().http();
  EXPECT_THAT(http.header_map().headers(),
              ::testing::ElementsAre(IsHeaderValue(kAllowMe, kVal1),
                                     IsHeaderValue(kAllowPrefixFoo, kVal4)));
}

TEST_F(CreateExtAuthzRequestTest, RequestHeadersOnlyAllowedSet) {
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromStaticString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromStaticString(kVal2),
               [](absl::string_view, const Slice&) {});
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.metadata = &batch;
  const std::vector<StringMatcher> allowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey1,
                            /*case_sensitive=*/false)
          .value(),
  };
  params.allowed_headers = allowed_headers;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& http = request.attributes().request().http();
  EXPECT_THAT(http.header_map().headers(),
              ::testing::ElementsAre(IsHeaderValue(kKey1, kVal1)));
}

TEST_F(CreateExtAuthzRequestTest, RequestHeadersOnlyDisallowedSet) {
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromStaticString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromStaticString(kVal2),
               [](absl::string_view, const Slice&) {});
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.metadata = &batch;
  const std::vector<StringMatcher> disallowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey1,
                            /*case_sensitive=*/false)
          .value(),
  };
  params.disallowed_headers = disallowed_headers;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& http = request.attributes().request().http();
  EXPECT_THAT(http.header_map().headers(),
              ::testing::ElementsAre(IsHeaderValue(kKey2, kVal2)));
}

TEST_F(CreateExtAuthzRequestTest,
       RequestHeadersNeitherAllowedNorDisallowedSet) {
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromStaticString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromStaticString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kCustomBinKey, Slice::FromStaticString(kCustomBinVal),
               [](absl::string_view, const Slice&) {});
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.metadata = &batch;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& http = request.attributes().request().http();
  // Per gRFC A102, the values of binary headers are base64-encoded.
  EXPECT_THAT(http.header_map().headers(),
              ::testing::ElementsAre(
                  IsHeaderValue(kKey1, kVal1), IsHeaderValue(kKey2, kVal2),
                  IsHeaderValue(kCustomBinKey, kCustomBinValBase64)));
}

// Known headers, such as :path, are included in header_map along with
// unknown ones. The metadata batch encodes known headers first, followed by
// unknown headers in insertion order.
TEST_F(CreateExtAuthzRequestTest, RequestHeadersPseudoHeaders) {
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromStaticString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromStaticString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Set(HttpPathMetadata(), Slice::FromStaticString(kPath));
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.metadata = &batch;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& http = request.attributes().request().http();
  EXPECT_THAT(http.header_map().headers(),
              ::testing::ElementsAre(IsHeaderValue(kPathHeader, kPath),
                                     IsHeaderValue(kKey1, kVal1),
                                     IsHeaderValue(kKey2, kVal2)));
}

// Each value of a repeated header becomes a separate HeaderValue entry, in
// order, rather than being joined into a single value.
TEST_F(CreateExtAuthzRequestTest, RequestHeadersRepeatedKeys) {
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromStaticString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey1, Slice::FromStaticString(kVal2),
               [](absl::string_view, const Slice&) {});
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.metadata = &batch;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);
  const auto& http = request.attributes().request().http();
  EXPECT_THAT(http.header_map().headers(),
              ::testing::ElementsAre(IsHeaderValue(kKey1, kVal1),
                                     IsHeaderValue(kKey1, kVal2)));
}

//
// GetUrlEncodedPemPeerCertificate() tests
//

TEST(GetUrlEncodedPemPeerCertificateTest, NoAuthContext) {
  EXPECT_THAT(GetUrlEncodedPemPeerCertificate(nullptr), ::testing::IsEmpty());
}

TEST(GetUrlEncodedPemPeerCertificateTest, NoPeerCertificate) {
  grpc_auth_context auth_context(/*chained=*/nullptr);
  EXPECT_THAT(GetUrlEncodedPemPeerCertificate(&auth_context),
              ::testing::IsEmpty());
}

// Matches Envoy's Http::Utility::PercentEncoding::urlEncode(): every
// character other than ALPHA, DIGIT, '*', '-', '.' and '_' is percent-encoded
// with uppercase hex digits.
TEST(GetUrlEncodedPemPeerCertificateTest, ValidPeerCertificate) {
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_X509_PEM_CERT_PROPERTY_NAME,
                                    "-----BEGIN CERTIFICATE-----\n"
                                    "a+b/c=\n"
                                    "-----END CERTIFICATE-----\n");
  EXPECT_THAT(GetUrlEncodedPemPeerCertificate(&auth_context),
              ::testing::StrEq("-----BEGIN%20CERTIFICATE-----%0A"
                               "a%2Bb%2Fc%3D%0A"
                               "-----END%20CERTIFICATE-----%0A"));
}

// Characters outside of Envoy's unreserved set are encoded too, including
// carriage returns and characters that never appear in a well-formed PEM.
TEST(GetUrlEncodedPemPeerCertificateTest, EncodesUnusualCharacters) {
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_X509_PEM_CERT_PROPERTY_NAME,
                                    "aZ9*-._\r\n~:%");
  EXPECT_THAT(GetUrlEncodedPemPeerCertificate(&auth_context),
              ::testing::StrEq("aZ9*-._%0D%0A%7E%3A%25"));
}

// An empty PEM property is treated the same as no peer certificate.
TEST(GetUrlEncodedPemPeerCertificateTest, EmptyPeerCertificate) {
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_X509_PEM_CERT_PROPERTY_NAME, "");
  EXPECT_THAT(GetUrlEncodedPemPeerCertificate(&auth_context),
              ::testing::IsEmpty());
}

// Bytes outside of the ASCII range are encoded as unsigned values.
TEST(GetUrlEncodedPemPeerCertificateTest, NonAsciiBytes) {
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_property(GRPC_X509_PEM_CERT_PROPERTY_NAME, "\x80\xff", 2);
  EXPECT_THAT(GetUrlEncodedPemPeerCertificate(&auth_context),
              ::testing::StrEq("%80%FF"));
}

// Every character in Envoy's unreserved set is left as is, and a space is
// encoded as "%20" rather than "+".
TEST(GetUrlEncodedPemPeerCertificateTest, UnreservedCharactersAndSpace) {
  constexpr const char* kUnreserved =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789*-._";
  grpc_auth_context auth_context(/*chained=*/nullptr);
  auth_context.add_cstring_property(GRPC_X509_PEM_CERT_PROPERTY_NAME,
                                    kUnreserved);
  EXPECT_THAT(GetUrlEncodedPemPeerCertificate(&auth_context),
              ::testing::StrEq(kUnreserved));
  grpc_auth_context space_context(/*chained=*/nullptr);
  space_context.add_cstring_property(GRPC_X509_PEM_CERT_PROPERTY_NAME, "a b+c");
  EXPECT_THAT(GetUrlEncodedPemPeerCertificate(&space_context),
              ::testing::StrEq("a%20b%2Bc"));
}

//
// ExtAuthzResponse::Parse() tests
//

MATCHER_P3(IsHeaderValueOption, key, value, append_action, "") {
  return ::testing::ExplainMatchResult(::testing::Pair(key, value), arg.header,
                                       result_listener) &&
         ::testing::ExplainMatchResult(append_action, arg.append_action,
                                       result_listener);
}

MATCHER_P2(IsHeaderMutation, add_headers_matcher, remove_headers_matcher, "") {
  return ::testing::ExplainMatchResult(add_headers_matcher, arg.add_headers,
                                       result_listener) &&
         ::testing::ExplainMatchResult(remove_headers_matcher,
                                       arg.remove_headers, result_listener);
}

MATCHER_P2(IsOkResponse, header_mutation_matcher,
           response_headers_to_add_matcher, "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<ExtAuthzResponse::OkResponse>(::testing::AllOf(
          ::testing::Field(&ExtAuthzResponse::OkResponse::header_mutation,
                           header_mutation_matcher),
          ::testing::Field(
              &ExtAuthzResponse::OkResponse::response_headers_to_add,
              response_headers_to_add_matcher))),
      arg, result_listener);
}

MATCHER_P2(IsDeniedResponse, status_matcher, headers_matcher, "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<ExtAuthzResponse::DeniedResponse>(::testing::AllOf(
          ::testing::Field(&ExtAuthzResponse::DeniedResponse::status,
                           status_matcher),
          ::testing::Field(&ExtAuthzResponse::DeniedResponse::headers,
                           headers_matcher))),
      arg, result_listener);
}

constexpr absl::string_view kAllGood = "all good";
constexpr absl::string_view kRespHeaderKey = "x-resp-1";
constexpr absl::string_view kRespHeaderVal = "val-resp-1";
constexpr absl::string_view kDenyReasonHeaderKey = "x-deny-reason";
constexpr absl::string_view kDenyReasonHeaderVal = "bad-token";
constexpr absl::string_view kUnauthenticatedRpc = "unauthenticated rpc";

class ParseExtAuthzResponseTest : public ::testing::Test {
 protected:
  absl::StatusOr<ExtAuthzResponse> ParseResponse(
      const CheckResponse& response) {
    std::string serialized;
    EXPECT_TRUE(response.SerializeToString(&serialized));
    return ExtAuthzResponse::Parse(serialized);
  }

  static HeaderValueOption CreateHeaderValueOption(
      absl::string_view key, absl::string_view value,
      HeaderValueOption::HeaderAppendAction append_action =
          HeaderValueOption::APPEND_IF_EXISTS_OR_ADD) {
    HeaderValueOption header;
    header.mutable_header()->set_key(std::string(key));
    header.mutable_header()->set_value(std::string(value));
    header.set_append_action(append_action);
    return header;
  }

  static HeaderValueOption CreateRawHeaderValueOption(
      absl::string_view key, absl::string_view raw_value,
      HeaderValueOption::HeaderAppendAction append_action =
          HeaderValueOption::APPEND_IF_EXISTS_OR_ADD) {
    HeaderValueOption header;
    header.mutable_header()->set_key(std::string(key));
    header.mutable_header()->set_raw_value(std::string(raw_value));
    header.set_append_action(append_action);
    return header;
  }
};

TEST_F(ParseExtAuthzResponseTest, ResponseInvalid) {
  // Field 1 (length-delimited) with length 255, but no data.
  auto parsed = ExtAuthzResponse::Parse("\x0a\xff");
  EXPECT_EQ(parsed.status(),
            absl::InternalError("Failed to parse CheckResponse"));
}

// An empty CheckResponse has an unset status, which is treated as OK.
TEST_F(ParseExtAuthzResponseTest, ResponseEmpty) {
  auto parsed = ExtAuthzResponse::Parse("");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_OK));
  EXPECT_THAT(parsed->status_message, ::testing::IsEmpty());
  EXPECT_THAT(
      parsed->response,
      IsOkResponse(IsHeaderMutation(::testing::IsEmpty(), ::testing::IsEmpty()),
                   ::testing::IsEmpty()));
}

// The status field alone determines whether the RPC is allowed, so an unset
// status is OK even if denied_response is set.
TEST_F(ParseExtAuthzResponseTest, StatusMissing) {
  CheckResponse response;
  response.mutable_denied_response();
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_OK));
  EXPECT_THAT(
      parsed->response,
      IsOkResponse(IsHeaderMutation(::testing::IsEmpty(), ::testing::IsEmpty()),
                   ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest, StatusInvalidCode) {
  CheckResponse response;
  // Not a valid gRPC status code.
  response.mutable_status()->set_code(99);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_UNKNOWN));
  EXPECT_THAT(parsed->response,
              IsDeniedResponse(::testing::Eq(GRPC_STATUS_PERMISSION_DENIED),
                               ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest, OkResponseNoHeaders) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_OK);
  response.mutable_ok_response();
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_OK));
  EXPECT_THAT(
      parsed->response,
      IsOkResponse(IsHeaderMutation(::testing::IsEmpty(), ::testing::IsEmpty()),
                   ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest, OkResponseHeaderMutations) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_OK);
  response.mutable_status()->set_message(std::string(kAllGood));
  auto* ok_response = response.mutable_ok_response();
  *ok_response->add_headers() = CreateHeaderValueOption(
      kKey1, kVal1, HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
  *ok_response->add_headers() = CreateRawHeaderValueOption(
      kKey2, kVal2, HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  ok_response->add_headers_to_remove(std::string(kKey3));
  *ok_response->add_response_headers_to_add() =
      CreateHeaderValueOption(kRespHeaderKey, kRespHeaderVal);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_OK));
  EXPECT_THAT(parsed->status_message, ::testing::StrEq(kAllGood));
  EXPECT_THAT(
      parsed->response,
      IsOkResponse(
          IsHeaderMutation(
              ::testing::ElementsAre(
                  IsHeaderValueOption(
                      kKey1, kVal1,
                      XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd),
                  IsHeaderValueOption(kKey2, kVal2,
                                      XdsHeaderValueOption::AppendAction::
                                          kOverwriteIfExistsOrAdd)),
              ::testing::ElementsAre(kKey3)),
          ::testing::ElementsAre(IsHeaderValueOption(
              kRespHeaderKey, kRespHeaderVal,
              XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd))));
}

TEST_F(ParseExtAuthzResponseTest, OkResponseHeaderInvalid) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_OK);
  *response.mutable_ok_response()->add_headers() =
      CreateHeaderValueOption("host", kVal1);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("Failed to parse XdsHeaderValueOption: "
                                "[field:header.key error:header \"host\" "
                                "not allowed]"));
}

TEST_F(ParseExtAuthzResponseTest, OkResponseHeaderToRemoveInvalid) {
  constexpr absl::string_view kInvalidHeaderKey = "invalid header key";
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_OK);
  response.mutable_ok_response()->add_headers_to_remove(
      std::string(kInvalidHeaderKey));
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InvalidArgumentError(
                "Invalid header name to remove: invalid header key"));
}

TEST_F(ParseExtAuthzResponseTest, OkResponseResponseHeaderToAddInvalid) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_OK);
  *response.mutable_ok_response()->add_response_headers_to_add() =
      CreateHeaderValueOption("host", kVal1);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("Failed to parse XdsHeaderValueOption: "
                                "[field:header.key error:header \"host\" "
                                "not allowed]"));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponseMissing) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_UNAUTHENTICATED);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_UNAUTHENTICATED));
  EXPECT_THAT(parsed->response,
              IsDeniedResponse(::testing::Eq(GRPC_STATUS_PERMISSION_DENIED),
                               ::testing::IsEmpty()));
}

// The status field alone determines whether the RPC is allowed, so an explicit
// OK status with denied_response set is still allowed and ignores
// denied_response.
TEST_F(ParseExtAuthzResponseTest, StatusOkWithDeniedResponse) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_OK);
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(envoy::type::v3::Unauthorized);
  *denied->add_headers() = CreateHeaderValueOption("grpc-status", "0");
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_OK));
  EXPECT_THAT(
      parsed->response,
      IsOkResponse(IsHeaderMutation(::testing::IsEmpty(), ::testing::IsEmpty()),
                   ::testing::IsEmpty()));
}

// The status field alone determines whether the RPC is denied, so a non-OK
// status with ok_response set is still denied.
TEST_F(ParseExtAuthzResponseTest, StatusNotOkWithOkResponse) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
  response.mutable_ok_response()->add_headers_to_remove(std::string(kKey1));
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->status_code,
              ::testing::Eq(GRPC_STATUS_PERMISSION_DENIED));
  EXPECT_THAT(parsed->response,
              IsDeniedResponse(::testing::Eq(GRPC_STATUS_PERMISSION_DENIED),
                               ::testing::IsEmpty()));
}

// Denied response headers are added to the trailers, where a grpc-* header
// from the authz server, such as grpc-status, must not be allowed.
TEST_F(ParseExtAuthzResponseTest, DeniedResponseHeaderInvalid) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
  *response.mutable_denied_response()->add_headers() =
      CreateHeaderValueOption("grpc-status", "0");
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("Failed to parse XdsHeaderValueOption: "
                                "[field:header.key error:header "
                                "\"grpc-status\" not allowed]"));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponse) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(envoy::type::v3::Unauthorized);
  *denied->add_headers() =
      CreateHeaderValueOption(kDenyReasonHeaderKey, kDenyReasonHeaderVal);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->status_code,
              ::testing::Eq(GRPC_STATUS_PERMISSION_DENIED));
  EXPECT_THAT(
      parsed->response,
      IsDeniedResponse(
          ::testing::Eq(GRPC_STATUS_UNAUTHENTICATED),
          ::testing::ElementsAre(IsHeaderValueOption(
              kDenyReasonHeaderKey, kDenyReasonHeaderVal,
              XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd))));
}

// A denied RPC must never be failed with OK, so HTTP 200 (which is not in the
// HTTP-to-gRPC status mapping) is converted to UNKNOWN.
TEST_F(ParseExtAuthzResponseTest, DeniedResponseHttpStatusOkMapsToUnknown) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(envoy::type::v3::OK);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->response,
              IsDeniedResponse(::testing::Eq(GRPC_STATUS_UNKNOWN),
                               ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponsePreservesStatus) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_UNAUTHENTICATED);
  response.mutable_status()->set_message(std::string(kUnauthenticatedRpc));
  response.mutable_denied_response();
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_UNAUTHENTICATED));
  EXPECT_THAT(parsed->status_message, ::testing::StrEq(kUnauthenticatedRpc));
  EXPECT_THAT(parsed->response,
              IsDeniedResponse(::testing::Eq(GRPC_STATUS_PERMISSION_DENIED),
                               ::testing::IsEmpty()));
}

// Removals of valid header names that gRPC never allows to be mutated, such
// as "host" and grpc-* headers, are not rejected by the parser; the filter
// handles them according to the header mutation rules.
TEST_F(ParseExtAuthzResponseTest, HeaderProtectedNamesToRemoveParsed) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_OK);
  response.mutable_ok_response()->add_headers_to_remove("host");
  response.mutable_ok_response()->add_headers_to_remove("grpc-timeout");
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->response,
              IsOkResponse(IsHeaderMutation(
                               ::testing::IsEmpty(),
                               ::testing::ElementsAre("host", "grpc-timeout")),
                           ::testing::IsEmpty()));
}

// gRFC A92 does not use error_response, so with an OK status it is ignored
// and the RPC is allowed with no mutations.
TEST_F(ParseExtAuthzResponseTest, ErrorResponseIgnoredWithOkStatus) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_OK);
  auto* error_response = response.mutable_error_response();
  error_response->mutable_status()->set_code(envoy::type::v3::Unauthorized);
  *error_response->add_headers() =
      CreateHeaderValueOption(kDenyReasonHeaderKey, kDenyReasonHeaderVal);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_OK));
  EXPECT_THAT(
      parsed->response,
      IsOkResponse(IsHeaderMutation(::testing::IsEmpty(), ::testing::IsEmpty()),
                   ::testing::IsEmpty()));
}

// gRFC A92 does not use error_response, so with a non-OK status it is
// ignored and the RPC is denied with the default status and no headers.
TEST_F(ParseExtAuthzResponseTest, ErrorResponseIgnoredWithNonOkStatus) {
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_UNAVAILABLE);
  auto* error_response = response.mutable_error_response();
  error_response->mutable_status()->set_code(envoy::type::v3::Unauthorized);
  *error_response->add_headers() =
      CreateHeaderValueOption(kDenyReasonHeaderKey, kDenyReasonHeaderVal);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_UNAVAILABLE));
  EXPECT_THAT(parsed->response,
              IsDeniedResponse(::testing::Eq(GRPC_STATUS_PERMISSION_DENIED),
                               ::testing::IsEmpty()));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
