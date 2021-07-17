/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/security/credentials/jwt/jwt_verifier.h"

#include <string.h>

#include <grpc/grpc.h>

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/security/credentials/jwt/json_token.h"
#include "src/core/lib/slice/b64.h"
#include "test/core/util/test_config.h"

using grpc_core::Json;

/* This JSON key was generated with the GCE console and revoked immediately.
   The identifiers have been changed as well.
   Maximum size for a string literal is 509 chars in C89, yay!  */
static const char json_key_str_part1[] =
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
static const char json_key_str_part2[] =
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
static const char json_key_str_part3_for_google_email_issuer[] =
    "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
    "\"client_email\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
    "com\", \"client_id\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
    "com\", \"type\": \"service_account\" }";
/* Trick our JWT library into issuing a JWT with iss=accounts.google.com. */
static const char json_key_str_part3_for_url_issuer[] =
    "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
    "\"client_email\": \"accounts.google.com\", "
    "\"client_id\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
    "com\", \"type\": \"service_account\" }";
static const char json_key_str_part3_for_custom_email_issuer[] =
    "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
    "\"client_email\": "
    "\"foo@bar.com\", \"client_id\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
    "com\", \"type\": \"service_account\" }";

static grpc_jwt_verifier_email_domain_key_url_mapping custom_mapping = {
    "bar.com", "keys.bar.com/jwk"};

static const char expected_user_data[] = "user data";

static const char good_jwk_set[] =
    "{"
    " \"keys\": ["
    "  {"
    "   \"kty\": \"RSA\","
    "   \"alg\": \"RS256\","
    "   \"use\": \"sig\","
    "   \"kid\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\","
    "   \"n\": "
    "\"4S8myegefIeRdynuYkSqBYaOLDvU19cHKC56RIqGjrkXFoZuydIz1IxACpWTtDasb4jQ6mxP"
    "QutZC1nKNJ6D-tYFC9LiGV7gt-KOQ_cnkEb4hcMw_xF_OI1FCx6cBcM0-"
    "RjiQkK8q7HbF0M6dUXo3t0vedNhmD65Cs2wxPP1TFU=\","
    "   \"e\": \"AQAB\""
    "  }"
    " ]"
    "}";

static gpr_timespec expected_lifetime = {3600, 0, GPR_TIMESPAN};

static const char good_google_email_keys_part1[] =
    "{\"e6b5137873db8d2ef81e06a47289e6434ec8a165\": \"-----BEGIN "
    "CERTIFICATE-----"
    "\\nMIICATCCAWoCCQDEywLhxvHjnDANBgkqhkiG9w0BAQsFADBFMQswCQYDVQQGEwJB\\nVTET"
    "MBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50ZXJuZXQgV2lkZ2l0\\ncyBQdHkgTHR"
    "kMB4XDTE1MDYyOTA4Mzk1MFoXDTI1MDYyNjA4Mzk1MFowRTELMAkG\\nA1UEBhMCQVUxEzARBg"
    "NVBAgMClNvbWUtU3RhdGUxITAfBgNVBAoMGEludGVybmV0\\nIFdpZGdpdHMgUHR5IEx0ZDCBn"
    "zANBgkqhkiG9w0BAQEFAAOBjQAwgYkCgYEA4S8m\\nyegefIeRdynuYkSqBYaOLDvU19cHKC56"
    "RIqGjrkXFoZuydIz1IxACpWTtDasb4jQ\\n6mxPQutZC1nKNJ6D+tYFC9LiGV7gt+KOQ/";

static const char good_google_email_keys_part2[] =
    "cnkEb4hcMw/xF/OI1FCx6cBcM0+"
    "Rji\\nQkK8q7HbF0M6dUXo3t0vedNhmD65Cs2wxPP1TFUCAwEAATANBgkqhkiG9w0BAQsF\\nA"
    "AOBgQBfu69FkPmBknbKNFgurPz78kbs3VNN+k/"
    "PUgO5DHKskJmgK2TbtvX2VMpx\\nkftmHGzgzMzUlOtigCaGMgHWjfqjpP9uuDbahXrZBJzB8c"
    "Oq7MrQF8r17qVvo3Ue\\nPjTKQMAsU8uxTEMmeuz9L6yExs0rfd6bPOrQkAoVfFfiYB3/"
    "pA==\\n-----END CERTIFICATE-----\\n\"}";

static const char expected_audience[] = "https://foo.com";

static const char good_openid_config[] =
    "{"
    " \"issuer\": \"https://accounts.google.com\","
    " \"authorization_endpoint\": "
    "\"https://accounts.google.com/o/oauth2/v2/auth\","
    " \"token_endpoint\": \"https://oauth2.googleapis.com/token\","
    " \"userinfo_endpoint\": \"https://www.googleapis.com/oauth2/v3/userinfo\","
    " \"revocation_endpoint\": \"https://oauth2.googleapis.com/revoke\","
    " \"jwks_uri\": \"https://www.googleapis.com/oauth2/v3/certs\""
    "}";

static const char expired_claims[] =
    "{ \"aud\": \"https://foo.com\","
    "  \"iss\": \"blah.foo.com\","
    "  \"sub\": \"juju@blah.foo.com\","
    "  \"jti\": \"jwtuniqueid\","
    "  \"iat\": 100," /* Way back in the past... */
    "  \"exp\": 120,"
    "  \"nbf\": 60,"
    "  \"foo\": \"bar\"}";

static const char claims_without_time_constraint[] =
    "{ \"aud\": \"https://foo.com\","
    "  \"iss\": \"blah.foo.com\","
    "  \"sub\": \"juju@blah.foo.com\","
    "  \"jti\": \"jwtuniqueid\","
    "  \"foo\": \"bar\"}";

static const char claims_with_bad_subject[] =
    "{ \"aud\": \"https://foo.com\","
    "  \"iss\": \"evil@blah.foo.com\","
    "  \"sub\": \"juju@blah.foo.com\","
    "  \"jti\": \"jwtuniqueid\","
    "  \"foo\": \"bar\"}";

static const char invalid_claims[] =
    "{ \"aud\": \"https://foo.com\","
    "  \"iss\": 46," /* Issuer cannot be a number. */
    "  \"sub\": \"juju@blah.foo.com\","
    "  \"jti\": \"jwtuniqueid\","
    "  \"foo\": \"bar\"}";

typedef struct {
  grpc_jwt_verifier_status expected_status;
  const char* expected_issuer;
  const char* expected_subject;
} verifier_test_config;

static void test_jwt_issuer_email_domain(void) {
  const char* d = grpc_jwt_issuer_email_domain("https://foo.com");
  GPR_ASSERT(d == nullptr);
  d = grpc_jwt_issuer_email_domain("foo.com");
  GPR_ASSERT(d == nullptr);
  d = grpc_jwt_issuer_email_domain("");
  GPR_ASSERT(d == nullptr);
  d = grpc_jwt_issuer_email_domain("@");
  GPR_ASSERT(d == nullptr);
  d = grpc_jwt_issuer_email_domain("bar@foo");
  GPR_ASSERT(strcmp(d, "foo") == 0);
  d = grpc_jwt_issuer_email_domain("bar@foo.com");
  GPR_ASSERT(strcmp(d, "foo.com") == 0);
  d = grpc_jwt_issuer_email_domain("bar@blah.foo.com");
  GPR_ASSERT(strcmp(d, "foo.com") == 0);
  d = grpc_jwt_issuer_email_domain("bar.blah@blah.foo.com");
  GPR_ASSERT(strcmp(d, "foo.com") == 0);
  d = grpc_jwt_issuer_email_domain("bar.blah@baz.blah.foo.com");
  GPR_ASSERT(strcmp(d, "foo.com") == 0);

  /* This is not a very good parser but make sure we do not crash on these weird
     inputs. */
  d = grpc_jwt_issuer_email_domain("@foo");
  GPR_ASSERT(strcmp(d, "foo") == 0);
  d = grpc_jwt_issuer_email_domain("bar@.");
  GPR_ASSERT(d != nullptr);
  d = grpc_jwt_issuer_email_domain("bar@..");
  GPR_ASSERT(d != nullptr);
  d = grpc_jwt_issuer_email_domain("bar@...");
  GPR_ASSERT(d != nullptr);
}

static void test_claims_success(void) {
  grpc_jwt_claims* claims;
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(claims_without_time_constraint, &error);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "JSON parse error: %s",
            grpc_error_std_string(error).c_str());
  }
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(json.type() == Json::Type::OBJECT);
  grpc_core::ExecCtx exec_ctx;
  claims = grpc_jwt_claims_from_json(json);
  GPR_ASSERT(claims != nullptr);
  GPR_ASSERT(*grpc_jwt_claims_json(claims) == json);
  GPR_ASSERT(strcmp(grpc_jwt_claims_audience(claims), "https://foo.com") == 0);
  GPR_ASSERT(strcmp(grpc_jwt_claims_issuer(claims), "blah.foo.com") == 0);
  GPR_ASSERT(strcmp(grpc_jwt_claims_subject(claims), "juju@blah.foo.com") == 0);
  GPR_ASSERT(strcmp(grpc_jwt_claims_id(claims), "jwtuniqueid") == 0);
  GPR_ASSERT(grpc_jwt_claims_check(claims, "https://foo.com") ==
             GRPC_JWT_VERIFIER_OK);
  grpc_jwt_claims_destroy(claims);
}

static void test_expired_claims_failure(void) {
  grpc_jwt_claims* claims;
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(expired_claims, &error);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "JSON parse error: %s",
            grpc_error_std_string(error).c_str());
  }
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(json.type() == Json::Type::OBJECT);
  gpr_timespec exp_iat = {100, 0, GPR_CLOCK_REALTIME};
  gpr_timespec exp_exp = {120, 0, GPR_CLOCK_REALTIME};
  gpr_timespec exp_nbf = {60, 0, GPR_CLOCK_REALTIME};
  grpc_core::ExecCtx exec_ctx;
  claims = grpc_jwt_claims_from_json(json);
  GPR_ASSERT(claims != nullptr);
  GPR_ASSERT(*grpc_jwt_claims_json(claims) == json);
  GPR_ASSERT(strcmp(grpc_jwt_claims_audience(claims), "https://foo.com") == 0);
  GPR_ASSERT(strcmp(grpc_jwt_claims_issuer(claims), "blah.foo.com") == 0);
  GPR_ASSERT(strcmp(grpc_jwt_claims_subject(claims), "juju@blah.foo.com") == 0);
  GPR_ASSERT(strcmp(grpc_jwt_claims_id(claims), "jwtuniqueid") == 0);
  GPR_ASSERT(gpr_time_cmp(grpc_jwt_claims_issued_at(claims), exp_iat) == 0);
  GPR_ASSERT(gpr_time_cmp(grpc_jwt_claims_expires_at(claims), exp_exp) == 0);
  GPR_ASSERT(gpr_time_cmp(grpc_jwt_claims_not_before(claims), exp_nbf) == 0);

  GPR_ASSERT(grpc_jwt_claims_check(claims, "https://foo.com") ==
             GRPC_JWT_VERIFIER_TIME_CONSTRAINT_FAILURE);
  grpc_jwt_claims_destroy(claims);
}

static void test_invalid_claims_failure(void) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(invalid_claims, &error);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "JSON parse error: %s",
            grpc_error_std_string(error).c_str());
  }
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(json.type() == Json::Type::OBJECT);
  grpc_core::ExecCtx exec_ctx;
  GPR_ASSERT(grpc_jwt_claims_from_json(json) == nullptr);
}

static void test_bad_audience_claims_failure(void) {
  grpc_jwt_claims* claims;
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(claims_without_time_constraint, &error);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "JSON parse error: %s",
            grpc_error_std_string(error).c_str());
  }
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(json.type() == Json::Type::OBJECT);
  grpc_core::ExecCtx exec_ctx;
  claims = grpc_jwt_claims_from_json(json);
  GPR_ASSERT(claims != nullptr);
  GPR_ASSERT(grpc_jwt_claims_check(claims, "https://bar.com") ==
             GRPC_JWT_VERIFIER_BAD_AUDIENCE);
  grpc_jwt_claims_destroy(claims);
}

static void test_bad_subject_claims_failure(void) {
  grpc_jwt_claims* claims;
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(claims_with_bad_subject, &error);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "JSON parse error: %s",
            grpc_error_std_string(error).c_str());
  }
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(json.type() == Json::Type::OBJECT);
  grpc_core::ExecCtx exec_ctx;
  claims = grpc_jwt_claims_from_json(json);
  GPR_ASSERT(claims != nullptr);
  GPR_ASSERT(grpc_jwt_claims_check(claims, "https://foo.com") ==
             GRPC_JWT_VERIFIER_BAD_SUBJECT);
  grpc_jwt_claims_destroy(claims);
}

static char* json_key_str(const char* last_part) {
  size_t result_len = strlen(json_key_str_part1) + strlen(json_key_str_part2) +
                      strlen(last_part);
  char* result = static_cast<char*>(gpr_malloc(result_len + 1));
  char* current = result;
  strcpy(result, json_key_str_part1);
  current += strlen(json_key_str_part1);
  strcpy(current, json_key_str_part2);
  current += strlen(json_key_str_part2);
  strcpy(current, last_part);
  return result;
}

static char* good_google_email_keys(void) {
  size_t result_len = strlen(good_google_email_keys_part1) +
                      strlen(good_google_email_keys_part2);
  char* result = static_cast<char*>(gpr_malloc(result_len + 1));
  char* current = result;
  strcpy(result, good_google_email_keys_part1);
  current += strlen(good_google_email_keys_part1);
  strcpy(current, good_google_email_keys_part2);
  return result;
}

static grpc_httpcli_response http_response(int status, char* body) {
  grpc_httpcli_response response;
  response = {};
  response.status = status;
  response.body = body;
  response.body_length = strlen(body);
  return response;
}

static int httpcli_post_should_not_be_called(
    const grpc_httpcli_request* /*request*/, const char* /*body_bytes*/,
    size_t /*body_size*/, grpc_millis /*deadline*/, grpc_closure* /*on_done*/,
    grpc_httpcli_response* /*response*/) {
  GPR_ASSERT("HTTP POST should not be called" == nullptr);
  return 1;
}

static int httpcli_get_google_keys_for_email(
    const grpc_httpcli_request* request, grpc_millis /*deadline*/,
    grpc_closure* on_done, grpc_httpcli_response* response) {
  *response = http_response(200, good_google_email_keys());
  GPR_ASSERT(request->handshaker == &grpc_httpcli_ssl);
  GPR_ASSERT(strcmp(request->host, "www.googleapis.com") == 0);
  GPR_ASSERT(strcmp(request->http.path,
                    "/robot/v1/metadata/x509/"
                    "777-abaslkan11hlb6nmim3bpspl31ud@developer."
                    "gserviceaccount.com") == 0);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static void on_verification_success(void* user_data,
                                    grpc_jwt_verifier_status status,
                                    grpc_jwt_claims* claims) {
  GPR_ASSERT(status == GRPC_JWT_VERIFIER_OK);
  GPR_ASSERT(claims != nullptr);
  GPR_ASSERT(user_data == (void*)expected_user_data);
  GPR_ASSERT(strcmp(grpc_jwt_claims_audience(claims), expected_audience) == 0);
  grpc_jwt_claims_destroy(claims);
}

static void test_jwt_verifier_google_email_issuer_success(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_jwt_verifier* verifier = grpc_jwt_verifier_create(nullptr, 0);
  char* jwt = nullptr;
  char* key_str = json_key_str(json_key_str_part3_for_google_email_issuer);
  grpc_auth_json_key key = grpc_auth_json_key_create_from_string(key_str);
  gpr_free(key_str);
  GPR_ASSERT(grpc_auth_json_key_is_valid(&key));
  grpc_httpcli_set_override(httpcli_get_google_keys_for_email,
                            httpcli_post_should_not_be_called);
  jwt = grpc_jwt_encode_and_sign(&key, expected_audience, expected_lifetime,
                                 nullptr);
  grpc_auth_json_key_destruct(&key);
  GPR_ASSERT(jwt != nullptr);
  grpc_jwt_verifier_verify(verifier, nullptr, jwt, expected_audience,
                           on_verification_success,
                           const_cast<char*>(expected_user_data));
  grpc_jwt_verifier_destroy(verifier);
  grpc_core::ExecCtx::Get()->Flush();
  gpr_free(jwt);
  grpc_httpcli_set_override(nullptr, nullptr);
}

static int httpcli_get_custom_keys_for_email(
    const grpc_httpcli_request* request, grpc_millis /*deadline*/,
    grpc_closure* on_done, grpc_httpcli_response* response) {
  *response = http_response(200, gpr_strdup(good_jwk_set));
  GPR_ASSERT(request->handshaker == &grpc_httpcli_ssl);
  GPR_ASSERT(strcmp(request->host, "keys.bar.com") == 0);
  GPR_ASSERT(strcmp(request->http.path, "/jwk/foo@bar.com") == 0);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static void test_jwt_verifier_custom_email_issuer_success(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_jwt_verifier* verifier = grpc_jwt_verifier_create(&custom_mapping, 1);
  char* jwt = nullptr;
  char* key_str = json_key_str(json_key_str_part3_for_custom_email_issuer);
  grpc_auth_json_key key = grpc_auth_json_key_create_from_string(key_str);
  gpr_free(key_str);
  GPR_ASSERT(grpc_auth_json_key_is_valid(&key));
  grpc_httpcli_set_override(httpcli_get_custom_keys_for_email,
                            httpcli_post_should_not_be_called);
  jwt = grpc_jwt_encode_and_sign(&key, expected_audience, expected_lifetime,
                                 nullptr);
  grpc_auth_json_key_destruct(&key);
  GPR_ASSERT(jwt != nullptr);
  grpc_jwt_verifier_verify(verifier, nullptr, jwt, expected_audience,
                           on_verification_success,
                           const_cast<char*>(expected_user_data));
  grpc_jwt_verifier_destroy(verifier);
  grpc_core::ExecCtx::Get()->Flush();
  gpr_free(jwt);
  grpc_httpcli_set_override(nullptr, nullptr);
}

static int httpcli_get_jwk_set(const grpc_httpcli_request* request,
                               grpc_millis /*deadline*/, grpc_closure* on_done,
                               grpc_httpcli_response* response) {
  *response = http_response(200, gpr_strdup(good_jwk_set));
  GPR_ASSERT(request->handshaker == &grpc_httpcli_ssl);
  GPR_ASSERT(strcmp(request->host, "www.googleapis.com") == 0);
  GPR_ASSERT(strcmp(request->http.path, "/oauth2/v3/certs") == 0);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int httpcli_get_openid_config(const grpc_httpcli_request* request,
                                     grpc_millis /*deadline*/,
                                     grpc_closure* on_done,
                                     grpc_httpcli_response* response) {
  *response = http_response(200, gpr_strdup(good_openid_config));
  GPR_ASSERT(request->handshaker == &grpc_httpcli_ssl);
  GPR_ASSERT(strcmp(request->host, "accounts.google.com") == 0);
  GPR_ASSERT(strcmp(request->http.path, GRPC_OPENID_CONFIG_URL_SUFFIX) == 0);
  grpc_httpcli_set_override(httpcli_get_jwk_set,
                            httpcli_post_should_not_be_called);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static void test_jwt_verifier_url_issuer_success(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_jwt_verifier* verifier = grpc_jwt_verifier_create(nullptr, 0);
  char* jwt = nullptr;
  char* key_str = json_key_str(json_key_str_part3_for_url_issuer);
  grpc_auth_json_key key = grpc_auth_json_key_create_from_string(key_str);
  gpr_free(key_str);
  GPR_ASSERT(grpc_auth_json_key_is_valid(&key));
  grpc_httpcli_set_override(httpcli_get_openid_config,
                            httpcli_post_should_not_be_called);
  jwt = grpc_jwt_encode_and_sign(&key, expected_audience, expected_lifetime,
                                 nullptr);
  grpc_auth_json_key_destruct(&key);
  GPR_ASSERT(jwt != nullptr);
  grpc_jwt_verifier_verify(verifier, nullptr, jwt, expected_audience,
                           on_verification_success,
                           const_cast<char*>(expected_user_data));
  grpc_jwt_verifier_destroy(verifier);
  grpc_core::ExecCtx::Get()->Flush();
  gpr_free(jwt);
  grpc_httpcli_set_override(nullptr, nullptr);
}

static void on_verification_key_retrieval_error(void* user_data,
                                                grpc_jwt_verifier_status status,
                                                grpc_jwt_claims* claims) {
  GPR_ASSERT(status == GRPC_JWT_VERIFIER_KEY_RETRIEVAL_ERROR);
  GPR_ASSERT(claims == nullptr);
  GPR_ASSERT(user_data == (void*)expected_user_data);
}

static int httpcli_get_bad_json(const grpc_httpcli_request* request,
                                grpc_millis /*deadline*/, grpc_closure* on_done,
                                grpc_httpcli_response* response) {
  *response = http_response(200, gpr_strdup("{\"bad\": \"stuff\"}"));
  GPR_ASSERT(request->handshaker == &grpc_httpcli_ssl);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static void test_jwt_verifier_url_issuer_bad_config(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_jwt_verifier* verifier = grpc_jwt_verifier_create(nullptr, 0);
  char* jwt = nullptr;
  char* key_str = json_key_str(json_key_str_part3_for_url_issuer);
  grpc_auth_json_key key = grpc_auth_json_key_create_from_string(key_str);
  gpr_free(key_str);
  GPR_ASSERT(grpc_auth_json_key_is_valid(&key));
  grpc_httpcli_set_override(httpcli_get_bad_json,
                            httpcli_post_should_not_be_called);
  jwt = grpc_jwt_encode_and_sign(&key, expected_audience, expected_lifetime,
                                 nullptr);
  grpc_auth_json_key_destruct(&key);
  GPR_ASSERT(jwt != nullptr);
  grpc_jwt_verifier_verify(verifier, nullptr, jwt, expected_audience,
                           on_verification_key_retrieval_error,
                           const_cast<char*>(expected_user_data));
  grpc_jwt_verifier_destroy(verifier);
  grpc_core::ExecCtx::Get()->Flush();
  gpr_free(jwt);
  grpc_httpcli_set_override(nullptr, nullptr);
}

static void test_jwt_verifier_bad_json_key(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_jwt_verifier* verifier = grpc_jwt_verifier_create(nullptr, 0);
  char* jwt = nullptr;
  char* key_str = json_key_str(json_key_str_part3_for_google_email_issuer);
  grpc_auth_json_key key = grpc_auth_json_key_create_from_string(key_str);
  gpr_free(key_str);
  GPR_ASSERT(grpc_auth_json_key_is_valid(&key));
  grpc_httpcli_set_override(httpcli_get_bad_json,
                            httpcli_post_should_not_be_called);
  jwt = grpc_jwt_encode_and_sign(&key, expected_audience, expected_lifetime,
                                 nullptr);
  grpc_auth_json_key_destruct(&key);
  GPR_ASSERT(jwt != nullptr);
  grpc_jwt_verifier_verify(verifier, nullptr, jwt, expected_audience,
                           on_verification_key_retrieval_error,
                           const_cast<char*>(expected_user_data));
  grpc_jwt_verifier_destroy(verifier);
  grpc_core::ExecCtx::Get()->Flush();
  gpr_free(jwt);
  grpc_httpcli_set_override(nullptr, nullptr);
}

static void corrupt_jwt_sig(char* jwt) {
  grpc_slice sig;
  char* bad_b64_sig;
  uint8_t* sig_bytes;
  char* last_dot = strrchr(jwt, '.');
  GPR_ASSERT(last_dot != nullptr);
  {
    grpc_core::ExecCtx exec_ctx;
    sig = grpc_base64_decode(last_dot + 1, 1);
  }
  GPR_ASSERT(!GRPC_SLICE_IS_EMPTY(sig));
  sig_bytes = GRPC_SLICE_START_PTR(sig);
  (*sig_bytes)++; /* Corrupt first byte. */
  bad_b64_sig = grpc_base64_encode(GRPC_SLICE_START_PTR(sig),
                                   GRPC_SLICE_LENGTH(sig), 1, 0);
  memcpy(last_dot + 1, bad_b64_sig, strlen(bad_b64_sig));
  gpr_free(bad_b64_sig);
  grpc_slice_unref(sig);
}

static void on_verification_bad_signature(void* user_data,
                                          grpc_jwt_verifier_status status,
                                          grpc_jwt_claims* claims) {
  GPR_ASSERT(status == GRPC_JWT_VERIFIER_BAD_SIGNATURE);
  GPR_ASSERT(claims == nullptr);
  GPR_ASSERT(user_data == (void*)expected_user_data);
}

static void test_jwt_verifier_bad_signature(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_jwt_verifier* verifier = grpc_jwt_verifier_create(nullptr, 0);
  char* jwt = nullptr;
  char* key_str = json_key_str(json_key_str_part3_for_url_issuer);
  grpc_auth_json_key key = grpc_auth_json_key_create_from_string(key_str);
  gpr_free(key_str);
  GPR_ASSERT(grpc_auth_json_key_is_valid(&key));
  grpc_httpcli_set_override(httpcli_get_openid_config,
                            httpcli_post_should_not_be_called);
  jwt = grpc_jwt_encode_and_sign(&key, expected_audience, expected_lifetime,
                                 nullptr);
  grpc_auth_json_key_destruct(&key);
  corrupt_jwt_sig(jwt);
  GPR_ASSERT(jwt != nullptr);
  grpc_jwt_verifier_verify(verifier, nullptr, jwt, expected_audience,
                           on_verification_bad_signature,
                           const_cast<char*>(expected_user_data));
  gpr_free(jwt);
  grpc_jwt_verifier_destroy(verifier);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_httpcli_set_override(nullptr, nullptr);
}

static int httpcli_get_should_not_be_called(
    const grpc_httpcli_request* /*request*/, grpc_millis /*deadline*/,
    grpc_closure* /*on_done*/, grpc_httpcli_response* /*response*/) {
  GPR_ASSERT(0);
  return 1;
}

static void on_verification_bad_format(void* user_data,
                                       grpc_jwt_verifier_status status,
                                       grpc_jwt_claims* claims) {
  GPR_ASSERT(status == GRPC_JWT_VERIFIER_BAD_FORMAT);
  GPR_ASSERT(claims == nullptr);
  GPR_ASSERT(user_data == (void*)expected_user_data);
}

static void test_jwt_verifier_bad_format(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_jwt_verifier* verifier = grpc_jwt_verifier_create(nullptr, 0);
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  grpc_jwt_verifier_verify(verifier, nullptr, "bad jwt", expected_audience,
                           on_verification_bad_format,
                           const_cast<char*>(expected_user_data));
  grpc_jwt_verifier_destroy(verifier);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_httpcli_set_override(nullptr, nullptr);
}

/* find verification key: bad jks, cannot find key in jks */
/* bad signature custom provided email*/
/* bad key */

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_jwt_issuer_email_domain();
  test_claims_success();
  test_expired_claims_failure();
  test_invalid_claims_failure();
  test_bad_audience_claims_failure();
  test_bad_subject_claims_failure();
  test_jwt_verifier_google_email_issuer_success();
  test_jwt_verifier_custom_email_issuer_success();
  test_jwt_verifier_url_issuer_success();
  test_jwt_verifier_url_issuer_bad_config();
  test_jwt_verifier_bad_json_key();
  test_jwt_verifier_bad_signature();
  test_jwt_verifier_bad_format();
  grpc_shutdown();
  return 0;
}
