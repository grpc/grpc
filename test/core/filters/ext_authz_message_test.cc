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
#include <grpc/status.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v3/attribute_context.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"
#include "google/rpc/status.pb.h"
#include "src/core/ext/filters/ext_authz/ext_authz_messages.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/util/matchers.h"
#include "src/core/util/time.h"
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
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

constexpr absl::string_view kKey1 = "key1";
constexpr absl::string_view kVal1 = "val1";
constexpr absl::string_view kKey2 = "key2";
constexpr absl::string_view kVal2 = "val2";
constexpr absl::string_view kKey3 = "key3";

MATCHER_P3(IsHeaderValueOption, key, value, append_action, "") {
  return ::testing::ExplainMatchResult(::testing::Pair(key, value), arg.header,
                                       result_listener) &&
         ::testing::ExplainMatchResult(append_action, arg.append_action,
                                       result_listener);
}

MATCHER_P2(IsHeader, key, value, "") {
  return ::testing::ExplainMatchResult(key, arg.key(), result_listener) &&
         ::testing::ExplainMatchResult(
             value, arg.raw_value().empty() ? arg.value() : arg.raw_value(),
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

//
// CreateExtAuthzRequest() tests
//

class CreateExtAuthzRequestTest : public ::testing::Test {
 protected:
  CheckRequest ParseRequest(const absl::StatusOr<std::string>& serialized) {
    EXPECT_TRUE(serialized.ok()) << serialized.status();
    CheckRequest parsed;
    if (serialized.ok()) {
      EXPECT_TRUE(parsed.ParseFromString(*serialized));
    }
    return parsed;
  }

  CheckRequest ParseRequest(const std::string& serialized) {
    CheckRequest parsed;
    EXPECT_TRUE(parsed.ParseFromString(serialized));
    return parsed;
  }
};

TEST_F(CreateExtAuthzRequestTest, ClientSideSerialization) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = "/test.service/TestMethod";
  params.start_time = Timestamp::FromMillisecondsAfterProcessEpoch(123456789);
  params.headers = {
      {"custom-header-1", "val1"},
      {"custom-header-2-bin", "binval"},
  };
  params.source.address = *StringToSockaddr("192.168.1.100:50051");
  params.destination.address = *StringToSockaddr("10.0.0.1:50052");

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  ASSERT_TRUE(request.has_attributes());
  const auto& attr = request.attributes();

  // On client side, source and destination MUST NOT be set.
  EXPECT_FALSE(attr.has_source());
  EXPECT_FALSE(attr.has_destination());

  // Request MUST be set.
  ASSERT_TRUE(attr.has_request());
  const auto& req = attr.request();

  // Time MUST be set.
  EXPECT_TRUE(req.has_time());

  // HTTP request fields.
  ASSERT_TRUE(req.has_http());
  const auto& http = req.http();
  EXPECT_EQ(http.method(), "POST");
  EXPECT_EQ(http.path(), "/test.service/TestMethod");
  EXPECT_EQ(http.protocol(), "HTTP/2");
  EXPECT_EQ(http.size(), -1);

  // Verify header_map entries.
  ASSERT_TRUE(http.has_header_map());
  ASSERT_EQ(http.header_map().headers_size(), 2);
  EXPECT_EQ(http.header_map().headers(0).key(), "custom-header-1");
  EXPECT_EQ(http.header_map().headers(0).value(), "val1");
  EXPECT_TRUE(http.header_map().headers(0).raw_value().empty());
  EXPECT_EQ(http.header_map().headers(1).key(), "custom-header-2-bin");
  EXPECT_EQ(http.header_map().headers(1).raw_value(), "binval");
  EXPECT_TRUE(http.header_map().headers(1).value().empty());

  // Verify unsupported/disallowed fields are NOT set.
  EXPECT_EQ(http.headers_size(), 0);
  EXPECT_TRUE(http.id().empty());
  EXPECT_TRUE(http.scheme().empty());
  EXPECT_TRUE(http.query().empty());
  EXPECT_TRUE(http.fragment().empty());
  EXPECT_TRUE(http.body().empty());
  EXPECT_TRUE(http.raw_body().empty());
  EXPECT_FALSE(attr.has_metadata_context());
  EXPECT_FALSE(attr.has_route_metadata_context());
  EXPECT_FALSE(attr.has_tls_session());
  EXPECT_EQ(attr.context_extensions_size(), 0);
}

TEST_F(CreateExtAuthzRequestTest, ClientSideSerialization_NoStartTime) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = "/test.service/TestMethod";
  params.start_time = std::nullopt;

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_request());
  EXPECT_TRUE(request.attributes().request().has_time());
  EXPECT_GT(request.attributes().request().time().seconds(), 0);
}

TEST_F(CreateExtAuthzRequestTest, ServerSideSerialization_PlainConnection) {
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = "/service/plain";
  params.source.address = *StringToSockaddr("192.168.1.10:12345");
  params.destination.address = *StringToSockaddr("10.0.0.1:8080");

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  ASSERT_TRUE(request.has_attributes());
  const auto& attr = request.attributes();

  // Source peer checks
  ASSERT_TRUE(attr.has_source());
  const auto& source = attr.source();
  ASSERT_TRUE(source.has_address());
  ASSERT_TRUE(source.address().has_socket_address());
  EXPECT_EQ(source.address().socket_address().address(), "192.168.1.10");
  EXPECT_EQ(source.address().socket_address().port_value(), 12345);
  EXPECT_EQ(source.address().socket_address().protocol(),
            envoy::config::core::v3::SocketAddress_Protocol_TCP);
  EXPECT_TRUE(source.principal().empty());
  EXPECT_TRUE(source.certificate().empty());
  EXPECT_TRUE(source.service().empty());
  EXPECT_EQ(source.labels_size(), 0);

  // Destination peer checks
  ASSERT_TRUE(attr.has_destination());
  const auto& dest = attr.destination();
  ASSERT_TRUE(dest.has_address());
  ASSERT_TRUE(dest.address().has_socket_address());
  EXPECT_EQ(dest.address().socket_address().address(), "10.0.0.1");
  EXPECT_EQ(dest.address().socket_address().port_value(), 8080);
  EXPECT_EQ(dest.address().socket_address().protocol(),
            envoy::config::core::v3::SocketAddress_Protocol_TCP);
  EXPECT_TRUE(dest.principal().empty());
  EXPECT_TRUE(dest.certificate().empty());
  EXPECT_TRUE(dest.service().empty());
  EXPECT_EQ(dest.labels_size(), 0);
}

TEST_F(CreateExtAuthzRequestTest, ServerSideSerialization_UnixDomainSocket) {
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = "/service/unix";
  grpc_resolved_address peer_addr;
  GRPC_CHECK_OK(UnixSockaddrPopulate("/tmp/client.sock", &peer_addr));
  params.source.address = peer_addr;
  grpc_resolved_address local_addr;
  GRPC_CHECK_OK(UnixSockaddrPopulate("/var/run/server.sock", &local_addr));
  params.destination.address = local_addr;

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  ASSERT_TRUE(request.has_attributes());
  const auto& attr = request.attributes();

  ASSERT_TRUE(attr.has_source());
  ASSERT_TRUE(attr.source().has_address());
  ASSERT_TRUE(attr.source().address().has_pipe());
  EXPECT_EQ(attr.source().address().pipe().path(), "/tmp/client.sock");

  ASSERT_TRUE(attr.has_destination());
  ASSERT_TRUE(attr.destination().has_address());
  ASSERT_TRUE(attr.destination().address().has_pipe());
  EXPECT_EQ(attr.destination().address().pipe().path(), "/var/run/server.sock");
}

TEST_F(CreateExtAuthzRequestTest, ServerSideSerialization_Ipv6Addresses) {
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = "/service/ipv6";
  params.source.address = *StringToSockaddr("[2001:db8::1]:12345");
  params.destination.address = *StringToSockaddr("[2001:db8::2]:8080");

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  ASSERT_TRUE(request.has_attributes());
  const auto& attr = request.attributes();

  ASSERT_TRUE(attr.has_source());
  ASSERT_TRUE(attr.source().has_address());
  ASSERT_TRUE(attr.source().address().has_socket_address());
  EXPECT_EQ(attr.source().address().socket_address().address(), "2001:db8::1");
  EXPECT_EQ(attr.source().address().socket_address().port_value(), 12345);

  ASSERT_TRUE(attr.has_destination());
  ASSERT_TRUE(attr.destination().has_address());
  ASSERT_TRUE(attr.destination().address().has_socket_address());
  EXPECT_EQ(attr.destination().address().socket_address().address(),
            "2001:db8::2");
  EXPECT_EQ(attr.destination().address().socket_address().port_value(), 8080);
}

TEST_F(CreateExtAuthzRequestTest,
       ServerSideSerialization_TlsConnection_UriSanPriority) {
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = "/service/tls";
  params.source.address = *StringToSockaddr("192.168.1.10:12345");
  params.source.uri_sans = {"spiffe://example.com/client-uri-1",
                            "spiffe://example.com/client-uri-2"};
  params.source.dns_sans = {"client-dns.example.com"};
  params.source.subject = "CN=client,O=Example";
  params.source.certificate =
      "-----BEGIN CERTIFICATE-----\nclient_cert\n-----END CERTIFICATE-----";
  params.include_peer_certificate = true;

  params.destination.address = *StringToSockaddr("10.0.0.1:8080");
  params.destination.uri_sans = {"spiffe://example.com/server-uri"};
  params.destination.dns_sans = {"server-dns.example.com"};
  params.destination.subject = "CN=server,O=Example";

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.has_source());
  EXPECT_EQ(attr.source().principal(), "spiffe://example.com/client-uri-1");
  EXPECT_EQ(
      attr.source().certificate(),
      "-----BEGIN CERTIFICATE-----\nclient_cert\n-----END CERTIFICATE-----");

  ASSERT_TRUE(attr.has_destination());
  EXPECT_EQ(attr.destination().principal(), "spiffe://example.com/server-uri");
  EXPECT_TRUE(attr.destination().certificate().empty());
}

TEST_F(CreateExtAuthzRequestTest,
       ServerSideSerialization_TlsConnection_DnsSanFallback) {
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = "/service/tls";
  params.source.address = *StringToSockaddr("192.168.1.10:12345");
  params.source.uri_sans = {};
  params.source.dns_sans = {"client-dns-1.example.com",
                            "client-dns-2.example.com"};
  params.source.subject = "CN=client,O=Example";
  params.source.certificate = "cert_data";
  params.include_peer_certificate = false;

  params.destination.address = *StringToSockaddr("10.0.0.1:8080");
  params.destination.uri_sans = {};
  params.destination.dns_sans = {"server-dns.example.com"};
  params.destination.subject = "CN=server,O=Example";

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.has_source());
  EXPECT_EQ(attr.source().principal(), "client-dns-1.example.com");
  EXPECT_TRUE(attr.source().certificate().empty());

  ASSERT_TRUE(attr.has_destination());
  EXPECT_EQ(attr.destination().principal(), "server-dns.example.com");
  EXPECT_TRUE(attr.destination().certificate().empty());
}

TEST_F(CreateExtAuthzRequestTest,
       ServerSideSerialization_TlsConnection_SubjectFallback) {
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = "/service/tls";
  params.source.address = *StringToSockaddr("192.168.1.10:12345");
  params.source.uri_sans = {};
  params.source.dns_sans = {};
  params.source.subject = "CN=client,O=Example Corp,C=US";

  params.destination.address = *StringToSockaddr("10.0.0.1:8080");
  params.destination.uri_sans = {};
  params.destination.dns_sans = {};
  params.destination.subject = "CN=server,O=Example Corp,C=US";

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.has_source());
  EXPECT_EQ(attr.source().principal(), "CN=client,O=Example Corp,C=US");

  ASSERT_TRUE(attr.has_destination());
  EXPECT_EQ(attr.destination().principal(), "CN=server,O=Example Corp,C=US");
}

TEST_F(CreateExtAuthzRequestTest,
       ServerSideSerialization_TlsConnection_UnsetPrincipal) {
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = "/service/tls";
  params.source.address = *StringToSockaddr("192.168.1.10:12345");
  params.source.uri_sans = {};
  params.source.dns_sans = {};
  params.source.subject = "";

  params.destination.address = *StringToSockaddr("10.0.0.1:8080");
  params.destination.uri_sans = {};
  params.destination.dns_sans = {};
  params.destination.subject = "";

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.has_source());
  EXPECT_TRUE(attr.source().principal().empty());

  ASSERT_TRUE(attr.has_destination());
  EXPECT_TRUE(attr.destination().principal().empty());
}

TEST_F(CreateExtAuthzRequestTest, HeaderFiltering_AllowedAndDisallowed) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = "/test/filter";
  params.headers = {
      {"allow-me", "1"},
      {"deny-me", "2"},
      {"other", "3"},
      {"allow-prefix-foo", "4"},
  };
  params.disallowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, "deny-me", false)
          .value(),
  };
  params.allowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, "allow-me", false)
          .value(),
      StringMatcher::Create(StringMatcher::Type::kPrefix, "allow-prefix-",
                            false)
          .value(),
  };

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  const auto& http = request.attributes().request().http();
  ASSERT_TRUE(http.has_header_map());
  ASSERT_EQ(http.header_map().headers_size(), 2);
  EXPECT_EQ(http.header_map().headers(0).key(), "allow-me");
  EXPECT_EQ(http.header_map().headers(0).value(), "1");
  EXPECT_EQ(http.header_map().headers(1).key(), "allow-prefix-foo");
  EXPECT_EQ(http.header_map().headers(1).value(), "4");
}

TEST_F(CreateExtAuthzRequestTest, HeaderFiltering_DisallowedTakesPrecedence) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = "/test/filter";
  params.headers = {
      {"test-header", "value"},
  };
  params.allowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kPrefix, "test-", false)
          .value(),
  };
  params.disallowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, "test-header", false)
          .value(),
  };

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  const auto& http = request.attributes().request().http();
  EXPECT_EQ(http.header_map().headers_size(), 0);
}

TEST_F(CreateExtAuthzRequestTest, HeaderFiltering_OnlyAllowedSet) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = "/test/filter";
  params.headers = {
      {"h1", "v1"},
      {"h2", "v2"},
  };
  params.allowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, "h1", false).value(),
  };

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  const auto& http = request.attributes().request().http();
  ASSERT_EQ(http.header_map().headers_size(), 1);
  EXPECT_EQ(http.header_map().headers(0).key(), "h1");
  EXPECT_EQ(http.header_map().headers(0).value(), "v1");
}

TEST_F(CreateExtAuthzRequestTest, HeaderFiltering_OnlyDisallowedSet) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = "/test/filter";
  params.headers = {
      {"h1", "v1"},
      {"h2", "v2"},
  };
  params.disallowed_headers = {
      StringMatcher::Create(StringMatcher::Type::kExact, "h1", false).value(),
  };

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  const auto& http = request.attributes().request().http();
  ASSERT_EQ(http.header_map().headers_size(), 1);
  EXPECT_EQ(http.header_map().headers(0).key(), "h2");
  EXPECT_EQ(http.header_map().headers(0).value(), "v2");
}

TEST_F(CreateExtAuthzRequestTest, HeaderValue_BinaryAndNonBinary) {
  ExtAuthzRequest params;
  params.is_client_call = true;
  params.path = "/test/headers";
  params.headers = {
      {"custom-text", "plain-text-value"},
      {"custom-bin", std::string("\x00\x01\x02\xFF", 4)},
  };

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  const auto& http = request.attributes().request().http();
  ASSERT_EQ(http.header_map().headers_size(), 2);
  EXPECT_EQ(http.header_map().headers(0).key(), "custom-text");
  EXPECT_EQ(http.header_map().headers(0).value(), "plain-text-value");
  EXPECT_TRUE(http.header_map().headers(0).raw_value().empty());

  EXPECT_EQ(http.header_map().headers(1).key(), "custom-bin");
  EXPECT_TRUE(http.header_map().headers(1).value().empty());
  EXPECT_EQ(http.header_map().headers(1).raw_value(),
            std::string("\x00\x01\x02\xFF", 4));
}

TEST_F(CreateExtAuthzRequestTest,
       ServerSideSerialization_PeerAddressWithoutPort) {
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = "/service/test";
  params.source.address = *StringToSockaddr("192.168.1.5", 8080);
  params.destination.address = *StringToSockaddr("10.0.0.5", 9090);

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_source());
  ASSERT_TRUE(request.attributes().source().has_address());
  EXPECT_EQ(request.attributes().source().address().socket_address().address(),
            "192.168.1.5");
  EXPECT_EQ(
      request.attributes().source().address().socket_address().port_value(),
      8080);

  ASSERT_TRUE(request.attributes().has_destination());
  ASSERT_TRUE(request.attributes().destination().has_address());
  EXPECT_EQ(
      request.attributes().destination().address().socket_address().address(),
      "10.0.0.5");
  EXPECT_EQ(request.attributes()
                .destination()
                .address()
                .socket_address()
                .port_value(),
            9090);
}

TEST_F(CreateExtAuthzRequestTest,
       ServerSideSerialization_Ipv6BareWithoutBrackets) {
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = "/service/test";
  params.source.address = *StringToSockaddr("2001:db8::1", 9090);

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_source());
  ASSERT_TRUE(request.attributes().source().has_address());
  EXPECT_EQ(request.attributes().source().address().socket_address().address(),
            "2001:db8::1");
  EXPECT_EQ(
      request.attributes().source().address().socket_address().port_value(),
      9090);
}

TEST_F(CreateExtAuthzRequestTest, ServerSideSerialization_NoAddress) {
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = "/service/test";
  params.source.address = std::nullopt;
  params.destination.address = std::nullopt;

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_source());
  EXPECT_FALSE(request.attributes().source().has_address());
  ASSERT_TRUE(request.attributes().has_destination());
  EXPECT_FALSE(request.attributes().destination().has_address());
}

TEST_F(CreateExtAuthzRequestTest, ServerSideSerialization_EmptySanSkipping) {
  ExtAuthzRequest params;
  params.is_client_call = false;
  params.path = "/service/test";
  params.source.address = *StringToSockaddr("127.0.0.1:50051");
  params.source.uri_sans = {"", "spiffe://example.com/test-service"};
  params.source.dns_sans = {"dns.example.com"};
  params.source.subject = "CN=subject";

  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  auto request = ParseRequest(*serialized);

  ASSERT_TRUE(request.has_attributes());
  ASSERT_TRUE(request.attributes().has_source());
  EXPECT_EQ(request.attributes().source().principal(),
            "spiffe://example.com/test-service");
}

//
// ExtAuthzResponse::Parse() tests
//

class ParseExtAuthzResponseTest : public ::testing::Test {
 protected:
  absl::StatusOr<ExtAuthzResponse> ParseResponse(
      const CheckResponse& response) {
    std::string serialized;
    EXPECT_TRUE(response.SerializeToString(&serialized));
    return ExtAuthzResponse::Parse(serialized);
  }
};

TEST_F(ParseExtAuthzResponseTest, ResponseInvalid) {
  // Field 1 (length-delimited) with length 255, but no data.
  auto parsed = ExtAuthzResponse::Parse("\x0a\xff");
  EXPECT_EQ(parsed.status(),
            absl::InternalError("Failed to parse CheckResponse"));
}

TEST_F(ParseExtAuthzResponseTest, ResponseInvalid_InvalidStatusCode) {
  CheckResponse response;
  response.mutable_status()->set_code(99);  // Out of range
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError(
                "Invalid grpc status code in CheckResponse status: 99"));

  response.mutable_status()->set_code(-1);
  parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError(
                "Invalid grpc status code in CheckResponse status: -1"));
}

TEST_F(ParseExtAuthzResponseTest, EmptyPayload) {
  auto parsed = ExtAuthzResponse::Parse("");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_OK);
  EXPECT_EQ(parsed->status_message, "");
  EXPECT_THAT(
      parsed->response,
      IsOkResponse(IsHeaderMutation(::testing::IsEmpty(), ::testing::IsEmpty()),
                   ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest, OkResponse) {
  CheckResponse response;
  response.mutable_status()->set_code(0);  // OK
  response.mutable_status()->set_message("all good");
  auto* ok_response = response.mutable_ok_response();
  auto* h1 = ok_response->add_headers();
  h1->mutable_header()->set_key(std::string(kKey1));
  h1->mutable_header()->set_value(std::string(kVal1));
  h1->set_append_action(HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
  auto* h2 = ok_response->add_headers();
  h2->mutable_header()->set_key(std::string(kKey2));
  h2->mutable_header()->set_raw_value(std::string(kVal2));
  h2->set_append_action(HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  ok_response->add_headers_to_remove(std::string(kKey3));
  auto* resp_h1 = ok_response->add_response_headers_to_add();
  resp_h1->mutable_header()->set_key("x-resp-1");
  resp_h1->mutable_header()->set_value("val-resp-1");
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_OK);
  EXPECT_EQ(parsed->status_message, "all good");
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
              "x-resp-1", "val-resp-1",
              XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd))));
}

TEST_F(ParseExtAuthzResponseTest, OkResponse_WithoutHttpHeaders) {
  CheckResponse response;
  response.mutable_status()->set_code(0);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_OK);
  EXPECT_THAT(
      parsed->response,
      IsOkResponse(IsHeaderMutation(::testing::IsEmpty(), ::testing::IsEmpty()),
                   ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest, OkResponse_HeaderValueOptionInvalid) {
  CheckResponse response;
  response.mutable_status()->set_code(0);
  auto* ok_response = response.mutable_ok_response();
  auto* h1 = ok_response->add_headers();
  h1->mutable_header()->set_key(std::string(kKey1));
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("Failed to parse XdsHeaderValueOption: "
                                "[field:header error:either value or raw_value "
                                "must be set]"));
}

TEST_F(ParseExtAuthzResponseTest, OkResponse_ResponseHeadersToAddInvalid) {
  CheckResponse response;
  response.mutable_status()->set_code(0);
  auto* ok_response = response.mutable_ok_response();
  auto* h1 = ok_response->add_response_headers_to_add();
  h1->mutable_header()->set_key(":path");
  h1->mutable_header()->set_value("val");
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("Failed to parse XdsHeaderValueOption: "
                                "[field:header.key error:header \":path\" "
                                "not allowed]"));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponse_Forbidden) {
  CheckResponse response;
  response.mutable_status()->set_code(7);  // PERMISSION_DENIED
  response.mutable_status()->set_message("forbidden access");
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(envoy::type::v3::Forbidden);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_PERMISSION_DENIED);
  EXPECT_EQ(parsed->status_message, "forbidden access");
  EXPECT_THAT(parsed->response, IsDeniedResponse(GRPC_STATUS_PERMISSION_DENIED,
                                                 ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponse_UnauthorizedWithHeaders) {
  CheckResponse response;
  response.mutable_status()->set_code(7);
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(envoy::type::v3::Unauthorized);
  auto* h1 = denied->add_headers();
  h1->mutable_header()->set_key("www-authenticate");
  h1->mutable_header()->set_value("Bearer");
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_PERMISSION_DENIED);
  EXPECT_THAT(
      parsed->response,
      IsDeniedResponse(
          GRPC_STATUS_UNAUTHENTICATED,
          ::testing::ElementsAre(IsHeaderValueOption(
              "www-authenticate", "Bearer",
              XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd))));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponse_HttpStatusMappings) {
  struct TestCase {
    envoy::type::v3::StatusCode http_code;
    grpc_status_code expected_grpc_code;
  };
  std::vector<TestCase> test_cases = {
      {envoy::type::v3::Unauthorized, GRPC_STATUS_UNAUTHENTICATED},
      {envoy::type::v3::Forbidden, GRPC_STATUS_PERMISSION_DENIED},
      {envoy::type::v3::NotFound, GRPC_STATUS_UNIMPLEMENTED},
      {envoy::type::v3::TooManyRequests, GRPC_STATUS_UNAVAILABLE},
      {envoy::type::v3::InternalServerError, GRPC_STATUS_UNKNOWN},
      {envoy::type::v3::BadGateway, GRPC_STATUS_UNAVAILABLE},
      {envoy::type::v3::ServiceUnavailable, GRPC_STATUS_UNAVAILABLE},
      {envoy::type::v3::GatewayTimeout, GRPC_STATUS_UNAVAILABLE},
  };
  for (const auto& test_case : test_cases) {
    CheckResponse response;
    response.mutable_status()->set_code(7);
    auto* denied = response.mutable_denied_response();
    denied->mutable_status()->set_code(test_case.http_code);
    auto parsed = ParseResponse(response);
    ASSERT_TRUE(parsed.ok()) << parsed.status();
    EXPECT_THAT(parsed->response, IsDeniedResponse(test_case.expected_grpc_code,
                                                   ::testing::IsEmpty()));
  }
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponse_WithoutHttpStatus) {
  CheckResponse response;
  response.mutable_status()->set_code(7);  // PERMISSION_DENIED
  response.mutable_denied_response();
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_PERMISSION_DENIED);
  EXPECT_THAT(parsed->response, IsDeniedResponse(GRPC_STATUS_PERMISSION_DENIED,
                                                 ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest,
       DeniedResponse_WithoutHttpStatus_ExplicitGrpcStatus) {
  CheckResponse response;
  response.mutable_status()->set_code(16);  // UNAUTHENTICATED
  response.mutable_denied_response();
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_THAT(parsed->response, IsDeniedResponse(GRPC_STATUS_PERMISSION_DENIED,
                                                 ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest,
       DeniedResponse_WithoutHttpStatus_NoStatusField) {
  CheckResponse response;
  response.mutable_denied_response();
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_PERMISSION_DENIED);
  EXPECT_THAT(parsed->response, IsDeniedResponse(GRPC_STATUS_PERMISSION_DENIED,
                                                 ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest, OkResponse_WithStatusOkAndDeniedResponse) {
  CheckResponse response;
  response.mutable_status()->set_code(0);  // OK
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(envoy::type::v3::Forbidden);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_OK);
  EXPECT_THAT(
      parsed->response,
      IsOkResponse(IsHeaderMutation(::testing::IsEmpty(), ::testing::IsEmpty()),
                   ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponse_WithoutStatusField) {
  CheckResponse response;
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(envoy::type::v3::Forbidden);
  auto* h1 = denied->add_headers();
  h1->mutable_header()->set_key("x-deny-reason");
  h1->mutable_header()->set_value("bad-token");
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_PERMISSION_DENIED);
  EXPECT_THAT(
      parsed->response,
      IsDeniedResponse(
          GRPC_STATUS_PERMISSION_DENIED,
          ::testing::ElementsAre(IsHeaderValueOption(
              "x-deny-reason", "bad-token",
              XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd))));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponse_ExplicitGrpcStatus) {
  CheckResponse response;
  response.mutable_status()->set_code(16);  // UNAUTHENTICATED
  response.mutable_status()->set_message("unauthenticated rpc");
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_EQ(parsed->status_message, "unauthenticated rpc");
  EXPECT_THAT(parsed->response, IsDeniedResponse(GRPC_STATUS_PERMISSION_DENIED,
                                                 ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponse_HeaderValueOptionInvalid) {
  CheckResponse response;
  response.mutable_status()->set_code(7);
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(envoy::type::v3::Forbidden);
  auto* h1 = denied->add_headers();
  h1->mutable_header()->set_key(std::string(kKey1));
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("Failed to parse XdsHeaderValueOption: "
                                "[field:header error:either value or raw_value "
                                "must be set]"));
}

TEST_F(ParseExtAuthzResponseTest, IgnoredFieldsDoNotAffectParsing_OkResponse) {
  CheckResponse response;
  response.mutable_status()->set_code(0);
  auto* ok_resp = response.mutable_ok_response();
  auto* h1 = ok_resp->add_headers();
  h1->mutable_header()->set_key(std::string(kKey1));
  h1->mutable_header()->set_value(std::string(kVal1));
  ok_resp->add_query_parameters_to_set()->set_key("k");
  ok_resp->add_query_parameters_to_remove("remove_q");
  auto* metadata = response.mutable_dynamic_metadata();
  (*metadata->mutable_fields())["field"].set_string_value("ignored");

  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_OK);
  EXPECT_THAT(
      parsed->response,
      IsOkResponse(
          IsHeaderMutation(
              ::testing::ElementsAre(IsHeaderValueOption(
                  kKey1, kVal1,
                  XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd)),
              ::testing::IsEmpty()),
          ::testing::IsEmpty()));
}

TEST_F(ParseExtAuthzResponseTest,
       IgnoredFieldsDoNotAffectParsing_DeniedResponse) {
  CheckResponse response;
  response.mutable_status()->set_code(7);
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(envoy::type::v3::Forbidden);
  denied->set_body("ignored body text");
  auto* metadata = response.mutable_dynamic_metadata();
  (*metadata->mutable_fields())["field"].set_string_value("ignored");

  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_PERMISSION_DENIED);
  EXPECT_THAT(parsed->response, IsDeniedResponse(GRPC_STATUS_PERMISSION_DENIED,
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
