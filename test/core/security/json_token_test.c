/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/lib/security/credentials/jwt/json_token.h"

#include <openssl/evp.h>
#include <string.h>

#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"
#include "src/core/lib/slice/b64.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

/* This JSON key was generated with the GCE console and revoked immediately.
   The identifiers have been changed as well.
   Maximum size for a string literal is 509 chars in C89, yay!  */
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

/* Test refresh token. */
static const char test_refresh_token_str[] =
    "{ \"client_id\": \"32555999999.apps.googleusercontent.com\","
    "  \"client_secret\": \"EmssLNjJy1332hD4KFsecret\","
    "  \"refresh_token\": \"1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42\","
    "  \"type\": \"authorized_user\"}";

static const char test_scope[] = "myperm1 myperm2";

static const char test_service_url[] = "https://foo.com/foo.v1";

static char *test_json_key_str(const char *bad_part3) {
  const char *part3 = bad_part3 != NULL ? bad_part3 : test_json_key_str_part3;
  size_t result_len = strlen(test_json_key_str_part1) +
                      strlen(test_json_key_str_part2) + strlen(part3);
  char *result = gpr_malloc(result_len + 1);
  char *current = result;
  strcpy(result, test_json_key_str_part1);
  current += strlen(test_json_key_str_part1);
  strcpy(current, test_json_key_str_part2);
  current += strlen(test_json_key_str_part2);
  strcpy(current, part3);
  return result;
}

static void test_parse_json_key_success(void) {
  char *json_string = test_json_key_str(NULL);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  GPR_ASSERT(grpc_auth_json_key_is_valid(&json_key));
  GPR_ASSERT(json_key.type != NULL &&
             strcmp(json_key.type, "service_account") == 0);
  GPR_ASSERT(json_key.private_key_id != NULL &&
             strcmp(json_key.private_key_id,
                    "e6b5137873db8d2ef81e06a47289e6434ec8a165") == 0);
  GPR_ASSERT(json_key.client_id != NULL &&
             strcmp(json_key.client_id,
                    "777-abaslkan11hlb6nmim3bpspl31ud.apps."
                    "googleusercontent.com") == 0);
  GPR_ASSERT(json_key.client_email != NULL &&
             strcmp(json_key.client_email,
                    "777-abaslkan11hlb6nmim3bpspl31ud@developer."
                    "gserviceaccount.com") == 0);
  GPR_ASSERT(json_key.private_key != NULL);
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

static void test_parse_json_key_failure_bad_json(void) {
  const char non_closing_part3[] =
      "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", \"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\", \"type\": \"service_account\" ";
  char *json_string = test_json_key_str(non_closing_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  GPR_ASSERT(!grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

static void test_parse_json_key_failure_no_type(void) {
  const char no_type_part3[] =
      "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", \"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\" }";
  char *json_string = test_json_key_str(no_type_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  GPR_ASSERT(!grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

static void test_parse_json_key_failure_no_client_id(void) {
  const char no_client_id_part3[] =
      "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", "
      "\"type\": \"service_account\" }";
  char *json_string = test_json_key_str(no_client_id_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  GPR_ASSERT(!grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

static void test_parse_json_key_failure_no_client_email(void) {
  const char no_client_email_part3[] =
      "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\", \"type\": \"service_account\" }";
  char *json_string = test_json_key_str(no_client_email_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  GPR_ASSERT(!grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

static void test_parse_json_key_failure_no_private_key_id(void) {
  const char no_private_key_id_part3[] =
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", \"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\", \"type\": \"service_account\" }";
  char *json_string = test_json_key_str(no_private_key_id_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  GPR_ASSERT(!grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

static void test_parse_json_key_failure_no_private_key(void) {
  const char no_private_key_json_string[] =
      "{ \"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", \"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\", \"type\": \"service_account\" }";
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(no_private_key_json_string);
  GPR_ASSERT(!grpc_auth_json_key_is_valid(&json_key));
  grpc_auth_json_key_destruct(&json_key);
}

static grpc_json *parse_json_part_from_jwt(const char *str, size_t len,
                                           char **scratchpad) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  char *b64;
  char *decoded;
  grpc_json *json;
  grpc_slice slice;
  b64 = gpr_malloc(len + 1);
  strncpy(b64, str, len);
  b64[len] = '\0';
  slice = grpc_base64_decode(&exec_ctx, b64, 1);
  GPR_ASSERT(!GRPC_SLICE_IS_EMPTY(slice));
  decoded = gpr_malloc(GRPC_SLICE_LENGTH(slice) + 1);
  strncpy(decoded, (const char *)GRPC_SLICE_START_PTR(slice),
          GRPC_SLICE_LENGTH(slice));
  decoded[GRPC_SLICE_LENGTH(slice)] = '\0';
  json = grpc_json_parse_string(decoded);
  gpr_free(b64);
  *scratchpad = decoded;
  grpc_slice_unref(slice);
  grpc_exec_ctx_finish(&exec_ctx);
  return json;
}

static void check_jwt_header(grpc_json *header) {
  grpc_json *ptr;
  grpc_json *alg = NULL;
  grpc_json *typ = NULL;
  grpc_json *kid = NULL;

  for (ptr = header->child; ptr; ptr = ptr->next) {
    if (strcmp(ptr->key, "alg") == 0) {
      alg = ptr;
    } else if (strcmp(ptr->key, "typ") == 0) {
      typ = ptr;
    } else if (strcmp(ptr->key, "kid") == 0) {
      kid = ptr;
    }
  }
  GPR_ASSERT(alg != NULL);
  GPR_ASSERT(alg->type == GRPC_JSON_STRING);
  GPR_ASSERT(strcmp(alg->value, "RS256") == 0);

  GPR_ASSERT(typ != NULL);
  GPR_ASSERT(typ->type == GRPC_JSON_STRING);
  GPR_ASSERT(strcmp(typ->value, "JWT") == 0);

  GPR_ASSERT(kid != NULL);
  GPR_ASSERT(kid->type == GRPC_JSON_STRING);
  GPR_ASSERT(strcmp(kid->value, "e6b5137873db8d2ef81e06a47289e6434ec8a165") ==
             0);
}

static void check_jwt_claim(grpc_json *claim, const char *expected_audience,
                            const char *expected_scope) {
  gpr_timespec expiration = gpr_time_0(GPR_CLOCK_REALTIME);
  gpr_timespec issue_time = gpr_time_0(GPR_CLOCK_REALTIME);
  gpr_timespec parsed_lifetime;
  grpc_json *iss = NULL;
  grpc_json *scope = NULL;
  grpc_json *aud = NULL;
  grpc_json *exp = NULL;
  grpc_json *iat = NULL;
  grpc_json *sub = NULL;
  grpc_json *ptr;

  for (ptr = claim->child; ptr; ptr = ptr->next) {
    if (strcmp(ptr->key, "iss") == 0) {
      iss = ptr;
    } else if (strcmp(ptr->key, "sub") == 0) {
      sub = ptr;
    } else if (strcmp(ptr->key, "scope") == 0) {
      scope = ptr;
    } else if (strcmp(ptr->key, "aud") == 0) {
      aud = ptr;
    } else if (strcmp(ptr->key, "exp") == 0) {
      exp = ptr;
    } else if (strcmp(ptr->key, "iat") == 0) {
      iat = ptr;
    }
  }

  GPR_ASSERT(iss != NULL);
  GPR_ASSERT(iss->type == GRPC_JSON_STRING);
  GPR_ASSERT(
      strcmp(
          iss->value,
          "777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount.com") ==
      0);

  if (expected_scope != NULL) {
    GPR_ASSERT(scope != NULL);
    GPR_ASSERT(sub == NULL);
    GPR_ASSERT(scope->type == GRPC_JSON_STRING);
    GPR_ASSERT(strcmp(scope->value, expected_scope) == 0);
  } else {
    /* Claims without scope must have a sub. */
    GPR_ASSERT(scope == NULL);
    GPR_ASSERT(sub != NULL);
    GPR_ASSERT(sub->type == GRPC_JSON_STRING);
    GPR_ASSERT(strcmp(iss->value, sub->value) == 0);
  }

  GPR_ASSERT(aud != NULL);
  GPR_ASSERT(aud->type == GRPC_JSON_STRING);
  GPR_ASSERT(strcmp(aud->value, expected_audience) == 0);

  GPR_ASSERT(exp != NULL);
  GPR_ASSERT(exp->type == GRPC_JSON_NUMBER);
  expiration.tv_sec = strtol(exp->value, NULL, 10);

  GPR_ASSERT(iat != NULL);
  GPR_ASSERT(iat->type == GRPC_JSON_NUMBER);
  issue_time.tv_sec = strtol(iat->value, NULL, 10);

  parsed_lifetime = gpr_time_sub(expiration, issue_time);
  GPR_ASSERT(parsed_lifetime.tv_sec == grpc_max_auth_token_lifetime().tv_sec);
}

static void check_jwt_signature(const char *b64_signature, RSA *rsa_key,
                                const char *signed_data,
                                size_t signed_data_size) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  EVP_MD_CTX *md_ctx = EVP_MD_CTX_create();
  EVP_PKEY *key = EVP_PKEY_new();

  grpc_slice sig = grpc_base64_decode(&exec_ctx, b64_signature, 1);
  GPR_ASSERT(!GRPC_SLICE_IS_EMPTY(sig));
  GPR_ASSERT(GRPC_SLICE_LENGTH(sig) == 128);

  GPR_ASSERT(md_ctx != NULL);
  GPR_ASSERT(key != NULL);
  EVP_PKEY_set1_RSA(key, rsa_key);

  GPR_ASSERT(EVP_DigestVerifyInit(md_ctx, NULL, EVP_sha256(), NULL, key) == 1);
  GPR_ASSERT(EVP_DigestVerifyUpdate(md_ctx, signed_data, signed_data_size) ==
             1);
  GPR_ASSERT(EVP_DigestVerifyFinal(md_ctx, GRPC_SLICE_START_PTR(sig),
                                   GRPC_SLICE_LENGTH(sig)) == 1);

  grpc_slice_unref_internal(&exec_ctx, sig);
  if (key != NULL) EVP_PKEY_free(key);
  if (md_ctx != NULL) EVP_MD_CTX_destroy(md_ctx);

  grpc_exec_ctx_finish(&exec_ctx);
}

static char *service_account_creds_jwt_encode_and_sign(
    const grpc_auth_json_key *key) {
  return grpc_jwt_encode_and_sign(key, GRPC_JWT_OAUTH2_AUDIENCE,
                                  grpc_max_auth_token_lifetime(), test_scope);
}

static char *jwt_creds_jwt_encode_and_sign(const grpc_auth_json_key *key) {
  return grpc_jwt_encode_and_sign(key, test_service_url,
                                  grpc_max_auth_token_lifetime(), NULL);
}

static void service_account_creds_check_jwt_claim(grpc_json *claim) {
  check_jwt_claim(claim, GRPC_JWT_OAUTH2_AUDIENCE, test_scope);
}

static void jwt_creds_check_jwt_claim(grpc_json *claim) {
  check_jwt_claim(claim, test_service_url, NULL);
}

static void test_jwt_encode_and_sign(
    char *(*jwt_encode_and_sign_func)(const grpc_auth_json_key *),
    void (*check_jwt_claim_func)(grpc_json *)) {
  char *json_string = test_json_key_str(NULL);
  grpc_json *parsed_header = NULL;
  grpc_json *parsed_claim = NULL;
  char *scratchpad;
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  const char *b64_signature;
  size_t offset = 0;
  char *jwt = jwt_encode_and_sign_func(&json_key);
  const char *dot = strchr(jwt, '.');
  GPR_ASSERT(dot != NULL);
  parsed_header =
      parse_json_part_from_jwt(jwt, (size_t)(dot - jwt), &scratchpad);
  GPR_ASSERT(parsed_header != NULL);
  check_jwt_header(parsed_header);
  offset = (size_t)(dot - jwt) + 1;
  grpc_json_destroy(parsed_header);
  gpr_free(scratchpad);

  dot = strchr(jwt + offset, '.');
  GPR_ASSERT(dot != NULL);
  parsed_claim = parse_json_part_from_jwt(
      jwt + offset, (size_t)(dot - (jwt + offset)), &scratchpad);
  GPR_ASSERT(parsed_claim != NULL);
  check_jwt_claim_func(parsed_claim);
  offset = (size_t)(dot - jwt) + 1;
  grpc_json_destroy(parsed_claim);
  gpr_free(scratchpad);

  dot = strchr(jwt + offset, '.');
  GPR_ASSERT(dot == NULL); /* no more part. */
  b64_signature = jwt + offset;
  check_jwt_signature(b64_signature, json_key.private_key, jwt, offset - 1);

  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
  gpr_free(jwt);
}

static void test_service_account_creds_jwt_encode_and_sign(void) {
  test_jwt_encode_and_sign(service_account_creds_jwt_encode_and_sign,
                           service_account_creds_check_jwt_claim);
}

static void test_jwt_creds_jwt_encode_and_sign(void) {
  test_jwt_encode_and_sign(jwt_creds_jwt_encode_and_sign,
                           jwt_creds_check_jwt_claim);
}

static void test_parse_refresh_token_success(void) {
  grpc_auth_refresh_token refresh_token =
      grpc_auth_refresh_token_create_from_string(test_refresh_token_str);
  GPR_ASSERT(grpc_auth_refresh_token_is_valid(&refresh_token));
  GPR_ASSERT(refresh_token.type != NULL &&
             (strcmp(refresh_token.type, "authorized_user") == 0));
  GPR_ASSERT(refresh_token.client_id != NULL &&
             (strcmp(refresh_token.client_id,
                     "32555999999.apps.googleusercontent.com") == 0));
  GPR_ASSERT(
      refresh_token.client_secret != NULL &&
      (strcmp(refresh_token.client_secret, "EmssLNjJy1332hD4KFsecret") == 0));
  GPR_ASSERT(refresh_token.refresh_token != NULL &&
             (strcmp(refresh_token.refresh_token,
                     "1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42") == 0));
  grpc_auth_refresh_token_destruct(&refresh_token);
}

static void test_parse_refresh_token_failure_no_type(void) {
  const char refresh_token_str[] =
      "{ \"client_id\": \"32555999999.apps.googleusercontent.com\","
      "  \"client_secret\": \"EmssLNjJy1332hD4KFsecret\","
      "  \"refresh_token\": \"1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42\"}";
  grpc_auth_refresh_token refresh_token =
      grpc_auth_refresh_token_create_from_string(refresh_token_str);
  GPR_ASSERT(!grpc_auth_refresh_token_is_valid(&refresh_token));
}

static void test_parse_refresh_token_failure_no_client_id(void) {
  const char refresh_token_str[] =
      "{ \"client_secret\": \"EmssLNjJy1332hD4KFsecret\","
      "  \"refresh_token\": \"1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42\","
      "  \"type\": \"authorized_user\"}";
  grpc_auth_refresh_token refresh_token =
      grpc_auth_refresh_token_create_from_string(refresh_token_str);
  GPR_ASSERT(!grpc_auth_refresh_token_is_valid(&refresh_token));
}

static void test_parse_refresh_token_failure_no_client_secret(void) {
  const char refresh_token_str[] =
      "{ \"client_id\": \"32555999999.apps.googleusercontent.com\","
      "  \"refresh_token\": \"1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42\","
      "  \"type\": \"authorized_user\"}";
  grpc_auth_refresh_token refresh_token =
      grpc_auth_refresh_token_create_from_string(refresh_token_str);
  GPR_ASSERT(!grpc_auth_refresh_token_is_valid(&refresh_token));
}

static void test_parse_refresh_token_failure_no_refresh_token(void) {
  const char refresh_token_str[] =
      "{ \"client_id\": \"32555999999.apps.googleusercontent.com\","
      "  \"client_secret\": \"EmssLNjJy1332hD4KFsecret\","
      "  \"type\": \"authorized_user\"}";
  grpc_auth_refresh_token refresh_token =
      grpc_auth_refresh_token_create_from_string(refresh_token_str);
  GPR_ASSERT(!grpc_auth_refresh_token_is_valid(&refresh_token));
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_parse_json_key_success();
  test_parse_json_key_failure_bad_json();
  test_parse_json_key_failure_no_type();
  test_parse_json_key_failure_no_client_id();
  test_parse_json_key_failure_no_client_email();
  test_parse_json_key_failure_no_private_key_id();
  test_parse_json_key_failure_no_private_key();
  test_service_account_creds_jwt_encode_and_sign();
  test_jwt_creds_jwt_encode_and_sign();
  test_parse_refresh_token_success();
  test_parse_refresh_token_failure_no_type();
  test_parse_refresh_token_failure_no_client_id();
  test_parse_refresh_token_failure_no_client_secret();
  test_parse_refresh_token_failure_no_refresh_token();
  return 0;
}
