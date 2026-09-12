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
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v3/attribute_context.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"
#include "google/rpc/status.pb.h"
#include "src/core/ext/filters/ext_authz/ext_authz_messages.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/matchers.h"
#include "src/core/util/time.h"
#include "test/core/test_util/test_config.h"

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
  EXPECT_THAT(http.method(), ::testing::StrEq("POST"));
  EXPECT_THAT(http.path(), ::testing::StrEq(kPath));
  EXPECT_THAT(http.protocol(), ::testing::StrEq("HTTP/2"));
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
      ::testing::ElementsAre(
          IsHeaderValue(kCustomTextKey, kCustomTextVal),
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
      StringMatcher::Create(StringMatcher::Type::kPrefix, kAllowPrefix,
                            false)
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
              ::testing::UnorderedElementsAre(
                  IsHeaderValue(kPathHeader, kPath),
                  IsHeaderValue(kKey1, kVal1),
                  IsHeaderValue(kKey2, kVal2)));
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

TEST_F(ParseExtAuthzResponseTest, ResponseInvalidMalformedProtobuf) {
  auto parsed = ExtAuthzResponse::Parse("\x0a\xff");
  EXPECT_THAT(parsed.status(),
              StatusIs(absl::StatusCode::kInternal,
                       ::testing::StrEq("Failed to parse CheckResponse")));
}

TEST_F(ParseExtAuthzResponseTest, ResponseInvalidEmptyPayload) {
  auto parsed = ExtAuthzResponse::Parse("");
  EXPECT_THAT(
      parsed.status(),
      StatusIs(absl::StatusCode::kInternal,
               ::testing::StrEq("status not present in CheckResponse")));
}

TEST_F(ParseExtAuthzResponseTest, ResponseInvalidMissingStatusField) {
  CheckResponse response;
  response.mutable_denied_response();
  auto parsed = ParseResponse(response);
  EXPECT_THAT(
      parsed.status(),
      StatusIs(absl::StatusCode::kInternal,
               ::testing::StrEq("status not present in CheckResponse")));
}

TEST_F(ParseExtAuthzResponseTest, ResponseInvalidInvalidStatusCode) {
  CheckResponse response;
  response.mutable_status()->set_code(99);
  auto parsed = ParseResponse(response);
  EXPECT_THAT(
      parsed.status(),
      StatusIs(absl::StatusCode::kInternal,
               ::testing::StrEq(
                   "Invalid grpc status code in CheckResponse status: 99")));
}

TEST_F(ParseExtAuthzResponseTest, ResponseInvalidStatusOkMissingOkResponse) {
  CheckResponse response;
  response.mutable_status()->set_code(0);
  auto parsed = ParseResponse(response);
  EXPECT_THAT(
      parsed.status(),
      StatusIs(absl::StatusCode::kInternal,
               ::testing::StrEq("ok_response not present in CheckResponse")));
}

TEST_F(ParseExtAuthzResponseTest,
       ResponseInvalidOkResponseHeaderOptionMissingValue) {
  CheckResponse response;
  response.mutable_status()->set_code(0);
  auto* header = response.mutable_ok_response()->add_headers();
  header->mutable_header()->set_key(std::string(kKey1));
  auto parsed = ParseResponse(response);
  EXPECT_THAT(
      parsed.status(),
      StatusIs(absl::StatusCode::kInternal,
               ::testing::HasSubstr("either value or raw_value must be set")));
}

TEST_F(ParseExtAuthzResponseTest, OkResponseWithoutHeaders) {
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

TEST_F(ParseExtAuthzResponseTest, OkResponseWithHeaderMutations) {
  CheckResponse response;
  response.mutable_status()->set_code(0);
  response.mutable_status()->set_message("all good");
  auto* ok_response = response.mutable_ok_response();
  *ok_response->add_headers() = CreateHeaderValueOption(
      kKey1, kVal1, HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
  *ok_response->add_headers() = CreateRawHeaderValueOption(
      kKey2, kVal2, HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  ok_response->add_headers_to_remove(std::string(kKey3));
  *ok_response->add_response_headers_to_add() =
      CreateHeaderValueOption("x-resp-1", "val-resp-1");
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << "status code: " << parsed.status().code()
                           << ", message: " << parsed.status().message();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_OK));
  EXPECT_THAT(parsed->status_message, ::testing::StrEq("all good"));
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

TEST_F(ParseExtAuthzResponseTest,
       ResponseInvalidStatusNotOkMissingDeniedResponse) {
  CheckResponse response;
  response.mutable_status()->set_code(16);
  auto parsed = ParseResponse(response);
  EXPECT_THAT(parsed.status(),
              StatusIs(absl::StatusCode::kInternal,
                       ::testing::StrEq(
                           "denied_response not present in CheckResponse")));
}

TEST_F(ParseExtAuthzResponseTest,
       ResponseInvalidDeniedResponseHeaderOptionMissingValue) {
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
               ::testing::HasSubstr("either value or raw_value must be set")));
}

TEST_F(ParseExtAuthzResponseTest,
       DeniedResponseWithoutHttpStatusDefaultsToPermissionDenied) {
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

TEST_F(ParseExtAuthzResponseTest, DeniedResponseWithHeaders) {
  CheckResponse response;
  response.mutable_status()->set_code(7);
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(envoy::type::v3::Forbidden);
  *denied->add_headers() =
      CreateHeaderValueOption("x-deny-reason", "bad-token");
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
              "x-deny-reason", "bad-token",
              XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd))));
}

TEST_F(ParseExtAuthzResponseTest, DeniedResponsePreservesStatusCodeAndMessage) {
  CheckResponse response;
  response.mutable_status()->set_code(16);
  response.mutable_status()->set_message("unauthenticated rpc");
  response.mutable_denied_response();
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << "status code: " << parsed.status().code()
                           << ", message: " << parsed.status().message();
  EXPECT_THAT(parsed->status_code, ::testing::Eq(GRPC_STATUS_UNAUTHENTICATED));
  EXPECT_THAT(parsed->status_message, ::testing::StrEq("unauthenticated rpc"));
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
