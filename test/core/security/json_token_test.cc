//
//
// Copyright 2015 gRPC authors.
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
//

#include "src/core/lib/security/credentials/jwt/json_token.h"

#include <string.h>

#include <gtest/gtest.h>
#include <openssl/evp.h>

#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_reader.h"
#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"
#include "src/core/lib/slice/b64.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

using grpc_core::Json;

// This JSON key was generated with the GCE console and revoked immediately.
// The identifiers have been changed as well.
// Maximum size for a string literal is 509 chars in C89, yay!
static const char test_json_key_str_part1[] =
    "{ \"private_key\": \"-----BEGIN PRIVATE KEY-----"
    "\\nMIICeAIBADANBgkqhkiG9w0BAQEFAASCAmIwggJeAgEAAoGBAOEvJsnoHnyHkXcp\\n7mJE"
    "qg"
    "WGjiw71NfXByguekSKho65FxaGbsnSM9SMQAqVk7Q2rG+I0OpsT0LrWQtZ\\nyjSeg/"
    "rWBQvS4hle4LfijkP3J5BG+"
    "IXDMP8RfziNRQsenAXDNPkY4kJCvKux2xdD\\nOnVF6N7dL3nTYZg+"
    "uQrNsMTz9UxVAgMBAAECgYEAzbLewe1xe9vy+2GoSsfib+28\\nDZgSE6Bu/"
    "zuFoPrRc6qL9p2SsnV7txrunTyJkkOnPLND9ABAXybRTlcVKP/sGgza\\n/"
    "8HpCqFYM9V8f34SBWfD4fRFT+n/"
    "73cfRUtGXdXpseva2lh8RilIQfPhNZAncenU\\ngqXjDvpkypEusgXAykECQQD+";
static const char test_json_key_str_part2[] =
    "53XxNVnxBHsYb+AYEfklR96yVi8HywjVHP34+OQZ\\nCslxoHQM8s+"
    "dBnjfScLu22JqkPv04xyxmt0QAKm9+vTdAkEA4ib7YvEAn2jXzcCI\\nEkoy2L/"
    "XydR1GCHoacdfdAwiL2npOdnbvi4ZmdYRPY1LSTO058tQHKVXV7NLeCa3\\nAARh2QJBAMKeDA"
    "G"
    "W303SQv2cZTdbeaLKJbB5drz3eo3j7dDKjrTD9JupixFbzcGw\\n8FZi5c8idxiwC36kbAL6Hz"
    "A"
    "ZoX+ofI0CQE6KCzPJTtYNqyShgKAZdJ8hwOcvCZtf\\n6z8RJm0+"
    "6YBd38lfh5j8mZd7aHFf6I17j5AQY7oPEc47TjJj/"
    "5nZ68ECQQDvYuI3\\nLyK5fS8g0SYbmPOL9TlcHDOqwG0mrX9qpg5DC2fniXNSrrZ64GTDKdzZ"
    "Y"
    "Ap6LI9W\\nIqv4vr6y38N79TTC\\n-----END PRIVATE KEY-----\\n\", ";
static const char test_json_key_str_part3[] =
    "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
    "\"client_email\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
    "com\", \"client_id\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
    "com\", \"type\": \"service_account\" }";

// Test refresh token.
static const char test_refresh_token_str[] =
    "{ \"client_id\": \"32555999999.apps.googleusercontent.com\","
    "  \"client_secret\": \"EmssLNjJy1332hD4KFsecret\","
    "  \"refresh_token\": \"1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42\","
    "  \"type\": \"authorized_user\"}";

static const char test_scope[] = "myperm1 myperm2";

static const char test_service_url[] = "https://foo.com/foo.v1";

static char* test_json_key_str(const char* bad_part3) {
  const char* part3 =
      bad_part3 != nullptr ? bad_part3 : test_json_key_str_part3;
  size_t result_len = strlen(test_json_key_str_part1) +
                      strlen(test_json_key_str_part2) + strlen(part3);
  char* result = static_cast<char*>(gpr_malloc(result_len + 1));
  char* current = result;
  strcpy(result, test_json_key_str_part1);
  current += strlen(test_json_key_str_part1);
  strcpy(current, test_json_key_str_part2);
  current += strlen(test_json_key_str_part2);
  strcpy(current, part3);
  return result;
}

TEST(JsonTokenTest, ParseJsonKeySuccess) {
  char* json_string = test_json_key_str(nullptr);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  ASSERT_TRUE(grpc_auth_json_key_is_valid(&json_key));
  ASSERT_TRUE(json_key.type != nullptr &&
              strcmp(json_key.type, "service_account") == 0);
  ASSERT_TRUE(json_key.private_key_id != nullptr &&
              strcmp(json_key.private_key_id,
                     "e6b5137873db8d2ef81e06a47289e6434ec8a165") == 0);
  ASSERT_TRUE(json_key.client_id != nullptr &&
              strcmp(json_key.client_id,
                     "777-abaslkan11hlb6nmim3bpspl31ud.apps."
                     "googleusercontent.com") == 0);
  ASSERT_TRUE(json_key.client_email != nullptr &&
              strcmp(json_key.client_email,
                     "777-abaslkan11hlb6nmim3bpspl31ud@developer."
                     "gserviceaccount.com") == 0);
  ASSERT_NE(json_key.private_key, nullptr);
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

TEST(JsonTokenTest, ParseJsonKeyFailureBadJson) {
  const char non_closing_part3[] =
      "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", \"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\", \"type\": \"service_account\" ";
  char* json_string = test_json_key_str(non_closing_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  ASSERT_FALSE(grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

TEST(JsonTokenTest, ParseJsonKeyFailureNoType) {
  const char no_type_part3[] =
      "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", \"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\" }";
  char* json_string = test_json_key_str(no_type_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  ASSERT_FALSE(grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

TEST(JsonTokenTest, ParseJsonKeyFailureNoClientId) {
  const char no_client_id_part3[] =
      "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", "
      "\"type\": \"service_account\" }";
  char* json_string = test_json_key_str(no_client_id_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  ASSERT_FALSE(grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

TEST(JsonTokenTest, ParseJsonKeyFailureNoClientEmail) {
  const char no_client_email_part3[] =
      "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\", \"type\": \"service_account\" }";
  char* json_string = test_json_key_str(no_client_email_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  ASSERT_FALSE(grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

TEST(JsonTokenTest, ParseJsonKeyFailureNoPrivateKeyId) {
  const char no_private_key_id_part3[] =
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", \"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\", \"type\": \"service_account\" }";
  char* json_string = test_json_key_str(no_private_key_id_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  ASSERT_FALSE(grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

TEST(JsonTokenTest, ParseJsonKeyFailureNoPrivateKey) {
  const char no_private_key_json_string[] =
      "{ \"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", \"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\", \"type\": \"service_account\" }";
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(no_private_key_json_string);
  ASSERT_FALSE(grpc_auth_json_key_is_valid(&json_key));
  grpc_auth_json_key_destruct(&json_key);
}

static Json parse_json_part_from_jwt(const char* str, size_t len) {
  grpc_core::ExecCtx exec_ctx;
  char* b64 = static_cast<char*>(gpr_malloc(len + 1));
  strncpy(b64, str, len);
  b64[len] = '\0';
  grpc_slice slice = grpc_base64_decode(b64, 1);
  gpr_free(b64);
  EXPECT_FALSE(GRPC_SLICE_IS_EMPTY(slice));
  absl::string_view string = grpc_core::StringViewFromSlice(slice);
  auto json = grpc_core::JsonParse(string);
  grpc_slice_unref(slice);
  if (!json.ok()) {
    gpr_log(GPR_ERROR, "JSON parse error: %s",
            json.status().ToString().c_str());
    return Json();
  }
  return std::move(*json);
}

static void check_jwt_header(const Json& header) {
  Json::Object object = header.object();
  Json value = object["alg"];
  ASSERT_EQ(value.type(), Json::Type::kString);
  ASSERT_STREQ(value.string().c_str(), "RS256");
  value = object["typ"];
  ASSERT_EQ(value.type(), Json::Type::kString);
  ASSERT_STREQ(value.string().c_str(), "JWT");
  value = object["kid"];
  ASSERT_EQ(value.type(), Json::Type::kString);
  ASSERT_STREQ(value.string().c_str(),
               "e6b5137873db8d2ef81e06a47289e6434ec8a165");
}

static void check_jwt_claim(const Json& claim, const char* expected_audience,
                            const char* expected_scope) {
  Json::Object object = claim.object();

  Json value = object["iss"];
  ASSERT_EQ(value.type(), Json::Type::kString);
  ASSERT_EQ(value.string(),
            "777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount.com");

  if (expected_scope != nullptr) {
    ASSERT_EQ(object.find("sub"), object.end());
    value = object["scope"];
    ASSERT_EQ(value.type(), Json::Type::kString);
    ASSERT_EQ(value.string(), expected_scope);
  } else {
    // Claims without scope must have a sub.
    ASSERT_EQ(object.find("scope"), object.end());
    value = object["sub"];
    ASSERT_EQ(value.type(), Json::Type::kString);
    ASSERT_EQ(value.string(), object["iss"].string());
  }

  value = object["aud"];
  ASSERT_EQ(value.type(), Json::Type::kString);
  ASSERT_EQ(value.string(), expected_audience);

  gpr_timespec expiration = gpr_time_0(GPR_CLOCK_REALTIME);
  value = object["exp"];
  ASSERT_EQ(value.type(), Json::Type::kNumber);
  expiration.tv_sec = strtol(value.string().c_str(), nullptr, 10);

  gpr_timespec issue_time = gpr_time_0(GPR_CLOCK_REALTIME);
  value = object["iat"];
  ASSERT_EQ(value.type(), Json::Type::kNumber);
  issue_time.tv_sec = strtol(value.string().c_str(), nullptr, 10);

  gpr_timespec parsed_lifetime = gpr_time_sub(expiration, issue_time);
  ASSERT_EQ(parsed_lifetime.tv_sec, grpc_max_auth_token_lifetime().tv_sec);
}

static void check_jwt_signature(const char* b64_signature, RSA* rsa_key,
                                const char* signed_data,
                                size_t signed_data_size) {
  grpc_core::ExecCtx exec_ctx;

  EVP_MD_CTX* md_ctx = EVP_MD_CTX_create();
  EVP_PKEY* key = EVP_PKEY_new();

  grpc_slice sig = grpc_base64_decode(b64_signature, 1);
  ASSERT_FALSE(GRPC_SLICE_IS_EMPTY(sig));
  ASSERT_EQ(GRPC_SLICE_LENGTH(sig), 128);

  ASSERT_NE(md_ctx, nullptr);
  ASSERT_NE(key, nullptr);
  EVP_PKEY_set1_RSA(key, rsa_key);

  ASSERT_EQ(EVP_DigestVerifyInit(md_ctx, nullptr, EVP_sha256(), nullptr, key),
            1);
  ASSERT_EQ(EVP_DigestVerifyUpdate(md_ctx, signed_data, signed_data_size), 1);
  ASSERT_EQ(EVP_DigestVerifyFinal(md_ctx, GRPC_SLICE_START_PTR(sig),
                                  GRPC_SLICE_LENGTH(sig)),
            1);

  grpc_slice_unref(sig);
  if (key != nullptr) EVP_PKEY_free(key);
  if (md_ctx != nullptr) EVP_MD_CTX_destroy(md_ctx);
}

static char* service_account_creds_jwt_encode_and_sign(
    const grpc_auth_json_key* key) {
  return grpc_jwt_encode_and_sign(key, GRPC_JWT_OAUTH2_AUDIENCE,
                                  grpc_max_auth_token_lifetime(), test_scope);
}

static char* jwt_creds_jwt_encode_and_sign(const grpc_auth_json_key* key) {
  return grpc_jwt_encode_and_sign(key, test_service_url,
                                  grpc_max_auth_token_lifetime(), nullptr);
}

static void service_account_creds_check_jwt_claim(const Json& claim) {
  check_jwt_claim(claim, GRPC_JWT_OAUTH2_AUDIENCE, test_scope);
}

static void jwt_creds_check_jwt_claim(const Json& claim) {
  check_jwt_claim(claim, test_service_url, nullptr);
}

static void test_jwt_encode_and_sign(
    char* (*jwt_encode_and_sign_func)(const grpc_auth_json_key*),
    void (*check_jwt_claim_func)(const Json&)) {
  char* json_string = test_json_key_str(nullptr);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  const char* b64_signature;
  size_t offset = 0;
  char* jwt = jwt_encode_and_sign_func(&json_key);
  const char* dot = strchr(jwt, '.');
  ASSERT_NE(dot, nullptr);
  Json parsed_header =
      parse_json_part_from_jwt(jwt, static_cast<size_t>(dot - jwt));
  ASSERT_EQ(parsed_header.type(), Json::Type::kObject);
  check_jwt_header(parsed_header);
  offset = static_cast<size_t>(dot - jwt) + 1;

  dot = strchr(jwt + offset, '.');
  ASSERT_NE(dot, nullptr);
  Json parsed_claim = parse_json_part_from_jwt(
      jwt + offset, static_cast<size_t>(dot - (jwt + offset)));
  ASSERT_EQ(parsed_claim.type(), Json::Type::kObject);
  check_jwt_claim_func(parsed_claim);
  offset = static_cast<size_t>(dot - jwt) + 1;

  dot = strchr(jwt + offset, '.');
  ASSERT_EQ(dot, nullptr);  // no more part.
  b64_signature = jwt + offset;
  check_jwt_signature(b64_signature, json_key.private_key, jwt, offset - 1);

  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
  gpr_free(jwt);
}

TEST(JsonTokenTest, ServiceAccountCredsJwtEncodeAndSign) {
  test_jwt_encode_and_sign(service_account_creds_jwt_encode_and_sign,
                           service_account_creds_check_jwt_claim);
}

TEST(JsonTokenTest, JwtCredsJwtEncodeAndSign) {
  test_jwt_encode_and_sign(jwt_creds_jwt_encode_and_sign,
                           jwt_creds_check_jwt_claim);
}

TEST(JsonTokenTest, ParseRefreshTokenSuccess) {
  grpc_auth_refresh_token refresh_token =
      grpc_auth_refresh_token_create_from_string(test_refresh_token_str);
  ASSERT_TRUE(grpc_auth_refresh_token_is_valid(&refresh_token));
  ASSERT_TRUE(refresh_token.type != nullptr &&
              (strcmp(refresh_token.type, "authorized_user") == 0));
  ASSERT_TRUE(refresh_token.client_id != nullptr &&
              (strcmp(refresh_token.client_id,
                      "32555999999.apps.googleusercontent.com") == 0));
  ASSERT_TRUE(
      refresh_token.client_secret != nullptr &&
      (strcmp(refresh_token.client_secret, "EmssLNjJy1332hD4KFsecret") == 0));
  ASSERT_TRUE(refresh_token.refresh_token != nullptr &&
              (strcmp(refresh_token.refresh_token,
                      "1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42") == 0));
  grpc_auth_refresh_token_destruct(&refresh_token);
}

TEST(JsonTokenTest, ParseRefreshTokenFailureNoType) {
  const char refresh_token_str[] =
      "{ \"client_id\": \"32555999999.apps.googleusercontent.com\","
      "  \"client_secret\": \"EmssLNjJy1332hD4KFsecret\","
      "  \"refresh_token\": \"1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42\"}";
  grpc_auth_refresh_token refresh_token =
      grpc_auth_refresh_token_create_from_string(refresh_token_str);
  ASSERT_FALSE(grpc_auth_refresh_token_is_valid(&refresh_token));
}

TEST(JsonTokenTest, ParseRefreshTokenFailureNoClientId) {
  const char refresh_token_str[] =
      "{ \"client_secret\": \"EmssLNjJy1332hD4KFsecret\","
      "  \"refresh_token\": \"1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42\","
      "  \"type\": \"authorized_user\"}";
  grpc_auth_refresh_token refresh_token =
      grpc_auth_refresh_token_create_from_string(refresh_token_str);
  ASSERT_FALSE(grpc_auth_refresh_token_is_valid(&refresh_token));
}

TEST(JsonTokenTest, ParseRefreshTokenFailureNoClientSecret) {
  const char refresh_token_str[] =
      "{ \"client_id\": \"32555999999.apps.googleusercontent.com\","
      "  \"refresh_token\": \"1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42\","
      "  \"type\": \"authorized_user\"}";
  grpc_auth_refresh_token refresh_token =
      grpc_auth_refresh_token_create_from_string(refresh_token_str);
  ASSERT_FALSE(grpc_auth_refresh_token_is_valid(&refresh_token));
}

TEST(JsonTokenTest, ParseRefreshTokenFailureNoRefreshToken) {
  const char refresh_token_str[] =
      "{ \"client_id\": \"32555999999.apps.googleusercontent.com\","
      "  \"client_secret\": \"EmssLNjJy1332hD4KFsecret\","
      "  \"type\": \"authorized_user\"}";
  grpc_auth_refresh_token refresh_token =
      grpc_auth_refresh_token_create_from_string(refresh_token_str);
  ASSERT_FALSE(grpc_auth_refresh_token_is_valid(&refresh_token));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
