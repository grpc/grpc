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

#include "src/core/credentials/call/external/gdch_service_account_credentials.h"

#include <grpc/credentials.h>
#include <grpc/grpc_security.h>
#include <grpc/support/time.h>

#include <string>
#include <vector>

#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_split.h"

namespace grpc_core {
namespace {

// Valid EC P-256 private key generated for testing purposes.
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

// --- Tests for SignUsingSha256 ---

TEST(GDCHServiceAccountCredentialsTest, SignUsingSha256DERSuccess) {
  std::string payload = "hello world";
  auto sig = GDCHServiceAccountCredentials::SignUsingSha256(
      payload, kTestPrivateKeyPem,
      GDCHServiceAccountCredentials::SignatureFormat::kDER);
  ASSERT_TRUE(sig.ok()) << sig.status().ToString();
  EXPECT_FALSE(sig->empty());
}

TEST(GDCHServiceAccountCredentialsTest, SignUsingSha256RawSuccess) {
  std::string payload = "hello world";
  auto sig = GDCHServiceAccountCredentials::SignUsingSha256(
      payload, kTestPrivateKeyPem,
      GDCHServiceAccountCredentials::SignatureFormat::kRaw);
  ASSERT_TRUE(sig.ok()) << sig.status().ToString();
  // For ECDSA ES256 (P-256), raw signature coordinates r and s are 32 bytes
  // each.
  EXPECT_EQ(sig->size(), 64);
}

TEST(GDCHServiceAccountCredentialsTest, SignUsingSha256FailureInvalidKey) {
  std::string payload = "hello world";
  auto sig = GDCHServiceAccountCredentials::SignUsingSha256(
      payload, "invalid pem content",
      GDCHServiceAccountCredentials::SignatureFormat::kRaw);
  EXPECT_FALSE(sig.ok());
}

// --- Tests for ParseServiceAccountJson ---

TEST(GDCHServiceAccountCredentialsTest, ParseServiceAccountJsonSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  auto info = GDCHServiceAccountCredentials::ParseServiceAccountJson(
      Json::FromObject(obj));
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

TEST(GDCHServiceAccountCredentialsTest,
     ParseServiceAccountJsonWithCaCertPathSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["ca_cert_path"] = Json::FromString("/etc/ssl/certs/ca-certificates.crt");
  auto info = GDCHServiceAccountCredentials::ParseServiceAccountJson(
      Json::FromObject(obj));
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

TEST(GDCHServiceAccountCredentialsTest,
     ParseServiceAccountJsonFailureNotAnObject) {
  auto info = GDCHServiceAccountCredentials::ParseServiceAccountJson(
      Json::FromString("not-an-object"));
  EXPECT_FALSE(info.ok());
}

TEST(GDCHServiceAccountCredentialsTest,
     ParseServiceAccountJsonFailureMissingRequiredFields) {
  const std::vector<std::string> required_fields = {
      "type",        "format_version", "project",  "private_key_id",
      "private_key", "name",           "token_uri"};

  for (const auto& field : required_fields) {
    Json::Object obj = CreateValidServiceAccountObject();
    obj.erase(field);
    auto info = GDCHServiceAccountCredentials::ParseServiceAccountJson(
        Json::FromObject(obj));
    EXPECT_FALSE(info.ok())
        << "Expected failure when missing required field: " << field;
  }
}

TEST(GDCHServiceAccountCredentialsTest,
     ParseServiceAccountJsonFailureEmptyRequiredFields) {
  const std::vector<std::string> required_fields = {
      "type",        "format_version", "project",  "private_key_id",
      "private_key", "name",           "token_uri"};

  for (const auto& field : required_fields) {
    Json::Object obj = CreateValidServiceAccountObject();
    obj[field] = Json::FromString("");
    auto info = GDCHServiceAccountCredentials::ParseServiceAccountJson(
        Json::FromObject(obj));
    EXPECT_FALSE(info.ok())
        << "Expected failure when required field is empty: " << field;
  }
}

TEST(GDCHServiceAccountCredentialsTest,
     ParseServiceAccountJsonFailureEmptyOptionalField) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["ca_cert_path"] = Json::FromString("");
  auto info = GDCHServiceAccountCredentials::ParseServiceAccountJson(
      Json::FromObject(obj));
  EXPECT_FALSE(info.ok())
      << "Expected failure when optional field ca_cert_path is empty";
}

TEST(GDCHServiceAccountCredentialsTest,
     ParseServiceAccountJsonFailureInvalidType) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["type"] = Json::FromString("invalid_type");
  auto info = GDCHServiceAccountCredentials::ParseServiceAccountJson(
      Json::FromObject(obj));
  EXPECT_FALSE(info.ok());
}

TEST(GDCHServiceAccountCredentialsTest,
     ParseServiceAccountJsonFailureInvalidFormatVersion) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["format_version"] = Json::FromString("2");
  auto info = GDCHServiceAccountCredentials::ParseServiceAccountJson(
      Json::FromObject(obj));
  EXPECT_FALSE(info.ok());
}

TEST(GDCHServiceAccountCredentialsTest,
     ParseServiceAccountJsonFailureNonStringFields) {
  const std::vector<std::string> all_fields = {
      "type",        "format_version", "project",      "private_key_id",
      "private_key", "name",           "ca_cert_path", "token_uri"};

  for (const auto& field : all_fields) {
    Json::Object obj = CreateValidServiceAccountObject();
    obj[field] = Json::FromBool(true);
    auto info = GDCHServiceAccountCredentials::ParseServiceAccountJson(
        Json::FromObject(obj));
    EXPECT_FALSE(info.ok())
        << "Expected failure when field has non-string type: " << field;
  }
}

// --- Tests for Create ---

TEST(GDCHServiceAccountCredentialsTest, CreateSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  auto creds = GDCHServiceAccountCredentials::Create(Json::FromObject(obj),
                                                     "https://my-audience.com");
  ASSERT_TRUE(creds.ok()) << creds.status().ToString();
  ASSERT_NE(*creds, nullptr);
  EXPECT_EQ((*creds)->debug_string(),
            "GDCHServiceAccountCredentials{Audience:}");
}

TEST(GDCHServiceAccountCredentialsTest, CreateFailureInvalidJson) {
  auto creds = GDCHServiceAccountCredentials::Create(
      Json::FromString("not-an-object"), "https://my-audience.com");
  EXPECT_FALSE(creds.ok());
}

// --- Tests for AssertionComponentsFromInfo ---

TEST(GDCHServiceAccountCredentialsTest, AssertionComponentsFromInfoSuccess) {
  auto info = CreateValidInfo();
  auto now = std::chrono::system_clock::from_time_t(12345678);

  auto components =
      GDCHServiceAccountCredentials::AssertionComponentsFromInfo(info, now);

  auto parsed_header = JsonParse(components.header);
  ASSERT_TRUE(parsed_header.ok());
  ASSERT_EQ(parsed_header->type(), Json::Type::kObject);
  EXPECT_EQ(parsed_header->object().at("alg").string(), "ES256");
  EXPECT_EQ(parsed_header->object().at("typ").string(), "JWT");
  EXPECT_EQ(parsed_header->object().at("kid").string(), info.private_key_id);

  auto parsed_claim = JsonParse(components.claim);
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

TEST(GDCHServiceAccountCredentialsTest, MakeJWTAssertionSuccess) {
  std::string header = "{\"alg\":\"ES256\",\"typ\":\"JWT\",\"kid\":\"key-id\"}";
  std::string payload =
      "{\"iss\":\"me\",\"sub\":\"me\",\"aud\":\"here\",\"iat\":100,\"exp\":"
      "3700}";

  auto jwt = GDCHServiceAccountCredentials::MakeJWTAssertion(
      header, payload, kTestPrivateKeyPem,
      GDCHServiceAccountCredentials::SignatureFormat::kRaw);
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

TEST(GDCHServiceAccountCredentialsTest, MakeJWTAssertionFailureInvalidKey) {
  std::string header = "{\"alg\":\"ES256\",\"typ\":\"JWT\",\"kid\":\"key-id\"}";
  std::string payload =
      "{\"iss\":\"me\",\"sub\":\"me\",\"aud\":\"here\",\"iat\":100,\"exp\":"
      "3700}";

  auto jwt = GDCHServiceAccountCredentials::MakeJWTAssertion(
      header, payload, "invalid key pem",
      GDCHServiceAccountCredentials::SignatureFormat::kRaw);
  EXPECT_FALSE(jwt.ok());
}

// --- Tests for CreateRequestBody ---

TEST(GDCHServiceAccountCredentialsTest, CreateRequestBodySuccess) {
  auto info = CreateValidInfo();
  std::string audience = "https://my-audience.com";

  auto body = GDCHServiceAccountCredentials::CreateRequestBody(info, audience);
  ASSERT_TRUE(body.ok()) << body.status().ToString();

  auto parsed_body = JsonParse(*body);
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

TEST(GDCHServiceAccountCredentialsTest, CreateRequestBodyFailureInvalidKey) {
  auto info = CreateValidInfo();
  info.private_key = "invalid key pem";
  std::string audience = "https://my-audience.com";

  auto body = GDCHServiceAccountCredentials::CreateRequestBody(info, audience);
  EXPECT_FALSE(body.ok());
}

// --- Tests for FormatHttpRequest ---

TEST(GDCHServiceAccountCredentialsTest, FormatHttpRequestSuccess) {
  auto info = CreateValidInfo();
  std::string audience = "https://my-audience.com";

  auto request =
      GDCHServiceAccountCredentials::FormatHttpRequest(info, audience);
  ASSERT_TRUE(request.ok()) << request.status().ToString();
  ASSERT_NE(request->get(), nullptr);

  EXPECT_STREQ((*request)->path, "/token");
  EXPECT_EQ((*request)->hdr_count, 1);
  EXPECT_STREQ((*request)->hdrs[0].key, "content-type");
  EXPECT_STREQ((*request)->hdrs[0].value, "application/json");

  ASSERT_NE((*request)->body, nullptr);
  EXPECT_GT((*request)->body_length, 0);
}

TEST(GDCHServiceAccountCredentialsTest, FormatHttpRequestFailureInvalidKey) {
  auto info = CreateValidInfo();
  info.private_key = "invalid key pem";
  std::string audience = "https://my-audience.com";

  auto request =
      GDCHServiceAccountCredentials::FormatHttpRequest(info, audience);
  EXPECT_FALSE(request.ok());
}

// --- Tests for ParseHttpResponse ---

TEST(GDCHServiceAccountCredentialsTest, ParseHttpResponseSuccess) {
  std::string response_body = "{\"access_token\": \"test-access-token\"}";
  auto token = GDCHServiceAccountCredentials::ParseHttpResponse(response_body);
  ASSERT_TRUE(token.ok()) << token.status().ToString();
  EXPECT_EQ(*token, "test-access-token");
}

TEST(GDCHServiceAccountCredentialsTest, ParseHttpResponseFailureNotObject) {
  auto token = GDCHServiceAccountCredentials::ParseHttpResponse("not-a-json");
  EXPECT_FALSE(token.ok());
}

TEST(GDCHServiceAccountCredentialsTest, ParseHttpResponseFailureMissingToken) {
  auto token = GDCHServiceAccountCredentials::ParseHttpResponse(
      "{\"other_field\": \"value\"}");
  EXPECT_FALSE(token.ok());
}

TEST(GDCHServiceAccountCredentialsTest,
     ParseHttpResponseFailureTokenNotString) {
  auto token = GDCHServiceAccountCredentials::ParseHttpResponse(
      "{\"access_token\": 123}");
  EXPECT_FALSE(token.ok());
}

// --- Tests for Type ---

TEST(GDCHServiceAccountCredentialsTest, TypeSuccess) {
  EXPECT_EQ(GDCHServiceAccountCredentials::Type().name(),
            "GDCHServiceAccountCredentials");
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
