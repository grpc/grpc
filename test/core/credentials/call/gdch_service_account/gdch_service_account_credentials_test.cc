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

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include "src/core/util/http_client/parser.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

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

GDCHServiceAccountCredentials::Info CreateValidInfo() {
  GDCHServiceAccountCredentials::Info info;
  info.type = "gdch_service_account";
  info.format_version = "1";
  info.project_id = "test-project";
  info.private_key_id = "test-private-key-id";
  info.private_key = kTestPrivateKeyPem;
  info.service_identity_name = "test-name";
  info.token_uri = "https://test-token-uri.com/token";
  return info;
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

  static absl::StatusOr<GDCHServiceAccountCredentials::Info>
  ParseServiceAccountJson(const Json& json) {
    return LoadFromJson<GDCHServiceAccountCredentials::Info>(json);
  }

  static AssertionComponents AssertionComponentsFromInfo(
      const GDCHServiceAccountCredentials::Info& info, Timestamp now) {
    return GDCHServiceAccountCredentials::AssertionComponentsFromInfo(info,
                                                                      now);
  }

  static absl::StatusOr<std::string> MakeJWTAssertion(
      const std::string& header, const std::string& payload,
      const std::string& pem_contents, SignatureFormat format) {
    return GDCHServiceAccountCredentials::MakeJWTAssertion(
        header, payload, pem_contents, format);
  }

  static absl::StatusOr<std::string> CreateRequestBody(
      const GDCHServiceAccountCredentials::Info& info,
      const std::string& audience) {
    return GDCHServiceAccountCredentials::CreateRequestBody(info, audience);
  }

  static absl::StatusOr<GrpcHttpRequestUniquePtr> FormatHttpRequest(
      const GDCHServiceAccountCredentials::Info& info,
      const std::string& audience) {
    absl::StatusOr<URI> token_url = URI::Parse(info.token_uri);
    if (!token_url.ok()) return token_url.status();
    return GDCHServiceAccountCredentials::FormatHttpRequest(info, audience,
                                                            *token_url);
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
  ASSERT_TRUE(sig.ok()) << sig.status().ToString();
  EXPECT_FALSE(sig->empty());
}

TEST_F(GDCHServiceAccountCredentialsTest, SignUsingSha256RawSuccess) {
  std::string payload = "hello world";
  absl::StatusOr<std::string> sig =
      SignUsingSha256(payload, kTestPrivateKeyPem, SignatureFormat::kRaw);
  ASSERT_TRUE(sig.ok()) << sig.status().ToString();
  // For ECDSA ES256 (P-256), raw signature coordinates r and s are 32 bytes
  // each.
  EXPECT_EQ(sig->size(), 64);
}

TEST_F(GDCHServiceAccountCredentialsTest, SignUsingSha256FailureInvalidKey) {
  std::string payload = "hello world";
  absl::StatusOr<std::string> sig =
      SignUsingSha256(payload, "invalid pem content", SignatureFormat::kRaw);
  EXPECT_FALSE(sig.ok());
}

// --- Tests for ParseServiceAccountJson ---

TEST_F(GDCHServiceAccountCredentialsTest, ParseServiceAccountJsonSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<GDCHServiceAccountCredentials::Info> info =
      ParseServiceAccountJson(Json::FromObject(obj));
  ASSERT_TRUE(info.ok()) << info.status().ToString();
  EXPECT_EQ(info->type, "gdch_service_account");
  EXPECT_EQ(info->format_version, "1");
  EXPECT_EQ(info->project_id, "test-project");
  EXPECT_EQ(info->private_key_id, "test-private-key-id");
  EXPECT_EQ(info->private_key, kTestPrivateKeyPem);
  EXPECT_EQ(info->service_identity_name, "test-name");
  EXPECT_FALSE(info->ca_cert_path.has_value());
  EXPECT_EQ(info->token_uri, "https://test-token-uri.com/token");
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseServiceAccountJsonWithCaCertPathSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["ca_cert_path"] = Json::FromString("/etc/ssl/certs/ca-certificates.crt");
  absl::StatusOr<GDCHServiceAccountCredentials::Info> info =
      ParseServiceAccountJson(Json::FromObject(obj));
  ASSERT_TRUE(info.ok()) << info.status().ToString();
  EXPECT_EQ(info->type, "gdch_service_account");
  EXPECT_EQ(info->format_version, "1");
  EXPECT_EQ(info->project_id, "test-project");
  EXPECT_EQ(info->private_key_id, "test-private-key-id");
  EXPECT_EQ(info->private_key, kTestPrivateKeyPem);
  EXPECT_EQ(info->service_identity_name, "test-name");
  ASSERT_TRUE(info->ca_cert_path.has_value());
  EXPECT_EQ(*info->ca_cert_path, "/etc/ssl/certs/ca-certificates.crt");
  EXPECT_EQ(info->token_uri, "https://test-token-uri.com/token");
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseServiceAccountJsonFailureNotAnObject) {
  absl::StatusOr<GDCHServiceAccountCredentials::Info> info =
      ParseServiceAccountJson(Json::FromString("not-an-object"));
  EXPECT_FALSE(info.ok());
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseServiceAccountJsonFailureMissingRequiredFields) {
  const std::vector<std::string> required_fields = {
      "type",        "format_version", "project",  "private_key_id",
      "private_key", "name",           "token_uri"};

  for (const std::string& field : required_fields) {
    Json::Object obj = CreateValidServiceAccountObject();
    obj.erase(field);
    absl::StatusOr<GDCHServiceAccountCredentials::Info> info =
        ParseServiceAccountJson(Json::FromObject(obj));
    EXPECT_FALSE(info.ok())
        << "Expected failure when missing required field: " << field;
  }
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseServiceAccountJsonFailureEmptyRequiredFields) {
  const std::vector<std::string> required_fields = {
      "type",        "format_version", "project",  "private_key_id",
      "private_key", "name",           "token_uri"};

  for (const std::string& field : required_fields) {
    Json::Object obj = CreateValidServiceAccountObject();
    obj[field] = Json::FromString("");
    absl::StatusOr<GDCHServiceAccountCredentials::Info> info =
        ParseServiceAccountJson(Json::FromObject(obj));
    EXPECT_FALSE(info.ok())
        << "Expected failure when required field is empty: " << field;
  }
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseServiceAccountJsonFailureEmptyOptionalField) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["ca_cert_path"] = Json::FromString("");
  absl::StatusOr<GDCHServiceAccountCredentials::Info> info =
      ParseServiceAccountJson(Json::FromObject(obj));
  EXPECT_FALSE(info.ok())
      << "Expected failure when optional field ca_cert_path is empty";
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseServiceAccountJsonFailureInvalidType) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["type"] = Json::FromString("invalid_type");
  absl::StatusOr<GDCHServiceAccountCredentials::Info> info =
      ParseServiceAccountJson(Json::FromObject(obj));
  EXPECT_FALSE(info.ok());
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseServiceAccountJsonFailureInvalidFormatVersion) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["format_version"] = Json::FromString("2");
  absl::StatusOr<GDCHServiceAccountCredentials::Info> info =
      ParseServiceAccountJson(Json::FromObject(obj));
  EXPECT_FALSE(info.ok());
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseServiceAccountJsonFailureNonStringFields) {
  const std::vector<std::string> all_fields = {
      "type",        "format_version", "project",      "private_key_id",
      "private_key", "name",           "ca_cert_path", "token_uri"};

  for (const std::string& field : all_fields) {
    Json::Object obj = CreateValidServiceAccountObject();
    obj[field] = Json::FromBool(true);
    absl::StatusOr<GDCHServiceAccountCredentials::Info> info =
        ParseServiceAccountJson(Json::FromObject(obj));
    EXPECT_FALSE(info.ok())
        << "Expected failure when field has non-string type: " << field;
  }
}

// --- Tests for Create ---

TEST_F(GDCHServiceAccountCredentialsTest, CreateSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_TRUE(creds.ok()) << creds.status().ToString();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->debug_string(),
            "GDCHServiceAccountCredentials{Audience:https://my-audience.com}");
  EXPECT_EQ((*creds)->type().name(), "GDCHServiceAccountCredentials");
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateFailureInvalidJson) {
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create("not-a-valid-json",
                                            "https://my-audience.com");
  EXPECT_FALSE(creds.ok());
}

// --- Tests for AssertionComponentsFromInfo ---

TEST_F(GDCHServiceAccountCredentialsTest, AssertionComponentsFromInfoSuccess) {
  GDCHServiceAccountCredentials::Info info = CreateValidInfo();
  Timestamp now = Timestamp::FromTimespecRoundDown(
      gpr_timespec{12345678, 0, GPR_CLOCK_REALTIME});

  AssertionComponents components = AssertionComponentsFromInfo(info, now);

  absl::StatusOr<Json> parsed_header = JsonParse(components.header);
  ASSERT_TRUE(parsed_header.ok());
  ASSERT_EQ(parsed_header->type(), Json::Type::kObject);
  EXPECT_EQ(parsed_header->object().at("alg").string(), "ES256");
  EXPECT_EQ(parsed_header->object().at("typ").string(), "JWT");
  EXPECT_EQ(parsed_header->object().at("kid").string(), info.private_key_id);

  absl::StatusOr<Json> parsed_claim = JsonParse(components.claim);
  ASSERT_TRUE(parsed_claim.ok());
  ASSERT_EQ(parsed_claim->type(), Json::Type::kObject);
  EXPECT_EQ(parsed_claim->object().at("iss").string(),
            "system:serviceaccount:test-project:test-name");
  EXPECT_EQ(parsed_claim->object().at("sub").string(),
            "system:serviceaccount:test-project:test-name");
  EXPECT_EQ(parsed_claim->object().at("aud").string(), info.token_uri);
  EXPECT_EQ(parsed_claim->object().at("iat").string(), "12345678");
  // Lifetime is 3600 seconds
  EXPECT_EQ(parsed_claim->object().at("exp").string(), "12349278");
}

// --- Tests for MakeJWTAssertion ---

TEST_F(GDCHServiceAccountCredentialsTest, MakeJWTAssertionSuccess) {
  std::string header = "{\"alg\":\"ES256\",\"typ\":\"JWT\",\"kid\":\"key-id\"}";
  std::string payload =
      "{\"iss\":\"me\",\"sub\":\"me\",\"aud\":\"here\",\"iat\":100,\"exp\":"
      "3700}";

  absl::StatusOr<std::string> jwt = MakeJWTAssertion(
      header, payload, kTestPrivateKeyPem, SignatureFormat::kRaw);
  ASSERT_TRUE(jwt.ok()) << jwt.status().ToString();

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
  std::string header = "{\"alg\":\"ES256\",\"typ\":\"JWT\",\"kid\":\"key-id\"}";
  std::string payload =
      "{\"iss\":\"me\",\"sub\":\"me\",\"aud\":\"here\",\"iat\":100,\"exp\":"
      "3700}";

  absl::StatusOr<std::string> jwt = MakeJWTAssertion(
      header, payload, "invalid key pem", SignatureFormat::kRaw);
  EXPECT_FALSE(jwt.ok());
}

// --- Tests for CreateRequestBody ---

TEST_F(GDCHServiceAccountCredentialsTest, CreateRequestBodySuccess) {
  GDCHServiceAccountCredentials::Info info = CreateValidInfo();
  std::string audience = "https://my-audience.com";

  absl::StatusOr<std::string> body = CreateRequestBody(info, audience);
  ASSERT_TRUE(body.ok()) << body.status().ToString();

  absl::StatusOr<Json> parsed_body = JsonParse(*body);
  ASSERT_TRUE(parsed_body.ok());
  ASSERT_EQ(parsed_body->type(), Json::Type::kObject);

  EXPECT_EQ(parsed_body->object().at("grant_type").string(),
            "urn:ietf:params:oauth:token-type:token-exchange");
  EXPECT_EQ(parsed_body->object().at("audience").string(), audience);
  EXPECT_EQ(parsed_body->object().at("requested_token_type").string(),
            "urn:ietf:params:oauth:token-type:access_token");
  EXPECT_EQ(parsed_body->object().at("subject_token_type").string(),
            "urn:k8s:params:oauth:token-type:serviceaccount");

  std::string jwt_token = parsed_body->object().at("subject_token").string();
  std::vector<std::string> parts = absl::StrSplit(jwt_token, '.');
  ASSERT_EQ(parts.size(), 3);
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateRequestBodyFailureInvalidKey) {
  GDCHServiceAccountCredentials::Info info = CreateValidInfo();
  info.private_key = "invalid key pem";
  std::string audience = "https://my-audience.com";

  absl::StatusOr<std::string> body = CreateRequestBody(info, audience);
  EXPECT_FALSE(body.ok());
}

// --- Tests for FormatHttpRequest ---

TEST_F(GDCHServiceAccountCredentialsTest, FormatHttpRequestSuccess) {
  GDCHServiceAccountCredentials::Info info = CreateValidInfo();
  std::string audience = "https://my-audience.com";

  absl::StatusOr<GDCHServiceAccountCredentialsTest::GrpcHttpRequestUniquePtr>
      request = FormatHttpRequest(info, audience);
  ASSERT_TRUE(request.ok()) << request.status().ToString();
  ASSERT_NE(request->get(), nullptr);

  EXPECT_STREQ((*request)->path, "/token");
  EXPECT_EQ((*request)->hdr_count, 1);
  EXPECT_STREQ((*request)->hdrs[0].key, "content-type");
  EXPECT_STREQ((*request)->hdrs[0].value, "application/json");

  ASSERT_NE((*request)->body, nullptr);
  EXPECT_GT((*request)->body_length, 0);
}

TEST_F(GDCHServiceAccountCredentialsTest, FormatHttpRequestFailureInvalidKey) {
  GDCHServiceAccountCredentials::Info info = CreateValidInfo();
  info.private_key = "invalid key pem";
  std::string audience = "https://my-audience.com";

  absl::StatusOr<GDCHServiceAccountCredentialsTest::GrpcHttpRequestUniquePtr>
      request = FormatHttpRequest(info, audience);
  EXPECT_FALSE(request.ok());
}

// --- Tests for ParseHttpResponse ---

TEST_F(GDCHServiceAccountCredentialsTest, ParseHttpResponseSuccess) {
  std::string response_body = "{\"access_token\": \"test-access-token\"}";
  absl::StatusOr<std::string> token = ParseHttpResponse(response_body);
  ASSERT_TRUE(token.ok()) << token.status().ToString();
  EXPECT_EQ(*token, "test-access-token");
}

TEST_F(GDCHServiceAccountCredentialsTest, ParseHttpResponseFailureNotObject) {
  absl::StatusOr<std::string> token = ParseHttpResponse("not-a-json");
  EXPECT_FALSE(token.ok());
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseHttpResponseFailureMissingToken) {
  absl::StatusOr<std::string> token =
      ParseHttpResponse("{\"other_field\": \"value\"}");
  EXPECT_FALSE(token.ok());
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseHttpResponseFailureTokenNotString) {
  absl::StatusOr<std::string> token =
      ParseHttpResponse("{\"access_token\": 123}");
  EXPECT_FALSE(token.ok());
}

// --- Tests for ExtractToken ---

TEST_F(GDCHServiceAccountCredentialsTest, ExtractTokenSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_TRUE(creds.ok()) << creds.status().ToString();

  std::string body = "{\"access_token\": \"test-access-token\"}";
  grpc_http_response response = {};
  response.status = 200;
  response.body = const_cast<char*>(body.data());
  response.body_length = body.size();

  absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>> token =
      (*creds)->ExtractToken(response);
  ASSERT_TRUE(token.ok()) << token.status().ToString();
  ASSERT_NE(*token, nullptr);
  EXPECT_GT((*token)->ExpirationTime(), Timestamp::Now());
}

TEST_F(GDCHServiceAccountCredentialsTest, ExtractTokenFailureInvalidJson) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_TRUE(creds.ok()) << creds.status().ToString();

  std::string body = "not-a-json";
  grpc_http_response response = {};
  response.status = 200;
  response.body = const_cast<char*>(body.data());
  response.body_length = body.size();

  absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>> token =
      (*creds)->ExtractToken(response);
  EXPECT_FALSE(token.ok());
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
