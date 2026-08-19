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

#include "src/core/credentials/call/gdch_service_account/gdch_service_account_credentials.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/uri.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::NotNull;

// This JSON key was generated with the GDCH console and immediately revoked.
// The identifiers have been changed as well.
const char kTestPrivateKeyPem[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgUBPYGHx4AnG2rIxQ\n"
    "5aXWVn2g4vGR4/WVxautYTHClUehRANCAAQ8TwEYxDnaovfS6UEHo6eNykUBlC2L\n"
    "GZ6rVuvkS+5Kw8k7IOVjrzE/lvVY1lDpKmIw2w7IgRxSIdDdyN6TbxNT\n"
    "-----END PRIVATE KEY-----\n";

Json::Object CreateValidServiceAccountObject() {
  return Json::Object{
      {"type", Json::FromString("gdch_service_account")},
      {"format_version", Json::FromString("1")},
      {"project", Json::FromString("test-project")},
      {"private_key_id", Json::FromString("test-private-key-id")},
      {"private_key", Json::FromString(kTestPrivateKeyPem)},
      {"name", Json::FromString("test-name")},
      {"token_uri", Json::FromString("https://test-token-uri.com/token")},
  };
}

}  // namespace

class GDCHServiceAccountCredentialsTest : public ::testing::Test {
 protected:
  using SignatureFormat = GDCHServiceAccountCredentials::SignatureFormat;
  using AssertionComponents =
      GDCHServiceAccountCredentials::AssertionComponents;
  using GrpcHttpRequestUniquePtr =
      GDCHServiceAccountCredentials::GrpcHttpRequestUniquePtr;

  static absl::StatusOr<std::string> SignUsingSha256(
      const std::string& str, const std::string& pem_contents,
      SignatureFormat format) {
    return GDCHServiceAccountCredentials::SignUsingSha256(str, pem_contents,
                                                          format);
  }

  static AssertionComponents CreateAssertionComponents(
      const GDCHServiceAccountCredentials& creds, Timestamp now) {
    return creds.CreateAssertionComponents(now);
  }

  static absl::StatusOr<std::string> MakeJWTAssertion(
      const std::string& header, const std::string& payload,
      const std::string& pem_contents, SignatureFormat format) {
    return GDCHServiceAccountCredentials::MakeJWTAssertion(
        header, payload, pem_contents, format);
  }

  static absl::StatusOr<std::string> CreateRequestBody(
      const GDCHServiceAccountCredentials& creds) {
    return creds.CreateRequestBody();
  }

  static absl::StatusOr<GrpcHttpRequestUniquePtr> FormatHttpRequest(
      const GDCHServiceAccountCredentials& creds) {
    return creds.FormatHttpRequest();
  }

  static absl::StatusOr<std::string> ParseHttpResponse(
      absl::string_view response_body) {
    return GDCHServiceAccountCredentials::ParseHttpResponse(response_body);
  }
};

namespace {

// --- Tests for SignUsingSha256 ---

TEST_F(GDCHServiceAccountCredentialsTest, SignUsingSha256DERSuccess) {
  std::string payload = "hello world";
  absl::StatusOr<std::string> sig =
      SignUsingSha256(payload, kTestPrivateKeyPem, SignatureFormat::kDER);
  ASSERT_THAT(sig, IsOk());
  EXPECT_FALSE(sig->empty());
}

TEST_F(GDCHServiceAccountCredentialsTest, SignUsingSha256RawSuccess) {
  std::string payload = "hello world";
  absl::StatusOr<std::string> sig =
      SignUsingSha256(payload, kTestPrivateKeyPem, SignatureFormat::kRaw);
  ASSERT_THAT(sig, IsOk());
  // For ECDSA ES256 (P-256), raw signature coordinates r and s are 32 bytes
  // each.
  EXPECT_EQ(sig->size(), 64);
}

TEST_F(GDCHServiceAccountCredentialsTest, SignUsingSha256FailureInvalidKey) {
  std::string payload = "hello world";
  absl::StatusOr<std::string> sig =
      SignUsingSha256(payload, "invalid pem content", SignatureFormat::kRaw);
  EXPECT_THAT(sig, StatusIs(absl::StatusCode::kInternal,
                            HasSubstr("Invalid ServiceAccountCredentials could "
                                      "not parse PEM to get private key:")));
}

// --- Tests for Create (JSON Validation) ---

TEST_F(GDCHServiceAccountCredentialsTest, CreateSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_THAT(creds, IsOkAndHolds(NotNull()));
  EXPECT_FALSE((*creds)->ca_cert_path().has_value());
  EXPECT_EQ((*creds)->debug_string(),
            "GDCHServiceAccountCredentials{Audience:https://my-audience.com}");
  EXPECT_EQ((*creds)->type().name(), "GDCHServiceAccountCredentials");
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateWithCaCertPathSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["ca_cert_path"] = Json::FromString("/etc/ssl/certs/ca-certificates.crt");
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_THAT(creds, IsOkAndHolds(NotNull()));
  ASSERT_TRUE((*creds)->ca_cert_path().has_value());
  EXPECT_EQ(*(*creds)->ca_cert_path(), "/etc/ssl/certs/ca-certificates.crt");
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateFailureInvalidJson) {
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create("not-a-valid-json",
                                            "https://my-audience.com");
  EXPECT_THAT(creds,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "JSON parsing failed: [JSON parse error at index 1]"));
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateFailureNotAnObject) {
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create("\"not-an-object\"",
                                            "https://my-audience.com");
  EXPECT_THAT(
      creds,
      StatusIs(absl::StatusCode::kInvalidArgument,
               "errors validating JSON: [field: error:is not an object]"));
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateFailureMissingRequiredFields) {
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create("{}", "https://my-audience.com");
  EXPECT_THAT(creds, StatusIs(absl::StatusCode::kInvalidArgument,
                              "errors validating JSON: ["
                              "field:format_version error:field not present; "
                              "field:name error:field not present; "
                              "field:private_key error:field not present; "
                              "field:private_key_id error:field not present; "
                              "field:project error:field not present; "
                              "field:token_uri error:field not present; "
                              "field:type error:field not present]"));
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateFailureEmptyRequiredFields) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["project"] = Json::FromString("");
  obj["private_key_id"] = Json::FromString("");
  obj["private_key"] = Json::FromString("");
  obj["name"] = Json::FromString("");
  obj["token_uri"] = Json::FromString("");
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  EXPECT_THAT(creds,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "errors validating JSON: ["
                       "field:name error:field must not be empty; "
                       "field:private_key error:field must not be empty; "
                       "field:private_key_id error:field must not be empty; "
                       "field:project error:field must not be empty; "
                       "field:token_uri error:field must not be empty]"));
}

TEST_F(GDCHServiceAccountCredentialsTest,
       CreateFailureEmptyTypeAndFormatVersion) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["type"] = Json::FromString("");
  obj["format_version"] = Json::FromString("");
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  EXPECT_THAT(creds,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "errors validating JSON: ["
                       "field:format_version error:field must be 1; "
                       "field:type error:field must be gdch_service_account]"));
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateFailureEmptyCaCertPath) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["ca_cert_path"] = Json::FromString("");
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  EXPECT_THAT(creds,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "errors validating JSON: ["
                       "field:ca_cert_path error:field must not be empty]"));
}

TEST_F(GDCHServiceAccountCredentialsTest,
       CreateFailureInvalidTypeAndFormatVersion) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["type"] = Json::FromString("invalid_type");
  obj["format_version"] = Json::FromString("2");
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  EXPECT_THAT(creds,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "errors validating JSON: ["
                       "field:format_version error:field must be 1; "
                       "field:type error:field must be gdch_service_account]"));
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateFailureNonStringFields) {
  Json::Object obj = {
      {"type", Json::FromBool(true)},
      {"format_version", Json::FromBool(true)},
      {"project", Json::FromBool(true)},
      {"private_key_id", Json::FromBool(true)},
      {"private_key", Json::FromBool(true)},
      {"name", Json::FromBool(true)},
      {"ca_cert_path", Json::FromBool(true)},
      {"token_uri", Json::FromBool(true)},
  };
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  EXPECT_THAT(creds, StatusIs(absl::StatusCode::kInvalidArgument,
                              "errors validating JSON: ["
                              "field:ca_cert_path error:is not a string; "
                              "field:format_version error:is not a string; "
                              "field:name error:is not a string; "
                              "field:private_key error:is not a string; "
                              "field:private_key_id error:is not a string; "
                              "field:project error:is not a string; "
                              "field:token_uri error:is not a string; "
                              "field:type error:is not a string]"));
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateFailureInvalidTokenUri) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["token_uri"] = Json::FromString(":no_scheme");
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  EXPECT_THAT(creds, StatusIs(absl::StatusCode::kInvalidArgument,
                              "Could not parse 'scheme' from uri ':no_scheme'. "
                              "Scheme not found."));
}

// --- Tests for CreateAssertionComponents ---

TEST_F(GDCHServiceAccountCredentialsTest, CreateAssertionComponentsSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_THAT(creds, IsOk());
  Timestamp now = Timestamp::FromTimespecRoundDown(
      gpr_timespec{12345678, 0, GPR_CLOCK_REALTIME});

  AssertionComponents components = CreateAssertionComponents(**creds, now);

  EXPECT_EQ(components.header,
            JsonDump(Json::FromObject(Json::Object{
                {"alg", Json::FromString("ES256")},
                {"kid", Json::FromString("test-private-key-id")},
                {"typ", Json::FromString("JWT")},
            })));
  EXPECT_EQ(
      components.claim,
      JsonDump(Json::FromObject(Json::Object{
          {"aud", Json::FromString("https://test-token-uri.com/token")},
          {"exp", Json::FromNumber(12349278)},
          {"iat", Json::FromNumber(12345678)},
          {"iss",
           Json::FromString("system:serviceaccount:test-project:test-name")},
          {"sub",
           Json::FromString("system:serviceaccount:test-project:test-name")},
      })));
}

// --- Tests for MakeJWTAssertion ---

TEST_F(GDCHServiceAccountCredentialsTest, MakeJWTAssertionSuccess) {
  std::string header = JsonDump(Json::FromObject(Json::Object{
      {"alg", Json::FromString("ES256")},
      {"typ", Json::FromString("JWT")},
      {"kid", Json::FromString("key-id")},
  }));
  std::string payload = JsonDump(Json::FromObject(Json::Object{
      {"iss", Json::FromString("me")},
      {"sub", Json::FromString("me")},
      {"aud", Json::FromString("here")},
      {"iat", Json::FromString("100")},
      {"exp", Json::FromString("3700")},
  }));

  absl::StatusOr<std::string> jwt = MakeJWTAssertion(
      header, payload, kTestPrivateKeyPem, SignatureFormat::kRaw);
  ASSERT_THAT(jwt, IsOk());

  // JWT consists of three parts separated by dots.
  std::vector<std::string> parts = absl::StrSplit(*jwt, '.');
  ASSERT_EQ(parts.size(), 3);

  std::string decoded_header;
  ASSERT_TRUE(absl::WebSafeBase64Unescape(parts[0], &decoded_header));
  EXPECT_EQ(decoded_header, header);

  std::string decoded_payload;
  ASSERT_TRUE(absl::WebSafeBase64Unescape(parts[1], &decoded_payload));
  EXPECT_EQ(decoded_payload, payload);

  std::string decoded_signature;
  ASSERT_TRUE(absl::WebSafeBase64Unescape(parts[2], &decoded_signature));
  EXPECT_EQ(decoded_signature.size(), 64);
}

TEST_F(GDCHServiceAccountCredentialsTest, MakeJWTAssertionFailureInvalidKey) {
  std::string header = JsonDump(Json::FromObject(Json::Object{
      {"alg", Json::FromString("ES256")},
      {"typ", Json::FromString("JWT")},
      {"kid", Json::FromString("key-id")},
  }));
  std::string payload = JsonDump(Json::FromObject(Json::Object{
      {"iss", Json::FromString("me")},
      {"sub", Json::FromString("me")},
      {"aud", Json::FromString("here")},
      {"iat", Json::FromString("100")},
      {"exp", Json::FromString("3700")},
  }));

  absl::StatusOr<std::string> jwt = MakeJWTAssertion(
      header, payload, "invalid key pem", SignatureFormat::kRaw);
  EXPECT_THAT(jwt, StatusIs(absl::StatusCode::kInternal,
                            HasSubstr("Invalid ServiceAccountCredentials could "
                                      "not parse PEM to get private key:")));
}

// --- Tests for CreateRequestBody ---

TEST_F(GDCHServiceAccountCredentialsTest, CreateRequestBodySuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_THAT(creds, IsOk());

  absl::StatusOr<std::string> body = CreateRequestBody(**creds);
  ASSERT_THAT(body, IsOk());

  absl::StatusOr<Json> parsed_body = JsonParse(*body);
  ASSERT_THAT(parsed_body, IsOk());
  ASSERT_EQ(parsed_body->type(), Json::Type::kObject);
  auto it = parsed_body->object().find("subject_token");
  ASSERT_NE(it, parsed_body->object().end());
  ASSERT_EQ(it->second.type(), Json::Type::kString);
  std::string jwt_token = it->second.string();

  EXPECT_EQ(
      *body,
      JsonDump(Json::FromObject(Json::Object{
          {"audience", Json::FromString("https://my-audience.com")},
          {"grant_type", Json::FromString("urn:ietf:params:oauth:token-type:"
                                          "token-exchange")},
          {"requested_token_type",
           Json::FromString("urn:ietf:params:oauth:token-type:"
                            "access_token")},
          {"subject_token", Json::FromString(jwt_token)},
          {"subject_token_type",
           Json::FromString("urn:k8s:params:oauth:token-type:"
                            "serviceaccount")},
      })));

  std::vector<std::string> parts = absl::StrSplit(jwt_token, '.');
  ASSERT_EQ(parts.size(), 3);
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateRequestBodyFailureInvalidKey) {
  absl::StatusOr<URI> token_url =
      URI::Parse("https://test-token-uri.com/token");
  ASSERT_THAT(token_url, IsOk());
  GDCHServiceAccountCredentials creds(
      "test-private-key-id", "invalid key pem",
      "system:serviceaccount:test-project:test-name",
      /*ca_cert_path=*/std::nullopt, *std::move(token_url),
      "https://my-audience.com");
  absl::StatusOr<std::string> body = CreateRequestBody(creds);
  EXPECT_THAT(body,
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Invalid ServiceAccountCredentials could not "
                                 "parse PEM to get private key:")));
}

// --- Tests for FormatHttpRequest ---

TEST_F(GDCHServiceAccountCredentialsTest, FormatHttpRequestSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_THAT(creds, IsOk());

  absl::StatusOr<GDCHServiceAccountCredentialsTest::GrpcHttpRequestUniquePtr>
      request = FormatHttpRequest(**creds);
  ASSERT_THAT(request, IsOkAndHolds(NotNull()));

  EXPECT_STREQ((*request)->path, "/token");
  EXPECT_EQ((*request)->hdr_count, 1);
  EXPECT_STREQ((*request)->hdrs[0].key, "content-type");
  EXPECT_STREQ((*request)->hdrs[0].value, "application/json");

  ASSERT_NE((*request)->body, nullptr);
  EXPECT_GT((*request)->body_length, 0);
}

TEST_F(GDCHServiceAccountCredentialsTest, FormatHttpRequestFailureInvalidKey) {
  absl::StatusOr<URI> token_url =
      URI::Parse("https://test-token-uri.com/token");
  ASSERT_THAT(token_url, IsOk());
  GDCHServiceAccountCredentials creds(
      "test-private-key-id", "invalid key pem",
      "system:serviceaccount:test-project:test-name",
      /*ca_cert_path=*/std::nullopt, *std::move(token_url),
      "https://my-audience.com");
  absl::StatusOr<GDCHServiceAccountCredentialsTest::GrpcHttpRequestUniquePtr>
      request = FormatHttpRequest(creds);
  EXPECT_THAT(request,
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Invalid ServiceAccountCredentials could not "
                                 "parse PEM to get private key:")));
}

// --- Tests for ParseHttpResponse ---

TEST_F(GDCHServiceAccountCredentialsTest, ParseHttpResponseSuccess) {
  std::string response_body = JsonDump(Json::FromObject(Json::Object{
      {"access_token", Json::FromString("test-access-token")},
  }));
  absl::StatusOr<std::string> token = ParseHttpResponse(response_body);
  ASSERT_THAT(token, IsOkAndHolds("test-access-token"));
}

TEST_F(GDCHServiceAccountCredentialsTest, ParseHttpResponseFailureNotObject) {
  absl::StatusOr<std::string> token = ParseHttpResponse("not-a-json");
  EXPECT_THAT(token,
              StatusIs(absl::StatusCode::kInternal,
                       "The format of response is not a valid json object."));
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseHttpResponseFailureMissingToken) {
  std::string response_body = JsonDump(Json::FromObject(Json::Object{
      {"other_field", Json::FromString("value")},
  }));
  absl::StatusOr<std::string> token = ParseHttpResponse(response_body);
  EXPECT_THAT(token, StatusIs(absl::StatusCode::kInvalidArgument,
                              "errors validating JSON: ["
                              "field:access_token error:field not present]"));
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseHttpResponseFailureTokenNotString) {
  std::string response_body = JsonDump(Json::FromObject(Json::Object{
      {"access_token", Json::FromNumber(123)},
  }));
  absl::StatusOr<std::string> token = ParseHttpResponse(response_body);
  EXPECT_THAT(token, StatusIs(absl::StatusCode::kInvalidArgument,
                              "errors validating JSON: ["
                              "field:access_token error:is not a string]"));
}

// --- Tests for ExtractToken ---

TEST_F(GDCHServiceAccountCredentialsTest, ExtractTokenSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_THAT(creds, IsOk());

  std::string body = JsonDump(Json::FromObject(Json::Object{
      {"access_token", Json::FromString("test-access-token")},
  }));
  grpc_http_response response = {};
  response.status = 200;
  response.body = const_cast<char*>(body.data());
  response.body_length = body.size();

  absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>> token =
      (*creds)->ExtractToken(response);
  ASSERT_THAT(token, IsOkAndHolds(NotNull()));
  EXPECT_GT((*token)->ExpirationTime(), Timestamp::Now());
}

TEST_F(GDCHServiceAccountCredentialsTest, ExtractTokenFailureInvalidJson) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_THAT(creds, IsOk());

  std::string body = "not-a-json";
  grpc_http_response response = {};
  response.status = 200;
  response.body = const_cast<char*>(body.data());
  response.body_length = body.size();

  absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>> token =
      (*creds)->ExtractToken(response);
  EXPECT_THAT(token,
              StatusIs(absl::StatusCode::kInternal,
                       "The format of response is not a valid json object."));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
