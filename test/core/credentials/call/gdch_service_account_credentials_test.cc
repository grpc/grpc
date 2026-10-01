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

#include <grpc/support/time.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/uri.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

namespace grpc_core {
namespace {

using ::testing::HasSubstr;

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
  ASSERT_TRUE(sig.ok()) << sig.status();
  EXPECT_FALSE(sig->empty());
}

TEST_F(GDCHServiceAccountCredentialsTest, SignUsingSha256RawSuccess) {
  std::string payload = "hello world";
  absl::StatusOr<std::string> sig =
      SignUsingSha256(payload, kTestPrivateKeyPem, SignatureFormat::kRaw);
  ASSERT_TRUE(sig.ok()) << sig.status();
  // For ECDSA ES256 (P-256), raw signature coordinates r and s are 32 bytes
  // each.
  EXPECT_EQ(sig->size(), 64);
}

TEST_F(GDCHServiceAccountCredentialsTest, SignUsingSha256FailureInvalidKey) {
  std::string payload = "hello world";
  absl::StatusOr<std::string> sig =
      SignUsingSha256(payload, "invalid pem content", SignatureFormat::kRaw);
  EXPECT_EQ(sig.status().code(), absl::StatusCode::kInternal);
  EXPECT_THAT(sig.status().message(),
              HasSubstr("Invalid ServiceAccountCredentials could "
                        "not parse PEM to get private key:"));
}

// --- Tests for Create (JSON Validation) ---

TEST_F(GDCHServiceAccountCredentialsTest, CreateSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
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
  ASSERT_TRUE(creds.ok()) << creds.status();
  ASSERT_NE(*creds, nullptr);
  ASSERT_TRUE((*creds)->ca_cert_path().has_value());
  EXPECT_EQ(*(*creds)->ca_cert_path(), "/etc/ssl/certs/ca-certificates.crt");
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateFailureInvalidJson) {
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create("not-a-valid-json",
                                            "https://my-audience.com");
  EXPECT_EQ(creds.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(creds.status().message(),
            "JSON parsing failed: [JSON parse error at index 1]");
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateFailureNotAnObject) {
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create("\"not-an-object\"",
                                            "https://my-audience.com");
  EXPECT_EQ(creds.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(creds.status().message(),
            "errors validating JSON: [field: error:is not an object]");
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateFailureMissingRequiredFields) {
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create("{}", "https://my-audience.com");
  EXPECT_EQ(creds.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(creds.status().message(),
            "errors validating JSON: ["
            "field:format_version error:field not present; "
            "field:name error:field not present; "
            "field:private_key error:field not present; "
            "field:private_key_id error:field not present; "
            "field:project error:field not present; "
            "field:token_uri error:field not present; "
            "field:type error:field not present]");
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
  EXPECT_EQ(creds.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(creds.status().message(),
            "errors validating JSON: ["
            "field:name error:field must not be empty; "
            "field:private_key error:field must not be empty; "
            "field:private_key_id error:field must not be empty; "
            "field:project error:field must not be empty; "
            "field:token_uri error:field must not be empty]");
}

TEST_F(GDCHServiceAccountCredentialsTest,
       CreateFailureEmptyTypeAndFormatVersion) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["type"] = Json::FromString("");
  obj["format_version"] = Json::FromString("");
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  EXPECT_EQ(creds.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(creds.status().message(),
            "errors validating JSON: ["
            "field:format_version error:field must be 1; "
            "field:type error:field must be gdch_service_account]");
}

TEST_F(GDCHServiceAccountCredentialsTest,
       CreateFailureInvalidTypeAndFormatVersion) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["type"] = Json::FromString("invalid_type");
  obj["format_version"] = Json::FromString("2");
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  EXPECT_EQ(creds.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(creds.status().message(),
            "errors validating JSON: ["
            "field:format_version error:field must be 1; "
            "field:type error:field must be gdch_service_account]");
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
  EXPECT_EQ(creds.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(creds.status().message(),
            "errors validating JSON: ["
            "field:ca_cert_path error:is not a string; "
            "field:format_version error:is not a string; "
            "field:name error:is not a string; "
            "field:private_key error:is not a string; "
            "field:private_key_id error:is not a string; "
            "field:project error:is not a string; "
            "field:token_uri error:is not a string; "
            "field:type error:is not a string]");
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateFailureInvalidTokenUri) {
  Json::Object obj = CreateValidServiceAccountObject();
  obj["token_uri"] = Json::FromString(":no_scheme");
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  EXPECT_EQ(creds.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(creds.status().message(),
            "Could not parse 'scheme' from uri ':no_scheme'. "
            "Scheme not found.");
}

// --- Tests for CreateAssertionComponents ---

TEST_F(GDCHServiceAccountCredentialsTest, CreateAssertionComponentsSuccess) {
  // Mock gpr_now_impl so that conversions between GPR_CLOCK_REALTIME and
  // GPR_CLOCK_MONOTONIC are deterministic across platforms. Without mocking,
  // differences in clock resolution between the system realtime clock and the
  // monotonic timer (e.g. on Windows) can introduce sub-second jitter during
  // the round-trip conversion, causing second-truncated values (tv_sec) to be
  // off by one.
  //
  // Note: grpc_timer_manager_set_start_threaded(false) is called in main() to
  // prevent background timer manager threads from running, avoiding data races
  // when gpr_now_impl is modified here.
  gpr_timespec (*orig_gpr_now_impl)(gpr_clock_type) = gpr_now_impl;
  gpr_now_impl = [](gpr_clock_type clock_type) {
    return gpr_timespec{12345678, 0, clock_type};
  };
  absl::Cleanup cleanup = [&]() { gpr_now_impl = orig_gpr_now_impl; };

  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_TRUE(creds.ok()) << creds.status();
  Timestamp now = Timestamp::FromTimespecRoundDown(
      gpr_timespec{12345678, 0, GPR_CLOCK_REALTIME});

  AssertionComponents components = CreateAssertionComponents(**creds, now);

  EXPECT_EQ(components.header,
            "{\"alg\":\"ES256\","
            "\"kid\":\"test-private-key-id\","
            "\"typ\":\"JWT\"}");
  EXPECT_EQ(components.claim,
            "{\"aud\":\"https://test-token-uri.com/token\","
            "\"exp\":12349278,"
            "\"iat\":12345678,"
            "\"iss\":\"system:serviceaccount:test-project:test-name\","
            "\"sub\":\"system:serviceaccount:test-project:test-name\"}");
}

// --- Tests for MakeJWTAssertion ---

TEST_F(GDCHServiceAccountCredentialsTest, MakeJWTAssertionSuccess) {
  std::string header =
      "{\"alg\":\"ES256\","
      "\"kid\":\"key-id\","
      "\"typ\":\"JWT\"}";
  std::string payload =
      "{\"aud\":\"here\","
      "\"exp\":\"3700\","
      "\"iat\":\"100\","
      "\"iss\":\"me\","
      "\"sub\":\"me\"}";

  absl::StatusOr<std::string> jwt = MakeJWTAssertion(
      header, payload, kTestPrivateKeyPem, SignatureFormat::kRaw);
  ASSERT_TRUE(jwt.ok()) << jwt.status();

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
  std::string header =
      "{\"alg\":\"ES256\","
      "\"kid\":\"key-id\","
      "\"typ\":\"JWT\"}";
  std::string payload =
      "{\"aud\":\"here\","
      "\"exp\":\"3700\","
      "\"iat\":\"100\","
      "\"iss\":\"me\","
      "\"sub\":\"me\"}";

  absl::StatusOr<std::string> jwt = MakeJWTAssertion(
      header, payload, "invalid key pem", SignatureFormat::kRaw);
  EXPECT_EQ(jwt.status().code(), absl::StatusCode::kInternal);
  EXPECT_THAT(jwt.status().message(),
              HasSubstr("Invalid ServiceAccountCredentials could "
                        "not parse PEM to get private key:"));
}

// --- Tests for CreateRequestBody ---

TEST_F(GDCHServiceAccountCredentialsTest, CreateRequestBodySuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_TRUE(creds.ok()) << creds.status();

  absl::StatusOr<std::string> body = CreateRequestBody(**creds);
  ASSERT_TRUE(body.ok()) << body.status();

  absl::StatusOr<Json> parsed_body = JsonParse(*body);
  ASSERT_TRUE(parsed_body.ok()) << parsed_body.status();
  ASSERT_EQ(parsed_body->type(), Json::Type::kObject);
  auto it = parsed_body->object().find("subject_token");
  ASSERT_NE(it, parsed_body->object().end());
  ASSERT_EQ(it->second.type(), Json::Type::kString);
  std::string jwt_token = it->second.string();

  EXPECT_EQ(*body, absl::StrCat(
                       "{\"audience\":\"https://my-audience.com\","
                       "\"grant_type\":"
                       "\"urn:ietf:params:oauth:token-type:token-exchange\","
                       "\"requested_token_type\":"
                       "\"urn:ietf:params:oauth:token-type:access_token\","
                       "\"subject_token\":\"",
                       jwt_token,
                       "\","
                       "\"subject_token_type\":"
                       "\"urn:k8s:params:oauth:token-type:serviceaccount\"}"));

  std::vector<std::string> parts = absl::StrSplit(jwt_token, '.');
  ASSERT_EQ(parts.size(), 3);
}

TEST_F(GDCHServiceAccountCredentialsTest, CreateRequestBodyFailureInvalidKey) {
  absl::StatusOr<URI> token_url =
      URI::Parse("https://test-token-uri.com/token");
  ASSERT_TRUE(token_url.ok()) << token_url.status();
  GDCHServiceAccountCredentials creds(
      "test-private-key-id", "invalid key pem",
      "system:serviceaccount:test-project:test-name",
      /*ca_cert_path=*/std::nullopt, *std::move(token_url),
      "https://my-audience.com");
  absl::StatusOr<std::string> body = CreateRequestBody(creds);
  EXPECT_EQ(body.status().code(), absl::StatusCode::kInternal);
  EXPECT_THAT(body.status().message(),
              HasSubstr("Invalid ServiceAccountCredentials could not "
                        "parse PEM to get private key:"));
}

// --- Tests for FormatHttpRequest ---

TEST_F(GDCHServiceAccountCredentialsTest, FormatHttpRequestSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_TRUE(creds.ok()) << creds.status();

  absl::StatusOr<GDCHServiceAccountCredentialsTest::GrpcHttpRequestUniquePtr>
      request = FormatHttpRequest(**creds);
  ASSERT_TRUE(request.ok()) << request.status();
  ASSERT_NE((*request).get(), nullptr);

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
  ASSERT_TRUE(token_url.ok()) << token_url.status();
  GDCHServiceAccountCredentials creds(
      "test-private-key-id", "invalid key pem",
      "system:serviceaccount:test-project:test-name",
      /*ca_cert_path=*/std::nullopt, *std::move(token_url),
      "https://my-audience.com");
  absl::StatusOr<GDCHServiceAccountCredentialsTest::GrpcHttpRequestUniquePtr>
      request = FormatHttpRequest(creds);
  EXPECT_EQ(request.status().code(), absl::StatusCode::kInternal);
  EXPECT_THAT(request.status().message(),
              HasSubstr("Invalid ServiceAccountCredentials could not "
                        "parse PEM to get private key:"));
}

// --- Tests for ParseHttpResponse ---

TEST_F(GDCHServiceAccountCredentialsTest, ParseHttpResponseSuccess) {
  std::string response_body = "{\"access_token\":\"test-access-token\"}";
  absl::StatusOr<std::string> token = ParseHttpResponse(response_body);
  ASSERT_TRUE(token.ok()) << token.status();
  EXPECT_EQ(*token, "test-access-token");
}

TEST_F(GDCHServiceAccountCredentialsTest, ParseHttpResponseFailureNotObject) {
  absl::StatusOr<std::string> token = ParseHttpResponse("not-a-json");
  EXPECT_EQ(token.status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(token.status().message(),
            "The format of response is not a valid json object.");
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseHttpResponseFailureMissingToken) {
  std::string response_body = "{\"other_field\":\"value\"}";
  absl::StatusOr<std::string> token = ParseHttpResponse(response_body);
  EXPECT_EQ(token.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(token.status().message(),
            "errors validating JSON: ["
            "field:access_token error:field not present]");
}

TEST_F(GDCHServiceAccountCredentialsTest,
       ParseHttpResponseFailureTokenNotString) {
  std::string response_body = "{\"access_token\":123}";
  absl::StatusOr<std::string> token = ParseHttpResponse(response_body);
  EXPECT_EQ(token.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(token.status().message(),
            "errors validating JSON: ["
            "field:access_token error:is not a string]");
}

// --- Tests for ExtractToken ---

TEST_F(GDCHServiceAccountCredentialsTest, ExtractTokenSuccess) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_TRUE(creds.ok()) << creds.status();

  std::string body = "{\"access_token\":\"test-access-token\"}";
  grpc_http_response response = {};
  response.status = 200;
  response.body = const_cast<char*>(body.data());
  response.body_length = body.size();

  absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>> token =
      (*creds)->ExtractToken(response);
  ASSERT_TRUE(token.ok()) << token.status();
  ASSERT_NE(*token, nullptr);
  EXPECT_GT((*token)->ExpirationTime(), Timestamp::Now());
}

TEST_F(GDCHServiceAccountCredentialsTest, ExtractTokenFailureInvalidJson) {
  Json::Object obj = CreateValidServiceAccountObject();
  absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>> creds =
      GDCHServiceAccountCredentials::Create(JsonDump(Json::FromObject(obj)),
                                            "https://my-audience.com");
  ASSERT_TRUE(creds.ok()) << creds.status();

  std::string body = "not-a-json";
  grpc_http_response response = {};
  response.status = 200;
  response.body = const_cast<char*>(body.data());
  response.body_length = body.size();

  absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>> token =
      (*creds)->ExtractToken(response);
  EXPECT_EQ(token.status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(token.status().message(),
            "The format of response is not a valid json object.");
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  // Disable background timer manager threads so that modifying gpr_now_impl in
  // tests does not cause data races with the timer manager.
  grpc_timer_manager_set_start_threaded(false);

  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
