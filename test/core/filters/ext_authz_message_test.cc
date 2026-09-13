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
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/status.h>

#include <optional>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v3/attribute_context.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"
#include "google/rpc/status.pb.h"
#include "src/core/call/evaluate_args.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/credentials/transport/tls/tls_utils.h"
#include "src/core/ext/filters/ext_authz/ext_authz_messages.h"
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
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

using ::envoy::config::core::v3::HeaderValueOption;
using ::envoy::service::auth::v3::AttributeContext;
using ::envoy::service::auth::v3::CheckRequest;
using ::envoy::service::auth::v3::CheckResponse;
using ::envoy::service::auth::v3::DeniedHttpResponse;
using ::envoy::service::auth::v3::OkHttpResponse;
using ::envoy::type::v3::HttpStatus;

constexpr absl::string_view kPath = "/test.service/TestMethod";
constexpr absl::string_view kPathHeader = ":path";
constexpr absl::string_view kKey1 = "key1";
constexpr absl::string_view kVal1 = "val1";
constexpr absl::string_view kKey2 = "key2";
constexpr absl::string_view kVal2 = "val2";
constexpr absl::string_view kKey3 = "key3";
constexpr absl::string_view kVal3 = "val3";
constexpr absl::string_view kVal4 = "val4";
constexpr absl::string_view kKeyPrefix = "key";
constexpr absl::string_view kCustomTextKey = "custom-text";
constexpr absl::string_view kCustomTextVal = "plain-text-value";
constexpr absl::string_view kCustomBinKey = "custom-bin";
constexpr absl::string_view kCustomBinVal{"\x00\x01\x02\xFF", 4};
constexpr absl::string_view kAllowMe = "allow-me";
constexpr absl::string_view kDenyMe = "deny-me";
constexpr absl::string_view kOther = "other";
constexpr absl::string_view kAllowPrefixFoo = "allow-prefix-foo";
constexpr absl::string_view kAllowPrefix = "allow-prefix-";
constexpr absl::string_view kInvalidHeaderKey = "invalid header key";
constexpr absl::string_view kInvalidHeaderKeyErrorMessage =
    "Invalid header name to remove: invalid header key";
constexpr absl::string_view kMethodPost = "POST";
constexpr absl::string_view kProtocolHttp2 = "HTTP/2";
constexpr char kUriSan[] = "spiffe://foo.com/bar/baz";
constexpr char kDnsSan[] = "client.example.com";
constexpr char kSubject[] = "CN=client,O=Example,C=US";
constexpr char kLocalUriSan[] = "spiffe://foo.com/server";
constexpr char kLocalDnsSan[] = "server.example.com";
constexpr char kLocalSubject[] = "CN=server,O=Example,C=US";
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
constexpr char kUnixAddressUri[] = "unix:/tmp/grpc-ext-authz-test.sock";
constexpr absl::string_view kEncodedCert =
    "-----BEGIN%20CERTIFICATE-----%0Aabc%3D%0A-----END%20CERTIFICATE-----%0A";

//
// CreateExtAuthzRequest() tests
//

MATCHER_P2(IsHeaderValue, key_matcher, value_matcher, "") {
  return ::testing::ExplainMatchResult(key_matcher, arg.key(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(value_matcher, arg.value(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(::testing::IsEmpty(), arg.raw_value(),
                                       result_listener);
}

MATCHER_P2(IsRawHeaderValue, key_matcher, raw_value_matcher, "") {
  return ::testing::ExplainMatchResult(key_matcher, arg.key(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(raw_value_matcher, arg.raw_value(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(::testing::IsEmpty(), arg.value(),
                                       result_listener);
}

class CreateExtAuthzRequestTest : public ::testing::Test {
 protected:
  CheckRequest ParseRequest(const std::string& serialized) {
    CheckRequest parsed;
    EXPECT_TRUE(parsed.ParseFromString(serialized));
    return parsed;
  }
};

TEST_F(CreateExtAuthzRequestTest, ClientRequestAttributes) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  const auto& attr = request.attributes();
  EXPECT_TRUE(attr.has_request());
  EXPECT_FALSE(attr.has_source());
  EXPECT_FALSE(attr.has_destination());
}

// Even on the server side, source and destination are omitted if no
// connection attributes are available.
TEST_F(CreateExtAuthzRequestTest, ServerRequestWithoutEvaluateArgs) {
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  const auto& attr = request.attributes();
  EXPECT_TRUE(attr.has_request());
  EXPECT_FALSE(attr.has_source());
  EXPECT_FALSE(attr.has_destination());
}

TEST_F(CreateExtAuthzRequestTest, ServerRequestSourceAndDestination) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, kUriSan);
  auth_context.add_cstring_property(GRPC_PEER_DNS_PROPERTY_NAME, kDnsSan);
  auth_context.add_cstring_property(GRPC_X509_SUBJECT_PROPERTY_NAME, kSubject);
  auth_context.add_cstring_property(GRPC_X509_LOCAL_URI_PROPERTY_NAME,
                                    kLocalUriSan);
  auth_context.add_cstring_property(GRPC_X509_LOCAL_DNS_PROPERTY_NAME,
                                    kLocalDnsSan);
  auth_context.add_cstring_property(GRPC_X509_LOCAL_SUBJECT_PROPERTY_NAME,
                                    kLocalSubject);
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
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.has_source());
  ASSERT_TRUE(attr.source().has_address());
  ASSERT_TRUE(attr.source().address().has_socket_address());
  EXPECT_THAT(attr.source().address().socket_address().address(),
              ::testing::StrEq(kPeerAddressHost));
  EXPECT_EQ(attr.source().address().socket_address().port_value(),
            kPeerAddressPort);
  // URI SAN takes precedence over DNS SAN and subject.
  EXPECT_THAT(attr.source().principal(), ::testing::StrEq(kUriSan));
  // Not requested, so not included.
  EXPECT_THAT(attr.source().certificate(), ::testing::IsEmpty());
  ASSERT_TRUE(attr.has_destination());
  ASSERT_TRUE(attr.destination().has_address());
  ASSERT_TRUE(attr.destination().address().has_socket_address());
  EXPECT_THAT(attr.destination().address().socket_address().address(),
              ::testing::StrEq(kLocalAddressHost));
  EXPECT_EQ(attr.destination().address().socket_address().port_value(),
            kLocalAddressPort);
  // The destination principal comes from this endpoint's own certificate,
  // with the same precedence rule, and is never the peer's identity.
  EXPECT_THAT(attr.destination().principal(), ::testing::StrEq(kLocalUriSan));
}

// IPv6 addresses are reported without the surrounding brackets.
TEST_F(CreateExtAuthzRequestTest, ServerRequestIpv6Address) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
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
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.source().address().has_socket_address());
  EXPECT_THAT(attr.source().address().socket_address().address(),
              ::testing::StrEq(kIpv6PeerAddressHost));
  EXPECT_EQ(attr.source().address().socket_address().port_value(),
            kIpv6PeerAddressPort);
  ASSERT_TRUE(attr.destination().address().has_socket_address());
  EXPECT_THAT(attr.destination().address().socket_address().address(),
              ::testing::StrEq(kIpv6LocalAddressHost));
  EXPECT_EQ(attr.destination().address().socket_address().port_value(),
            kIpv6LocalAddressPort);
}

// The two sides are resolved independently: an unknown local address must not
// suppress the source address (and vice versa).
TEST_F(CreateExtAuthzRequestTest, ServerRequestPeerAddressWithoutLocalAddress) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  EvaluateArgs::PerChannelArgs channel_args(
      &auth_context,
      ChannelArgs().Set(GRPC_ARG_ENDPOINT_PEER_ADDRESS, kPeerAddressUri));
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.source().address().has_socket_address());
  EXPECT_THAT(attr.source().address().socket_address().address(),
              ::testing::StrEq(kPeerAddressHost));
  EXPECT_EQ(attr.source().address().socket_address().port_value(),
            kPeerAddressPort);
  ASSERT_TRUE(attr.has_destination());
  EXPECT_FALSE(attr.destination().has_address());
}

// EvaluateArgs resolves endpoint addresses with StringToSockaddr(), which
// parses only IPv4/IPv6 host:port strings, so a unix domain socket peer yields
// no address at all, rather than a bogus socket address. This is why
// CreateAddress() does not implement the envoy.config.core.v3.Pipe case: it
// would be unreachable.
TEST_F(CreateExtAuthzRequestTest, ServerRequestUnixDomainSocketAddress) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  EvaluateArgs::PerChannelArgs channel_args(
      &auth_context,
      ChannelArgs()
          .Set(GRPC_ARG_ENDPOINT_PEER_ADDRESS, kUnixAddressUri)
          .Set(GRPC_ARG_ENDPOINT_LOCAL_ADDRESS, kUnixAddressUri));
  EvaluateArgs args(&batch, &channel_args);
  // EvaluateArgs itself cannot represent the address either.
  EXPECT_EQ(args.GetPeerAddress().len, 0u);
  EXPECT_EQ(args.GetLocalAddress().len, 0u);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.has_source());
  EXPECT_FALSE(attr.source().has_address());
  EXPECT_FALSE(attr.source().address().has_pipe());
  ASSERT_TRUE(attr.has_destination());
  EXPECT_FALSE(attr.destination().has_address());
  EXPECT_FALSE(attr.destination().address().has_pipe());
}

TEST_F(CreateExtAuthzRequestTest, ServerRequestPrincipalFallsBackToDnsSan) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_PEER_DNS_PROPERTY_NAME, kDnsSan);
  auth_context.add_cstring_property(GRPC_X509_SUBJECT_PROPERTY_NAME, kSubject);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.attributes().has_source());
  EXPECT_THAT(request.attributes().source().principal(),
              ::testing::StrEq(kDnsSan));
}

TEST_F(CreateExtAuthzRequestTest, ServerRequestPrincipalFallsBackToSubject) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_X509_SUBJECT_PROPERTY_NAME, kSubject);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.attributes().has_source());
  EXPECT_THAT(request.attributes().source().principal(),
              ::testing::StrEq(kSubject));
}

// Empty SAN entries are skipped rather than being reported as the principal,
// whether they come before or after the first usable one.
TEST_F(CreateExtAuthzRequestTest, ServerRequestPrincipalSkipsEmptyUriSan) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
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
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.attributes().has_source());
  EXPECT_THAT(request.attributes().source().principal(),
              ::testing::StrEq(kUriSan));
}

// If every URI and DNS SAN is empty, we fall all the way through to the
// subject.
TEST_F(CreateExtAuthzRequestTest, ServerRequestPrincipalSkipsAllEmptySans) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, "");
  auth_context.add_cstring_property(GRPC_PEER_DNS_PROPERTY_NAME, "");
  auth_context.add_cstring_property(GRPC_X509_SUBJECT_PROPERTY_NAME, kSubject);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.attributes().has_source());
  EXPECT_THAT(request.attributes().source().principal(),
              ::testing::StrEq(kSubject));
}

// Pins the lifetime contract documented on GetPrincipal(): the principal is
// read out of the std::vector<absl::string_view> that EvaluateArgs returns *by
// value*, so it is read after that vector has been destroyed. The views point
// into the grpc_auth_context, not into the vector, so this must be safe. The
// SAN values here are long and numerous enough that the vector's storage is
// heap-allocated, so that a regression would show up as a heap-use-after-free
// under ASAN rather than silently reading stale stack memory.
TEST_F(CreateExtAuthzRequestTest, ServerRequestPrincipalDoesNotDangle) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  const std::string long_san =
      absl::StrCat("spiffe://", std::string(256, 'a'), ".example.com/workload");
  for (int i = 0; i < 16; ++i) {
    auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, "");
  }
  auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME,
                                    long_san.c_str());
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.attributes().has_source());
  EXPECT_THAT(request.attributes().source().principal(),
              ::testing::StrEq(long_san));
}

// The local URI SAN takes precedence over the local DNS SAN and subject, so
// with no URI SAN we fall back to the DNS SAN.
TEST_F(CreateExtAuthzRequestTest,
       ServerRequestDestinationPrincipalFallsBackToLocalDnsSan) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_X509_LOCAL_DNS_PROPERTY_NAME,
                                    kLocalDnsSan);
  auth_context.add_cstring_property(GRPC_X509_LOCAL_SUBJECT_PROPERTY_NAME,
                                    kLocalSubject);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.attributes().has_destination());
  EXPECT_THAT(request.attributes().destination().principal(),
              ::testing::StrEq(kLocalDnsSan));
}

TEST_F(CreateExtAuthzRequestTest,
       ServerRequestDestinationPrincipalFallsBackToLocalSubject) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_X509_LOCAL_SUBJECT_PROPERTY_NAME,
                                    kLocalSubject);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.attributes().has_destination());
  EXPECT_THAT(request.attributes().destination().principal(),
              ::testing::StrEq(kLocalSubject));
}

// Empty local SAN values are skipped rather than being reported as the
// principal, exactly as for the source side.
TEST_F(CreateExtAuthzRequestTest,
       ServerRequestDestinationPrincipalSkipsEmptyLocalSans) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_X509_LOCAL_URI_PROPERTY_NAME, "");
  auth_context.add_cstring_property(GRPC_X509_LOCAL_DNS_PROPERTY_NAME, "");
  auth_context.add_cstring_property(GRPC_X509_LOCAL_SUBJECT_PROPERTY_NAME,
                                    kLocalSubject);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.attributes().has_destination());
  EXPECT_THAT(request.attributes().destination().principal(),
              ::testing::StrEq(kLocalSubject));
}

// The peer's identity must never leak into destination.principal, and this
// endpoint's identity must never leak into source.principal.
TEST_F(CreateExtAuthzRequestTest, ServerRequestPrincipalsAreNotInterchanged) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, kUriSan);
  auth_context.add_cstring_property(GRPC_PEER_DNS_PROPERTY_NAME, kDnsSan);
  auth_context.add_cstring_property(GRPC_X509_SUBJECT_PROPERTY_NAME, kSubject);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.has_source());
  EXPECT_THAT(attr.source().principal(), ::testing::StrEq(kUriSan));
  // Only peer properties are set, so there is no local identity to report.
  ASSERT_TRUE(attr.has_destination());
  EXPECT_THAT(attr.destination().principal(), ::testing::IsEmpty());
}

TEST_F(CreateExtAuthzRequestTest,
       ServerRequestSourcePrincipalIgnoresLocalProperties) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_X509_LOCAL_URI_PROPERTY_NAME,
                                    kLocalUriSan);
  auth_context.add_cstring_property(GRPC_X509_LOCAL_DNS_PROPERTY_NAME,
                                    kLocalDnsSan);
  auth_context.add_cstring_property(GRPC_X509_LOCAL_SUBJECT_PROPERTY_NAME,
                                    kLocalSubject);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.has_source());
  EXPECT_THAT(attr.source().principal(), ::testing::IsEmpty());
  ASSERT_TRUE(attr.has_destination());
  EXPECT_THAT(attr.destination().principal(), ::testing::StrEq(kLocalUriSan));
}

// With no TLS credentials and no endpoint addresses, source and destination
// are present but empty.  This is what a non-TLS transport (insecure, local,
// ALTS, ...) looks like: those build their own auth contexts with no X.509
// properties at all, so both principals are simply unset.
TEST_F(CreateExtAuthzRequestTest, ServerRequestWithoutConnectionAttributes) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.has_source());
  EXPECT_FALSE(attr.source().has_address());
  EXPECT_THAT(attr.source().principal(), ::testing::IsEmpty());
  ASSERT_TRUE(attr.has_destination());
  EXPECT_FALSE(attr.destination().has_address());
  EXPECT_THAT(attr.destination().principal(), ::testing::IsEmpty());
}

TEST_F(CreateExtAuthzRequestTest, ServerRequestWithPeerCertificate) {
  grpc_metadata_batch batch;
  grpc_auth_context auth_context(nullptr);
  EvaluateArgs::PerChannelArgs channel_args(&auth_context, ChannelArgs());
  EvaluateArgs args(&batch, &channel_args);
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = kPath;
  params.args = &args;
  params.peer_certificate = kEncodedCert;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.attributes().has_source());
  EXPECT_THAT(request.attributes().source().certificate(),
              ::testing::StrEq(kEncodedCert));
}

// The peer certificate is ignored on the client side, where source is not
// populated at all.
TEST_F(CreateExtAuthzRequestTest, ClientRequestIgnoresPeerCertificate) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.peer_certificate = kEncodedCert;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  EXPECT_FALSE(request.attributes().has_source());
}

TEST_F(CreateExtAuthzRequestTest, HttpFields) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_request());
  ASSERT_TRUE(request.attributes().request().has_http());
  const auto& http = request.attributes().request().http();
  EXPECT_THAT(http.method(), ::testing::StrEq(kMethodPost));
  EXPECT_THAT(http.path(), ::testing::StrEq(kPath));
  EXPECT_THAT(http.protocol(), ::testing::StrEq(kProtocolHttp2));
  EXPECT_THAT(http.size(), ::testing::Eq(-1));
}

TEST_F(CreateExtAuthzRequestTest, ExplicitStartTime) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.start_time = Timestamp::FromMillisecondsAfterProcessEpoch(123456789);
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_request());
  ASSERT_TRUE(request.attributes().request().has_time());
  EXPECT_THAT(request.attributes().request().time().seconds(),
              ::testing::Gt(0));
}

TEST_F(CreateExtAuthzRequestTest, DefaultStartTime) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.start_time = std::nullopt;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_request());
  ASSERT_TRUE(request.attributes().request().has_time());
  EXPECT_THAT(request.attributes().request().time().seconds(),
              ::testing::Gt(0));
}

TEST_F(CreateExtAuthzRequestTest, HeaderEncodingTextAndBinary) {
  grpc_metadata_batch batch;
  batch.Append(kCustomTextKey, Slice::FromStaticString(kCustomTextVal),
               [](absl::string_view, const Slice&) {});
  batch.Append(kCustomBinKey, Slice::FromStaticString(kCustomBinVal),
               [](absl::string_view, const Slice&) {});
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.metadata = &batch;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_request());
  ASSERT_TRUE(request.attributes().request().has_http());
  const auto& http = request.attributes().request().http();
  ASSERT_TRUE(http.has_header_map());
  EXPECT_THAT(
      http.header_map().headers(),
      ::testing::ElementsAre(IsHeaderValue(kCustomTextKey, kCustomTextVal),
                             IsRawHeaderValue(kCustomBinKey, kCustomBinVal)));
}

TEST_F(CreateExtAuthzRequestTest, HeaderFilteringAllowedAndDisallowed) {
  grpc_metadata_batch batch;
  batch.Append(kAllowMe, Slice::FromStaticString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kDenyMe, Slice::FromStaticString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kOther, Slice::FromStaticString(kVal3),
               [](absl::string_view, const Slice&) {});
  batch.Append(kAllowPrefixFoo, Slice::FromStaticString(kVal4),
               [](absl::string_view, const Slice&) {});
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.metadata = &batch;
  params.disallowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, kDenyMe, false)
          .value(),
  };
  params.allowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, kAllowMe, false)
          .value(),
      StringMatcher::Create(StringMatcher::Type::kPrefix, kAllowPrefix, false)
          .value(),
  };
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_request());
  ASSERT_TRUE(request.attributes().request().has_http());
  const auto& http = request.attributes().request().http();
  ASSERT_TRUE(http.has_header_map());
  EXPECT_THAT(http.header_map().headers(),
              ::testing::ElementsAre(IsHeaderValue(kAllowMe, kVal1),
                                     IsHeaderValue(kAllowPrefixFoo, kVal4)));
}

TEST_F(CreateExtAuthzRequestTest, HeaderFilteringDisallowedTakesPrecedence) {
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromStaticString(kVal1),
               [](absl::string_view, const Slice&) {});
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.metadata = &batch;
  params.allowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kPrefix, kKeyPrefix, false)
          .value(),
  };
  params.disallowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey1, false).value(),
  };
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_request());
  ASSERT_TRUE(request.attributes().request().has_http());
  const auto& http = request.attributes().request().http();
  ASSERT_TRUE(http.has_header_map());
  EXPECT_THAT(http.header_map().headers(), ::testing::IsEmpty());
}

TEST_F(CreateExtAuthzRequestTest, HeaderFilteringOnlyAllowed) {
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromStaticString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromStaticString(kVal2),
               [](absl::string_view, const Slice&) {});
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.metadata = &batch;
  params.allowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey1, false).value(),
  };
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_request());
  ASSERT_TRUE(request.attributes().request().has_http());
  const auto& http = request.attributes().request().http();
  ASSERT_TRUE(http.has_header_map());
  EXPECT_THAT(http.header_map().headers(),
              ::testing::ElementsAre(IsHeaderValue(kKey1, kVal1)));
}

TEST_F(CreateExtAuthzRequestTest, HeaderFilteringOnlyDisallowed) {
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromStaticString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromStaticString(kVal2),
               [](absl::string_view, const Slice&) {});
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.metadata = &batch;
  params.disallowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey1, false).value(),
  };
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_request());
  ASSERT_TRUE(request.attributes().request().has_http());
  const auto& http = request.attributes().request().http();
  ASSERT_TRUE(http.has_header_map());
  EXPECT_THAT(http.header_map().headers(),
              ::testing::ElementsAre(IsHeaderValue(kKey2, kVal2)));
}

TEST_F(CreateExtAuthzRequestTest, HeaderFilteringDefaultForwardsAll) {
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromStaticString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromStaticString(kVal2),
               [](absl::string_view, const Slice&) {});
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.metadata = &batch;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_request());
  ASSERT_TRUE(request.attributes().request().has_http());
  const auto& http = request.attributes().request().http();
  ASSERT_TRUE(http.has_header_map());
  EXPECT_THAT(http.header_map().headers(),
              ::testing::ElementsAre(IsHeaderValue(kKey1, kVal1),
                                     IsHeaderValue(kKey2, kVal2)));
}

TEST_F(CreateExtAuthzRequestTest, MetadataBatchPathAndHeaders) {
  grpc_metadata_batch batch;
  batch.Set(HttpPathMetadata(), Slice::FromStaticString(kPath));
  batch.Append(kKey1, Slice::FromStaticString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromStaticString(kVal2),
               [](absl::string_view, const Slice&) {});
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = kPath;
  params.metadata = &batch;
  std::string serialized = CreateExtAuthzRequest(params).value();
  auto request = ParseRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_request());
  ASSERT_TRUE(request.attributes().request().has_http());
  const auto& http = request.attributes().request().http();
  EXPECT_THAT(http.path(), ::testing::StrEq(kPath));
  ASSERT_TRUE(http.has_header_map());
  EXPECT_THAT(http.header_map().headers(),
              ::testing::UnorderedElementsAre(IsHeaderValue(kPathHeader, kPath),
                                              IsHeaderValue(kKey1, kVal1),
                                              IsHeaderValue(kKey2, kVal2)));
}

//
// GetUrlEncodedPemPeerCertificate() tests
//

TEST(GetUrlEncodedPemPeerCertificateTest, NoAuthContext) {
  EXPECT_THAT(GetUrlEncodedPemPeerCertificate(nullptr), ::testing::IsEmpty());
}

TEST(GetUrlEncodedPemPeerCertificateTest, NoPeerCertificate) {
  grpc_auth_context auth_context(nullptr);
  EXPECT_THAT(GetUrlEncodedPemPeerCertificate(&auth_context),
              ::testing::IsEmpty());
}

// An empty property value is treated the same as a missing property, rather
// than producing an empty-but-present source.certificate field.
TEST(GetUrlEncodedPemPeerCertificateTest, EmptyPeerCertificateProperty) {
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_X509_PEM_CERT_PROPERTY_NAME, "");
  EXPECT_THAT(GetUrlEncodedPemPeerCertificate(&auth_context),
              ::testing::IsEmpty());
}

// Matches Envoy's Http::Utility::PercentEncoding::urlEncode(): every
// character other than ALPHA, DIGIT, '*', '-', '.' and '_' is percent-encoded
// with uppercase hex digits.
TEST(GetUrlEncodedPemPeerCertificateTest, PeerCertificate) {
  grpc_auth_context auth_context(nullptr);
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
TEST(GetUrlEncodedPemPeerCertificateTest, PeerCertificateEncodesAllReserved) {
  grpc_auth_context auth_context(nullptr);
  auth_context.add_cstring_property(GRPC_X509_PEM_CERT_PROPERTY_NAME,
                                    "aZ9*-._\r\n~:%");
  EXPECT_THAT(GetUrlEncodedPemPeerCertificate(&auth_context),
              ::testing::StrEq("aZ9*-._%0D%0A%7E%3A%25"));
}

//
// ExtAuthzResponse::Parse() tests
//

MATCHER_P2(StatusIs, status_code, message_matcher, "") {
  return ::testing::ExplainMatchResult(status_code, arg.code(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(message_matcher, arg.message(),
                                       result_listener);
}

MATCHER_P3(IsHeaderValueOption, key, value, append_action, "") {
  return ::testing::ExplainMatchResult(::testing::Pair(key, value), arg.header,
                                       result_listener) &&
         ::testing::ExplainMatchResult(append_action, arg.append_action,
                                       result_listener);
}

MATCHER_P2(IsHeaderMutation, set_headers_matcher, remove_headers_matcher, "") {
  return ::testing::ExplainMatchResult(set_headers_matcher, arg.set_headers,
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

constexpr absl::string_view kCheckResponseParseErrorMessage =
    "Failed to parse CheckResponse";
constexpr absl::string_view kStatusNotPresentErrorMessage =
    "status not present in CheckResponse";
constexpr absl::string_view kInvalidStatusCodeErrorMessage =
    "Invalid grpc status code in CheckResponse status: 99";
constexpr absl::string_view kOkResponseNotPresentErrorMessage =
    "ok_response not present in CheckResponse";
constexpr absl::string_view kHeaderOptionMissingValueErrorMessage =
    "either value or raw_value must be set";
constexpr absl::string_view kAllGood = "all good";
constexpr absl::string_view kRespHeaderKey = "x-resp-1";
constexpr absl::string_view kRespHeaderVal = "val-resp-1";
constexpr absl::string_view kDeniedResponseNotPresentErrorMessage =
    "denied_response not present in CheckResponse";
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

TEST_F(ParseExtAuthzResponseTest, MalformedProtobufFails) {
  auto parsed = ExtAuthzResponse::Parse("\x0a\xff");
  EXPECT_THAT(parsed.status(),
              StatusIs(absl::StatusCode::kInternal,
                       ::testing::StrEq(kCheckResponseParseErrorMessage)));
}

TEST_F(ParseExtAuthzResponseTest, EmptyPayloadFails) {
  auto parsed = ExtAuthzResponse::Parse("");
  EXPECT_THAT(parsed.status(),
              StatusIs(absl::StatusCode::kInternal,
                       ::testing::StrEq(kStatusNotPresentErrorMessage)));
}

TEST_F(ParseExtAuthzResponseTest, MissingStatusFieldFails) {
  CheckResponse response;
  response.mutable_denied_response();
  auto parsed = ParseResponse(response);
  EXPECT_THAT(parsed.status(),
              StatusIs(absl::StatusCode::kInternal,
                       ::testing::StrEq(kStatusNotPresentErrorMessage)));
}

TEST_F(ParseExtAuthzResponseTest, InvalidStatusCodeFails) {
  CheckResponse response;
  response.mutable_status()->set_code(99);
  auto parsed = ParseResponse(response);
  EXPECT_THAT(parsed.status(),
              StatusIs(absl::StatusCode::kInternal,
                       ::testing::StrEq(kInvalidStatusCodeErrorMessage)));
}

TEST_F(ParseExtAuthzResponseTest, StatusOkMissingOkResponseFails) {
  CheckResponse response;
  response.mutable_status()->set_code(0);
  auto parsed = ParseResponse(response);
  EXPECT_THAT(parsed.status(),
              StatusIs(absl::StatusCode::kInternal,
                       ::testing::StrEq(kOkResponseNotPresentErrorMessage)));
}

TEST_F(ParseExtAuthzResponseTest, OkResponseHeaderOptionMissingValueFails) {
  CheckResponse response;
  response.mutable_status()->set_code(0);
  auto* header = response.mutable_ok_response()->add_headers();
  header->mutable_header()->set_key(std::string(kKey1));
  auto parsed = ParseResponse(response);
  EXPECT_THAT(
      parsed.status(),
      StatusIs(absl::StatusCode::kInternal,
               ::testing::HasSubstr(kHeaderOptionMissingValueErrorMessage)));
}

TEST_F(ParseExtAuthzResponseTest, OkResponseNoHeaders) {
  CheckResponse response;
  response.mutable_status()->set_code(0);
  response.mutable_ok_response();
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << "status code: " << parsed.status().code()
                           << ", message: " << parsed.status().message();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_OK));
  EXPECT_THAT(
      parsed->response,
      IsOkResponse(IsHeaderMutation(::testing::IsEmpty(), ::testing::IsEmpty()),
                   ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest, OkResponseHeaderMutations) {
  CheckResponse response;
  response.mutable_status()->set_code(0);
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
  ASSERT_TRUE(parsed.ok()) << "status code: " << parsed.status().code()
                           << ", message: " << parsed.status().message();
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

TEST_F(ParseExtAuthzResponseTest, OkResponseInvalidHeaderToRemoveFails) {
  CheckResponse response;
  response.mutable_status()->set_code(0);
  auto* ok_response = response.mutable_ok_response();
  ok_response->add_headers_to_remove(std::string(kInvalidHeaderKey));
  auto parsed = ParseResponse(response);
  EXPECT_THAT(parsed.status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::StrEq(kInvalidHeaderKeyErrorMessage)));
}

TEST_F(ParseExtAuthzResponseTest, StatusNotOkMissingDeniedResponseFails) {
  CheckResponse response;
  response.mutable_status()->set_code(16);
  auto parsed = ParseResponse(response);
  EXPECT_THAT(
      parsed.status(),
      StatusIs(absl::StatusCode::kInternal,
               ::testing::StrEq(kDeniedResponseNotPresentErrorMessage)));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponseHeaderOptionMissingValueFails) {
  CheckResponse response;
  response.mutable_status()->set_code(7);
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(envoy::type::v3::Forbidden);
  auto* header = denied->add_headers();
  header->mutable_header()->set_key(std::string(kKey1));
  auto parsed = ParseResponse(response);
  EXPECT_THAT(
      parsed.status(),
      StatusIs(absl::StatusCode::kInternal,
               ::testing::HasSubstr(kHeaderOptionMissingValueErrorMessage)));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponseDefaultHttpStatus) {
  CheckResponse response;
  response.mutable_status()->set_code(7);
  response.mutable_denied_response();
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << "status code: " << parsed.status().code()
                           << ", message: " << parsed.status().message();
  EXPECT_THAT(parsed->status_code,
              ::testing::Eq(GRPC_STATUS_PERMISSION_DENIED));
  EXPECT_THAT(parsed->response,
              IsDeniedResponse(::testing::Eq(GRPC_STATUS_PERMISSION_DENIED),
                               ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponseHttpStatusMapping) {
  CheckResponse response;
  response.mutable_status()->set_code(7);
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(envoy::type::v3::Unauthorized);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << "status code: " << parsed.status().code()
                           << ", message: " << parsed.status().message();
  EXPECT_THAT(parsed->status_code,
              ::testing::Eq(GRPC_STATUS_PERMISSION_DENIED));
  EXPECT_THAT(parsed->response,
              IsDeniedResponse(::testing::Eq(GRPC_STATUS_UNAUTHENTICATED),
                               ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponseHeaders) {
  CheckResponse response;
  response.mutable_status()->set_code(7);
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(envoy::type::v3::Forbidden);
  *denied->add_headers() =
      CreateHeaderValueOption(kDenyReasonHeaderKey, kDenyReasonHeaderVal);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << "status code: " << parsed.status().code()
                           << ", message: " << parsed.status().message();
  EXPECT_THAT(parsed->status_code,
              ::testing::Eq(GRPC_STATUS_PERMISSION_DENIED));
  EXPECT_THAT(
      parsed->response,
      IsDeniedResponse(
          ::testing::Eq(GRPC_STATUS_PERMISSION_DENIED),
          ::testing::ElementsAre(IsHeaderValueOption(
              kDenyReasonHeaderKey, kDenyReasonHeaderVal,
              XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd))));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponsePreservesStatus) {
  CheckResponse response;
  response.mutable_status()->set_code(16);
  response.mutable_status()->set_message(std::string(kUnauthenticatedRpc));
  response.mutable_denied_response();
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << "status code: " << parsed.status().code()
                           << ", message: " << parsed.status().message();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_UNAUTHENTICATED));
  EXPECT_THAT(parsed->status_message, ::testing::StrEq(kUnauthenticatedRpc));
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
