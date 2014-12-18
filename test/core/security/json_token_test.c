/*
 *
 * Copyright 2014, Google Inc.
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

#include "src/core/security/json_token.h"

#include <string.h>

#include "src/core/security/base64.h"
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>
#include "test/core/util/test_config.h"
#include "third_party/cJSON/cJSON.h"
#include <openssl/evp.h>

/* This JSON key was generated with the GCE console and revoked immediately.
   The identifiers have been changed as well.
   Maximum size for a string literal is 509 chars in C89, yay!  */
static const char test_json_key_str_part1[] =
    "{ \"private_key\": \"-----BEGIN PRIVATE KEY-----"
    "\nMIICeAIBADANBgkqhkiG9w0BAQEFAASCAmIwggJeAgEAAoGBAOEvJsnoHnyHkXcp\n7mJEqg"
    "WGjiw71NfXByguekSKho65FxaGbsnSM9SMQAqVk7Q2rG+I0OpsT0LrWQtZ\nyjSeg/"
    "rWBQvS4hle4LfijkP3J5BG+"
    "IXDMP8RfziNRQsenAXDNPkY4kJCvKux2xdD\nOnVF6N7dL3nTYZg+"
    "uQrNsMTz9UxVAgMBAAECgYEAzbLewe1xe9vy+2GoSsfib+28\nDZgSE6Bu/"
    "zuFoPrRc6qL9p2SsnV7txrunTyJkkOnPLND9ABAXybRTlcVKP/sGgza\n/"
    "8HpCqFYM9V8f34SBWfD4fRFT+n/"
    "73cfRUtGXdXpseva2lh8RilIQfPhNZAncenU\ngqXjDvpkypEusgXAykECQQD+";
static const char test_json_key_str_part2[] =
    "53XxNVnxBHsYb+AYEfklR96yVi8HywjVHP34+OQZ\nCslxoHQM8s+"
    "dBnjfScLu22JqkPv04xyxmt0QAKm9+vTdAkEA4ib7YvEAn2jXzcCI\nEkoy2L/"
    "XydR1GCHoacdfdAwiL2npOdnbvi4ZmdYRPY1LSTO058tQHKVXV7NLeCa3\nAARh2QJBAMKeDAG"
    "W303SQv2cZTdbeaLKJbB5drz3eo3j7dDKjrTD9JupixFbzcGw\n8FZi5c8idxiwC36kbAL6HzA"
    "ZoX+ofI0CQE6KCzPJTtYNqyShgKAZdJ8hwOcvCZtf\n6z8RJm0+"
    "6YBd38lfh5j8mZd7aHFf6I17j5AQY7oPEc47TjJj/"
    "5nZ68ECQQDvYuI3\nLyK5fS8g0SYbmPOL9TlcHDOqwG0mrX9qpg5DC2fniXNSrrZ64GTDKdzZY"
    "Ap6LI9W\nIqv4vr6y38N79TTC\n-----END PRIVATE KEY-----\n\", ";
static const char test_json_key_str_part3[] =
    "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
    "\"client_email\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
    "com\", \"client_id\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
    "com\", \"type\": \"service_account\" }";

static const char test_scope[] = "myperm1 myperm2";

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
             !(strcmp(json_key.type, "service_account")));
  GPR_ASSERT(json_key.private_key_id != NULL &&
             !strcmp(json_key.private_key_id,
                     "e6b5137873db8d2ef81e06a47289e6434ec8a165"));
  GPR_ASSERT(json_key.client_id != NULL &&
             !strcmp(json_key.client_id,
                     "777-abaslkan11hlb6nmim3bpspl31ud.apps."
                     "googleusercontent.com"));
  GPR_ASSERT(json_key.client_email != NULL &&
             !strcmp(json_key.client_email,
                     "777-abaslkan11hlb6nmim3bpspl31ud@developer."
                     "gserviceaccount.com"));
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

static cJSON *parse_json_part_from_jwt(const char *str, size_t len) {
  char *b64;
  char *decoded;
  cJSON *json;
  gpr_slice slice;
  b64 = gpr_malloc(len + 1);
  strncpy(b64, str, len);
  b64[len] = '\0';
  slice = grpc_base64_decode(b64, 1);
  GPR_ASSERT(!GPR_SLICE_IS_EMPTY(slice));
  decoded = gpr_malloc(GPR_SLICE_LENGTH(slice) + 1);
  strncpy(decoded, (const char *)GPR_SLICE_START_PTR(slice),
          GPR_SLICE_LENGTH(slice));
  decoded[GPR_SLICE_LENGTH(slice)] = '\0';
  json = cJSON_Parse(decoded);
  gpr_free(b64);
  gpr_free(decoded);
  gpr_slice_unref(slice);
  return json;
}

static void check_jwt_header(cJSON *header) {
  cJSON *child = cJSON_GetObjectItem(header, "alg");
  GPR_ASSERT(child != NULL);
  GPR_ASSERT(child->type == cJSON_String);
  GPR_ASSERT(!strcmp(child->valuestring, "RS256"));

  child = cJSON_GetObjectItem(header, "typ");
  GPR_ASSERT(child != NULL);
  GPR_ASSERT(child->type == cJSON_String);
  GPR_ASSERT(!strcmp(child->valuestring, "JWT"));
}

static void check_jwt_claim(cJSON *claim) {
  gpr_timespec exp = {0, 0};
  gpr_timespec issue_time = {0, 0};
  gpr_timespec parsed_lifetime;
  cJSON *child = cJSON_GetObjectItem(claim, "iss");
  GPR_ASSERT(child != NULL);
  GPR_ASSERT(child->type == cJSON_String);
  GPR_ASSERT(
      !strcmp(
          child->valuestring,
          "777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount.com"));

  child = cJSON_GetObjectItem(claim, "scope");
  GPR_ASSERT(child != NULL);
  GPR_ASSERT(child->type == cJSON_String);
  GPR_ASSERT(!strcmp(child->valuestring, test_scope));

  child = cJSON_GetObjectItem(claim, "aud");
  GPR_ASSERT(child != NULL);
  GPR_ASSERT(child->type == cJSON_String);
  GPR_ASSERT(!strcmp(child->valuestring,
                     "https://www.googleapis.com/oauth2/v3/token"));

  child = cJSON_GetObjectItem(claim, "exp");
  GPR_ASSERT(child != NULL);
  GPR_ASSERT(child->type == cJSON_Number);
  exp.tv_sec = child->valueint;

  child = cJSON_GetObjectItem(claim, "iat");
  GPR_ASSERT(child != NULL);
  GPR_ASSERT(child->type == cJSON_Number);
  issue_time.tv_sec = child->valueint;

  parsed_lifetime = gpr_time_sub(exp, issue_time);
  GPR_ASSERT(parsed_lifetime.tv_sec == grpc_max_auth_token_lifetime.tv_sec);
}

static void check_jwt_signature(const char *b64_signature, RSA *rsa_key,
                                const char *signed_data,
                                size_t signed_data_size) {
  EVP_MD_CTX *md_ctx = EVP_MD_CTX_create();
  EVP_PKEY *key = EVP_PKEY_new();

  gpr_slice sig = grpc_base64_decode(b64_signature, 1);
  GPR_ASSERT(!GPR_SLICE_IS_EMPTY(sig));
  GPR_ASSERT(GPR_SLICE_LENGTH(sig) == 128);

  GPR_ASSERT(md_ctx != NULL);
  GPR_ASSERT(key != NULL);
  EVP_PKEY_set1_RSA(key, rsa_key);

  GPR_ASSERT(EVP_DigestVerifyInit(md_ctx, NULL, EVP_sha256(), NULL, key) == 1);
  GPR_ASSERT(EVP_DigestVerifyUpdate(md_ctx, signed_data, signed_data_size) ==
             1);
  GPR_ASSERT(EVP_DigestVerifyFinal(md_ctx, GPR_SLICE_START_PTR(sig),
                                   GPR_SLICE_LENGTH(sig)) == 1);

  gpr_slice_unref(sig);
  if (key != NULL) EVP_PKEY_free(key);
  if (md_ctx != NULL) EVP_MD_CTX_destroy(md_ctx);
}

static void test_jwt_encode_and_sign(void) {
  char *json_string = test_json_key_str(NULL);
  cJSON *parsed_header = NULL;
  cJSON *parsed_claim = NULL;
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  const char *b64_signature;
  size_t offset = 0;
  char *jwt = grpc_jwt_encode_and_sign(&json_key, test_scope,
                                       grpc_max_auth_token_lifetime);
  const char *dot = strchr(jwt, '.');
  GPR_ASSERT(dot != NULL);
  parsed_header = parse_json_part_from_jwt(jwt, dot - jwt);
  GPR_ASSERT(parsed_header != NULL);
  check_jwt_header(parsed_header);
  offset = dot - jwt + 1;

  dot = strchr(jwt + offset, '.');
  GPR_ASSERT(dot != NULL);
  parsed_claim = parse_json_part_from_jwt(jwt + offset, dot - (jwt + offset));
  GPR_ASSERT(parsed_claim != NULL);
  check_jwt_claim(parsed_claim);
  offset = dot - jwt + 1;

  dot = strchr(jwt + offset, '.');
  GPR_ASSERT(dot == NULL); /* no more part. */
  b64_signature = jwt + offset;
  check_jwt_signature(b64_signature, json_key.private_key, jwt, offset - 1);

  gpr_free(json_string);
  cJSON_Delete(parsed_header);
  cJSON_Delete(parsed_claim);
  grpc_auth_json_key_destruct(&json_key);
  gpr_free(jwt);
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
  test_jwt_encode_and_sign();
  return 0;
}
