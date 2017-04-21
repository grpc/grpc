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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/credentials.h"

#include <openssl/rsa.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/security/credentials/composite/composite_credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/credentials/google_default/google_default_credentials.h"
#include "src/core/lib/security/credentials/jwt/jwt_credentials.h"
#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/support/tmpfile.h"
#include "test/core/util/test_config.h"

/* -- Mock channel credentials. -- */

static grpc_channel_credentials *grpc_mock_channel_credentials_create(
    const grpc_channel_credentials_vtable *vtable) {
  grpc_channel_credentials *c = gpr_malloc(sizeof(*c));
  memset(c, 0, sizeof(*c));
  c->type = "mock";
  c->vtable = vtable;
  gpr_ref_init(&c->refcount, 1);
  return c;
}

/* -- Constants. -- */

static const char test_google_iam_authorization_token[] = "blahblahblhahb";
static const char test_google_iam_authority_selector[] = "respectmyauthoritah";
static const char test_oauth2_bearer_token[] =
    "Bearer blaaslkdjfaslkdfasdsfasf";

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

static const char valid_oauth2_json_response[] =
    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
    " \"expires_in\":3599, "
    " \"token_type\":\"Bearer\"}";

static const char test_user_data[] = "user data";

static const char test_scope[] = "perm1 perm2";

static const char test_signed_jwt[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImY0OTRkN2M1YWU2MGRmOTcyNmM4YW"
    "U0MDcyZTViYTdmZDkwODg2YzcifQ";

static const char test_service_url[] = "https://foo.com/foo.v1";
static const char other_test_service_url[] = "https://bar.com/bar.v1";

static const char test_method[] = "ThisIsNotAMethod";

/* -- Utils. -- */

static char *test_json_key_str(void) {
  size_t result_len = strlen(test_json_key_str_part1) +
                      strlen(test_json_key_str_part2) +
                      strlen(test_json_key_str_part3);
  char *result = gpr_malloc(result_len + 1);
  char *current = result;
  strcpy(result, test_json_key_str_part1);
  current += strlen(test_json_key_str_part1);
  strcpy(current, test_json_key_str_part2);
  current += strlen(test_json_key_str_part2);
  strcpy(current, test_json_key_str_part3);
  return result;
}

typedef struct {
  const char *key;
  const char *value;
} expected_md;

static grpc_httpcli_response http_response(int status, const char *body) {
  grpc_httpcli_response response;
  memset(&response, 0, sizeof(grpc_httpcli_response));
  response.status = status;
  response.body = gpr_strdup((char *)body);
  response.body_length = strlen(body);
  return response;
}

/* -- Tests. -- */

static void test_empty_md_store(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_md_store *store = grpc_credentials_md_store_create(0);
  GPR_ASSERT(store->num_entries == 0);
  GPR_ASSERT(store->allocated == 0);
  grpc_credentials_md_store_unref(&exec_ctx, store);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_ref_unref_empty_md_store(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_md_store *store = grpc_credentials_md_store_create(0);
  grpc_credentials_md_store_ref(store);
  grpc_credentials_md_store_ref(store);
  GPR_ASSERT(store->num_entries == 0);
  GPR_ASSERT(store->allocated == 0);
  grpc_credentials_md_store_unref(&exec_ctx, store);
  grpc_credentials_md_store_unref(&exec_ctx, store);
  grpc_credentials_md_store_unref(&exec_ctx, store);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_add_to_empty_md_store(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_md_store *store = grpc_credentials_md_store_create(0);
  const char *key_str = "hello";
  const char *value_str = "there blah blah blah blah blah blah blah";
  grpc_slice key = grpc_slice_from_copied_string(key_str);
  grpc_slice value = grpc_slice_from_copied_string(value_str);
  grpc_credentials_md_store_add(store, key, value);
  GPR_ASSERT(store->num_entries == 1);
  GPR_ASSERT(grpc_slice_eq(key, store->entries[0].key));
  GPR_ASSERT(grpc_slice_eq(value, store->entries[0].value));
  grpc_slice_unref(key);
  grpc_slice_unref(value);
  grpc_credentials_md_store_unref(&exec_ctx, store);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_add_cstrings_to_empty_md_store(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_md_store *store = grpc_credentials_md_store_create(0);
  const char *key_str = "hello";
  const char *value_str = "there blah blah blah blah blah blah blah";
  grpc_credentials_md_store_add_cstrings(store, key_str, value_str);
  GPR_ASSERT(store->num_entries == 1);
  GPR_ASSERT(grpc_slice_str_cmp(store->entries[0].key, key_str) == 0);
  GPR_ASSERT(grpc_slice_str_cmp(store->entries[0].value, value_str) == 0);
  grpc_credentials_md_store_unref(&exec_ctx, store);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_empty_preallocated_md_store(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_md_store *store = grpc_credentials_md_store_create(4);
  GPR_ASSERT(store->num_entries == 0);
  GPR_ASSERT(store->allocated == 4);
  GPR_ASSERT(store->entries != NULL);
  grpc_credentials_md_store_unref(&exec_ctx, store);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_add_abunch_to_md_store(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_md_store *store = grpc_credentials_md_store_create(4);
  size_t num_entries = 1000;
  const char *key_str = "hello";
  const char *value_str = "there blah blah blah blah blah blah blah";
  size_t i;
  for (i = 0; i < num_entries; i++) {
    grpc_credentials_md_store_add_cstrings(store, key_str, value_str);
  }
  for (i = 0; i < num_entries; i++) {
    GPR_ASSERT(grpc_slice_str_cmp(store->entries[i].key, key_str) == 0);
    GPR_ASSERT(grpc_slice_str_cmp(store->entries[i].value, value_str) == 0);
  }
  grpc_credentials_md_store_unref(&exec_ctx, store);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_oauth2_token_fetcher_creds_parsing_ok(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(200, valid_oauth2_json_response);
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &exec_ctx, &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_OK);
  GPR_ASSERT(token_lifetime.tv_sec == 3599);
  GPR_ASSERT(token_lifetime.tv_nsec == 0);
  GPR_ASSERT(token_md->num_entries == 1);
  GPR_ASSERT(grpc_slice_str_cmp(token_md->entries[0].key, "authorization") ==
             0);
  GPR_ASSERT(grpc_slice_str_cmp(token_md->entries[0].value,
                                "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_") ==
             0);
  grpc_credentials_md_store_unref(&exec_ctx, token_md);
  grpc_http_response_destroy(&response);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_oauth2_token_fetcher_creds_parsing_bad_http_status(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(401, valid_oauth2_json_response);
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &exec_ctx, &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_oauth2_token_fetcher_creds_parsing_empty_http_body(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response = http_response(200, "");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &exec_ctx, &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_oauth2_token_fetcher_creds_parsing_invalid_json(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    " \"token_type\":\"Bearer\"");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &exec_ctx, &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_oauth2_token_fetcher_creds_parsing_missing_token(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response = http_response(200,
                                                 "{"
                                                 " \"expires_in\":3599, "
                                                 " \"token_type\":\"Bearer\"}");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &exec_ctx, &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_oauth2_token_fetcher_creds_parsing_missing_token_type(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    "}");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &exec_ctx, &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_oauth2_token_fetcher_creds_parsing_missing_token_lifetime(
    void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"token_type\":\"Bearer\"}");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &exec_ctx, &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void check_metadata(expected_md *expected, grpc_credentials_md *md_elems,
                           size_t num_md) {
  size_t i;
  for (i = 0; i < num_md; i++) {
    size_t j;
    for (j = 0; j < num_md; j++) {
      if (0 == grpc_slice_str_cmp(md_elems[j].key, expected[i].key)) {
        GPR_ASSERT(grpc_slice_str_cmp(md_elems[j].value, expected[i].value) ==
                   0);
        break;
      }
    }
    if (j == num_md) {
      gpr_log(GPR_ERROR, "key %s not found", expected[i].key);
      GPR_ASSERT(0);
    }
  }
}

static void check_google_iam_metadata(grpc_exec_ctx *exec_ctx, void *user_data,
                                      grpc_credentials_md *md_elems,
                                      size_t num_md,
                                      grpc_credentials_status status,
                                      const char *error_details) {
  grpc_call_credentials *c = (grpc_call_credentials *)user_data;
  expected_md emd[] = {{GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
                        test_google_iam_authorization_token},
                       {GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
                        test_google_iam_authority_selector}};
  GPR_ASSERT(status == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(error_details == NULL);
  GPR_ASSERT(num_md == 2);
  check_metadata(emd, md_elems, num_md);
  grpc_call_credentials_unref(exec_ctx, c);
}

static void test_google_iam_creds(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_call_credentials *creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      NULL);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method, NULL,
                                            NULL};
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, creds, NULL, auth_md_ctx, check_google_iam_metadata, creds);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void check_access_token_metadata(
    grpc_exec_ctx *exec_ctx, void *user_data, grpc_credentials_md *md_elems,
    size_t num_md, grpc_credentials_status status, const char *error_details) {
  grpc_call_credentials *c = (grpc_call_credentials *)user_data;
  expected_md emd[] = {{GRPC_AUTHORIZATION_METADATA_KEY, "Bearer blah"}};
  GPR_ASSERT(status == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(error_details == NULL);
  GPR_ASSERT(num_md == 1);
  check_metadata(emd, md_elems, num_md);
  grpc_call_credentials_unref(exec_ctx, c);
}

static void test_access_token_creds(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_call_credentials *creds =
      grpc_access_token_credentials_create("blah", NULL);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method, NULL,
                                            NULL};
  GPR_ASSERT(strcmp(creds->type, GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) == 0);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, creds, NULL, auth_md_ctx, check_access_token_metadata, creds);
  grpc_exec_ctx_finish(&exec_ctx);
}

static grpc_security_status check_channel_oauth2_create_security_connector(
    grpc_exec_ctx *exec_ctx, grpc_channel_credentials *c,
    grpc_call_credentials *call_creds, const char *target,
    const grpc_channel_args *args, grpc_channel_security_connector **sc,
    grpc_channel_args **new_args) {
  GPR_ASSERT(strcmp(c->type, "mock") == 0);
  GPR_ASSERT(call_creds != NULL);
  GPR_ASSERT(strcmp(call_creds->type, GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) == 0);
  return GRPC_SECURITY_OK;
}

static void test_channel_oauth2_composite_creds(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_channel_args *new_args;
  grpc_channel_credentials_vtable vtable = {
      NULL, check_channel_oauth2_create_security_connector, NULL};
  grpc_channel_credentials *channel_creds =
      grpc_mock_channel_credentials_create(&vtable);
  grpc_call_credentials *oauth2_creds =
      grpc_access_token_credentials_create("blah", NULL);
  grpc_channel_credentials *channel_oauth2_creds =
      grpc_composite_channel_credentials_create(channel_creds, oauth2_creds,
                                                NULL);
  grpc_channel_credentials_release(channel_creds);
  grpc_call_credentials_release(oauth2_creds);
  GPR_ASSERT(grpc_channel_credentials_create_security_connector(
                 &exec_ctx, channel_oauth2_creds, NULL, NULL, NULL,
                 &new_args) == GRPC_SECURITY_OK);
  grpc_channel_credentials_release(channel_oauth2_creds);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void check_oauth2_google_iam_composite_metadata(
    grpc_exec_ctx *exec_ctx, void *user_data, grpc_credentials_md *md_elems,
    size_t num_md, grpc_credentials_status status, const char *error_details) {
  grpc_call_credentials *c = (grpc_call_credentials *)user_data;
  expected_md emd[] = {
      {GRPC_AUTHORIZATION_METADATA_KEY, test_oauth2_bearer_token},
      {GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
       test_google_iam_authorization_token},
      {GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
       test_google_iam_authority_selector}};
  GPR_ASSERT(status == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(error_details == NULL);
  GPR_ASSERT(num_md == 3);
  check_metadata(emd, md_elems, num_md);
  grpc_call_credentials_unref(exec_ctx, c);
}

static void test_oauth2_google_iam_composite_creds(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  const grpc_call_credentials_array *creds_array;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method, NULL,
                                            NULL};
  grpc_call_credentials *oauth2_creds = grpc_md_only_test_credentials_create(
      "authorization", test_oauth2_bearer_token, 0);
  grpc_call_credentials *google_iam_creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      NULL);
  grpc_call_credentials *composite_creds =
      grpc_composite_call_credentials_create(oauth2_creds, google_iam_creds,
                                             NULL);
  grpc_call_credentials_unref(&exec_ctx, oauth2_creds);
  grpc_call_credentials_unref(&exec_ctx, google_iam_creds);
  GPR_ASSERT(
      strcmp(composite_creds->type, GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0);
  creds_array =
      grpc_composite_call_credentials_get_credentials(composite_creds);
  GPR_ASSERT(creds_array->num_creds == 2);
  GPR_ASSERT(strcmp(creds_array->creds_array[0]->type,
                    GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) == 0);
  GPR_ASSERT(strcmp(creds_array->creds_array[1]->type,
                    GRPC_CALL_CREDENTIALS_TYPE_IAM) == 0);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, composite_creds, NULL, auth_md_ctx,
      check_oauth2_google_iam_composite_metadata, composite_creds);
  grpc_exec_ctx_finish(&exec_ctx);
}

static grpc_security_status
check_channel_oauth2_google_iam_create_security_connector(
    grpc_exec_ctx *exec_ctx, grpc_channel_credentials *c,
    grpc_call_credentials *call_creds, const char *target,
    const grpc_channel_args *args, grpc_channel_security_connector **sc,
    grpc_channel_args **new_args) {
  const grpc_call_credentials_array *creds_array;
  GPR_ASSERT(strcmp(c->type, "mock") == 0);
  GPR_ASSERT(call_creds != NULL);
  GPR_ASSERT(strcmp(call_creds->type, GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) ==
             0);
  creds_array = grpc_composite_call_credentials_get_credentials(call_creds);
  GPR_ASSERT(strcmp(creds_array->creds_array[0]->type,
                    GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) == 0);
  GPR_ASSERT(strcmp(creds_array->creds_array[1]->type,
                    GRPC_CALL_CREDENTIALS_TYPE_IAM) == 0);
  return GRPC_SECURITY_OK;
}

static void test_channel_oauth2_google_iam_composite_creds(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_channel_args *new_args;
  grpc_channel_credentials_vtable vtable = {
      NULL, check_channel_oauth2_google_iam_create_security_connector, NULL};
  grpc_channel_credentials *channel_creds =
      grpc_mock_channel_credentials_create(&vtable);
  grpc_call_credentials *oauth2_creds =
      grpc_access_token_credentials_create("blah", NULL);
  grpc_channel_credentials *channel_oauth2_creds =
      grpc_composite_channel_credentials_create(channel_creds, oauth2_creds,
                                                NULL);
  grpc_call_credentials *google_iam_creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      NULL);
  grpc_channel_credentials *channel_oauth2_iam_creds =
      grpc_composite_channel_credentials_create(channel_oauth2_creds,
                                                google_iam_creds, NULL);
  grpc_channel_credentials_release(channel_creds);
  grpc_call_credentials_release(oauth2_creds);
  grpc_channel_credentials_release(channel_oauth2_creds);
  grpc_call_credentials_release(google_iam_creds);

  GPR_ASSERT(grpc_channel_credentials_create_security_connector(
                 &exec_ctx, channel_oauth2_iam_creds, NULL, NULL, NULL,
                 &new_args) == GRPC_SECURITY_OK);

  grpc_channel_credentials_release(channel_oauth2_iam_creds);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void on_oauth2_creds_get_metadata_success(
    grpc_exec_ctx *exec_ctx, void *user_data, grpc_credentials_md *md_elems,
    size_t num_md, grpc_credentials_status status, const char *error_details) {
  GPR_ASSERT(status == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(error_details == NULL);
  GPR_ASSERT(num_md == 1);
  GPR_ASSERT(grpc_slice_str_cmp(md_elems[0].key, "authorization") == 0);
  GPR_ASSERT(grpc_slice_str_cmp(md_elems[0].value,
                                "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_") ==
             0);
  GPR_ASSERT(user_data != NULL);
  GPR_ASSERT(strcmp((const char *)user_data, test_user_data) == 0);
}

static void on_oauth2_creds_get_metadata_failure(
    grpc_exec_ctx *exec_ctx, void *user_data, grpc_credentials_md *md_elems,
    size_t num_md, grpc_credentials_status status, const char *error_details) {
  GPR_ASSERT(status == GRPC_CREDENTIALS_ERROR);
  GPR_ASSERT(num_md == 0);
  GPR_ASSERT(user_data != NULL);
  GPR_ASSERT(strcmp((const char *)user_data, test_user_data) == 0);
}

static void validate_compute_engine_http_request(
    const grpc_httpcli_request *request) {
  GPR_ASSERT(request->handshaker != &grpc_httpcli_ssl);
  GPR_ASSERT(strcmp(request->host, "metadata.google.internal") == 0);
  GPR_ASSERT(
      strcmp(request->http.path,
             "/computeMetadata/v1/instance/service-accounts/default/token") ==
      0);
  GPR_ASSERT(request->http.hdr_count == 1);
  GPR_ASSERT(strcmp(request->http.hdrs[0].key, "Metadata-Flavor") == 0);
  GPR_ASSERT(strcmp(request->http.hdrs[0].value, "Google") == 0);
}

static int compute_engine_httpcli_get_success_override(
    grpc_exec_ctx *exec_ctx, const grpc_httpcli_request *request,
    gpr_timespec deadline, grpc_closure *on_done,
    grpc_httpcli_response *response) {
  validate_compute_engine_http_request(request);
  *response = http_response(200, valid_oauth2_json_response);
  grpc_closure_sched(exec_ctx, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int compute_engine_httpcli_get_failure_override(
    grpc_exec_ctx *exec_ctx, const grpc_httpcli_request *request,
    gpr_timespec deadline, grpc_closure *on_done,
    grpc_httpcli_response *response) {
  validate_compute_engine_http_request(request);
  *response = http_response(403, "Not Authorized.");
  grpc_closure_sched(exec_ctx, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int httpcli_post_should_not_be_called(
    grpc_exec_ctx *exec_ctx, const grpc_httpcli_request *request,
    const char *body_bytes, size_t body_size, gpr_timespec deadline,
    grpc_closure *on_done, grpc_httpcli_response *response) {
  GPR_ASSERT("HTTP POST should not be called" == NULL);
  return 1;
}

static int httpcli_get_should_not_be_called(grpc_exec_ctx *exec_ctx,
                                            const grpc_httpcli_request *request,
                                            gpr_timespec deadline,
                                            grpc_closure *on_done,
                                            grpc_httpcli_response *response) {
  GPR_ASSERT("HTTP GET should not be called" == NULL);
  return 1;
}

static void test_compute_engine_creds_success(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_call_credentials *compute_engine_creds =
      grpc_google_compute_engine_credentials_create(NULL);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method, NULL,
                                            NULL};

  /* First request: http get should be called. */
  grpc_httpcli_set_override(compute_engine_httpcli_get_success_override,
                            httpcli_post_should_not_be_called);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, compute_engine_creds, NULL, auth_md_ctx,
      on_oauth2_creds_get_metadata_success, (void *)test_user_data);
  grpc_exec_ctx_flush(&exec_ctx);

  /* Second request: the cached token should be served directly. */
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, compute_engine_creds, NULL, auth_md_ctx,
      on_oauth2_creds_get_metadata_success, (void *)test_user_data);
  grpc_exec_ctx_finish(&exec_ctx);

  grpc_call_credentials_unref(&exec_ctx, compute_engine_creds);
  grpc_httpcli_set_override(NULL, NULL);
}

static void test_compute_engine_creds_failure(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method, NULL,
                                            NULL};
  grpc_call_credentials *compute_engine_creds =
      grpc_google_compute_engine_credentials_create(NULL);
  grpc_httpcli_set_override(compute_engine_httpcli_get_failure_override,
                            httpcli_post_should_not_be_called);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, compute_engine_creds, NULL, auth_md_ctx,
      on_oauth2_creds_get_metadata_failure, (void *)test_user_data);
  grpc_call_credentials_unref(&exec_ctx, compute_engine_creds);
  grpc_httpcli_set_override(NULL, NULL);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void validate_refresh_token_http_request(
    const grpc_httpcli_request *request, const char *body, size_t body_size) {
  /* The content of the assertion is tested extensively in json_token_test. */
  char *expected_body = NULL;
  GPR_ASSERT(body != NULL);
  GPR_ASSERT(body_size != 0);
  gpr_asprintf(&expected_body, GRPC_REFRESH_TOKEN_POST_BODY_FORMAT_STRING,
               "32555999999.apps.googleusercontent.com",
               "EmssLNjJy1332hD4KFsecret",
               "1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42");
  GPR_ASSERT(strlen(expected_body) == body_size);
  GPR_ASSERT(memcmp(expected_body, body, body_size) == 0);
  gpr_free(expected_body);
  GPR_ASSERT(request->handshaker == &grpc_httpcli_ssl);
  GPR_ASSERT(strcmp(request->host, GRPC_GOOGLE_OAUTH2_SERVICE_HOST) == 0);
  GPR_ASSERT(
      strcmp(request->http.path, GRPC_GOOGLE_OAUTH2_SERVICE_TOKEN_PATH) == 0);
  GPR_ASSERT(request->http.hdr_count == 1);
  GPR_ASSERT(strcmp(request->http.hdrs[0].key, "Content-Type") == 0);
  GPR_ASSERT(strcmp(request->http.hdrs[0].value,
                    "application/x-www-form-urlencoded") == 0);
}

static int refresh_token_httpcli_post_success(
    grpc_exec_ctx *exec_ctx, const grpc_httpcli_request *request,
    const char *body, size_t body_size, gpr_timespec deadline,
    grpc_closure *on_done, grpc_httpcli_response *response) {
  validate_refresh_token_http_request(request, body, body_size);
  *response = http_response(200, valid_oauth2_json_response);
  grpc_closure_sched(exec_ctx, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int refresh_token_httpcli_post_failure(
    grpc_exec_ctx *exec_ctx, const grpc_httpcli_request *request,
    const char *body, size_t body_size, gpr_timespec deadline,
    grpc_closure *on_done, grpc_httpcli_response *response) {
  validate_refresh_token_http_request(request, body, body_size);
  *response = http_response(403, "Not Authorized.");
  grpc_closure_sched(exec_ctx, on_done, GRPC_ERROR_NONE);
  return 1;
}

static void test_refresh_token_creds_success(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method, NULL,
                                            NULL};
  grpc_call_credentials *refresh_token_creds =
      grpc_google_refresh_token_credentials_create(test_refresh_token_str,
                                                   NULL);

  /* First request: http get should be called. */
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            refresh_token_httpcli_post_success);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, refresh_token_creds, NULL, auth_md_ctx,
      on_oauth2_creds_get_metadata_success, (void *)test_user_data);
  grpc_exec_ctx_flush(&exec_ctx);

  /* Second request: the cached token should be served directly. */
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, refresh_token_creds, NULL, auth_md_ctx,
      on_oauth2_creds_get_metadata_success, (void *)test_user_data);
  grpc_exec_ctx_flush(&exec_ctx);

  grpc_call_credentials_unref(&exec_ctx, refresh_token_creds);
  grpc_httpcli_set_override(NULL, NULL);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_refresh_token_creds_failure(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method, NULL,
                                            NULL};
  grpc_call_credentials *refresh_token_creds =
      grpc_google_refresh_token_credentials_create(test_refresh_token_str,
                                                   NULL);
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            refresh_token_httpcli_post_failure);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, refresh_token_creds, NULL, auth_md_ctx,
      on_oauth2_creds_get_metadata_failure, (void *)test_user_data);
  grpc_call_credentials_unref(&exec_ctx, refresh_token_creds);
  grpc_httpcli_set_override(NULL, NULL);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void validate_jwt_encode_and_sign_params(
    const grpc_auth_json_key *json_key, const char *scope,
    gpr_timespec token_lifetime) {
  GPR_ASSERT(grpc_auth_json_key_is_valid(json_key));
  GPR_ASSERT(json_key->private_key != NULL);
  GPR_ASSERT(RSA_check_key(json_key->private_key));
  GPR_ASSERT(json_key->type != NULL &&
             strcmp(json_key->type, "service_account") == 0);
  GPR_ASSERT(json_key->private_key_id != NULL &&
             strcmp(json_key->private_key_id,
                    "e6b5137873db8d2ef81e06a47289e6434ec8a165") == 0);
  GPR_ASSERT(json_key->client_id != NULL &&
             strcmp(json_key->client_id,
                    "777-abaslkan11hlb6nmim3bpspl31ud.apps."
                    "googleusercontent.com") == 0);
  GPR_ASSERT(json_key->client_email != NULL &&
             strcmp(json_key->client_email,
                    "777-abaslkan11hlb6nmim3bpspl31ud@developer."
                    "gserviceaccount.com") == 0);
  if (scope != NULL) GPR_ASSERT(strcmp(scope, test_scope) == 0);
  GPR_ASSERT(!gpr_time_cmp(token_lifetime, grpc_max_auth_token_lifetime()));
}

static char *encode_and_sign_jwt_success(const grpc_auth_json_key *json_key,
                                         const char *audience,
                                         gpr_timespec token_lifetime,
                                         const char *scope) {
  validate_jwt_encode_and_sign_params(json_key, scope, token_lifetime);
  return gpr_strdup(test_signed_jwt);
}

static char *encode_and_sign_jwt_failure(const grpc_auth_json_key *json_key,
                                         const char *audience,
                                         gpr_timespec token_lifetime,
                                         const char *scope) {
  validate_jwt_encode_and_sign_params(json_key, scope, token_lifetime);
  return NULL;
}

static char *encode_and_sign_jwt_should_not_be_called(
    const grpc_auth_json_key *json_key, const char *audience,
    gpr_timespec token_lifetime, const char *scope) {
  GPR_ASSERT("grpc_jwt_encode_and_sign should not be called" == NULL);
  return NULL;
}

static void on_jwt_creds_get_metadata_success(
    grpc_exec_ctx *exec_ctx, void *user_data, grpc_credentials_md *md_elems,
    size_t num_md, grpc_credentials_status status, const char *error_details) {
  char *expected_md_value;
  gpr_asprintf(&expected_md_value, "Bearer %s", test_signed_jwt);
  GPR_ASSERT(status == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(error_details == NULL);
  GPR_ASSERT(num_md == 1);
  GPR_ASSERT(grpc_slice_str_cmp(md_elems[0].key, "authorization") == 0);
  GPR_ASSERT(grpc_slice_str_cmp(md_elems[0].value, expected_md_value) == 0);
  GPR_ASSERT(user_data != NULL);
  GPR_ASSERT(strcmp((const char *)user_data, test_user_data) == 0);
  gpr_free(expected_md_value);
}

static void on_jwt_creds_get_metadata_failure(
    grpc_exec_ctx *exec_ctx, void *user_data, grpc_credentials_md *md_elems,
    size_t num_md, grpc_credentials_status status, const char *error_details) {
  GPR_ASSERT(status == GRPC_CREDENTIALS_ERROR);
  GPR_ASSERT(num_md == 0);
  GPR_ASSERT(user_data != NULL);
  GPR_ASSERT(strcmp((const char *)user_data, test_user_data) == 0);
}

static void test_jwt_creds_success(void) {
  char *json_key_string = test_json_key_str();
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method, NULL,
                                            NULL};
  grpc_call_credentials *jwt_creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime(), NULL);

  /* First request: jwt_encode_and_sign should be called. */
  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_success);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, jwt_creds, NULL, auth_md_ctx,
      on_jwt_creds_get_metadata_success, (void *)test_user_data);
  grpc_exec_ctx_flush(&exec_ctx);

  /* Second request: the cached token should be served directly. */
  grpc_jwt_encode_and_sign_set_override(
      encode_and_sign_jwt_should_not_be_called);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, jwt_creds, NULL, auth_md_ctx,
      on_jwt_creds_get_metadata_success, (void *)test_user_data);
  grpc_exec_ctx_flush(&exec_ctx);

  /* Third request: Different service url so jwt_encode_and_sign should be
     called again (no caching). */
  auth_md_ctx.service_url = other_test_service_url;
  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_success);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, jwt_creds, NULL, auth_md_ctx,
      on_jwt_creds_get_metadata_success, (void *)test_user_data);
  grpc_exec_ctx_flush(&exec_ctx);

  gpr_free(json_key_string);
  grpc_call_credentials_unref(&exec_ctx, jwt_creds);
  grpc_jwt_encode_and_sign_set_override(NULL);
}

static void test_jwt_creds_signing_failure(void) {
  char *json_key_string = test_json_key_str();
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method, NULL,
                                            NULL};
  grpc_call_credentials *jwt_creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime(), NULL);

  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_failure);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, jwt_creds, NULL, auth_md_ctx,
      on_jwt_creds_get_metadata_failure, (void *)test_user_data);

  gpr_free(json_key_string);
  grpc_call_credentials_unref(&exec_ctx, jwt_creds);
  grpc_jwt_encode_and_sign_set_override(NULL);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void set_google_default_creds_env_var_with_file_contents(
    const char *file_prefix, const char *contents) {
  size_t contents_len = strlen(contents);
  char *creds_file_name;
  FILE *creds_file = gpr_tmpfile(file_prefix, &creds_file_name);
  GPR_ASSERT(creds_file_name != NULL);
  GPR_ASSERT(creds_file != NULL);
  GPR_ASSERT(fwrite(contents, 1, contents_len, creds_file) == contents_len);
  fclose(creds_file);
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, creds_file_name);
  gpr_free(creds_file_name);
}

static void test_google_default_creds_auth_key(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_service_account_jwt_access_credentials *jwt;
  grpc_composite_channel_credentials *creds;
  char *json_key = test_json_key_str();
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "json_key_google_default_creds", json_key);
  gpr_free(json_key);
  creds = (grpc_composite_channel_credentials *)
      grpc_google_default_credentials_create();
  GPR_ASSERT(creds != NULL);
  jwt = (grpc_service_account_jwt_access_credentials *)creds->call_creds;
  GPR_ASSERT(
      strcmp(jwt->key.client_id,
             "777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent.com") ==
      0);
  grpc_channel_credentials_unref(&exec_ctx, &creds->base);
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_google_default_creds_refresh_token(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_google_refresh_token_credentials *refresh;
  grpc_composite_channel_credentials *creds;
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "refresh_token_google_default_creds", test_refresh_token_str);
  creds = (grpc_composite_channel_credentials *)
      grpc_google_default_credentials_create();
  GPR_ASSERT(creds != NULL);
  refresh = (grpc_google_refresh_token_credentials *)creds->call_creds;
  GPR_ASSERT(strcmp(refresh->refresh_token.client_id,
                    "32555999999.apps.googleusercontent.com") == 0);
  grpc_channel_credentials_unref(&exec_ctx, &creds->base);
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
  grpc_exec_ctx_finish(&exec_ctx);
}

static int default_creds_gce_detection_httpcli_get_success_override(
    grpc_exec_ctx *exec_ctx, const grpc_httpcli_request *request,
    gpr_timespec deadline, grpc_closure *on_done,
    grpc_httpcli_response *response) {
  *response = http_response(200, "");
  grpc_http_header *headers = gpr_malloc(sizeof(*headers) * 1);
  headers[0].key = gpr_strdup("Metadata-Flavor");
  headers[0].value = gpr_strdup("Google");
  response->hdr_count = 1;
  response->hdrs = headers;
  GPR_ASSERT(strcmp(request->http.path, "/") == 0);
  GPR_ASSERT(strcmp(request->host, "metadata.google.internal") == 0);
  grpc_closure_sched(exec_ctx, on_done, GRPC_ERROR_NONE);
  return 1;
}

static char *null_well_known_creds_path_getter(void) { return NULL; }

static void test_google_default_creds_gce(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_composite_channel_credentials *creds;
  grpc_channel_credentials *cached_creds;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method, NULL,
                                            NULL};
  grpc_flush_cached_google_default_credentials();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);

  /* Simulate a successful detection of GCE. */
  grpc_httpcli_set_override(
      default_creds_gce_detection_httpcli_get_success_override,
      httpcli_post_should_not_be_called);
  creds = (grpc_composite_channel_credentials *)
      grpc_google_default_credentials_create();

  /* Verify that the default creds actually embeds a GCE creds. */
  GPR_ASSERT(creds != NULL);
  GPR_ASSERT(creds->call_creds != NULL);
  grpc_httpcli_set_override(compute_engine_httpcli_get_success_override,
                            httpcli_post_should_not_be_called);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, creds->call_creds, NULL, auth_md_ctx,
      on_oauth2_creds_get_metadata_success, (void *)test_user_data);
  grpc_exec_ctx_flush(&exec_ctx);
  grpc_exec_ctx_finish(&exec_ctx);

  /* Check that we get a cached creds if we call
     grpc_google_default_credentials_create again.
     GCE detection should not occur anymore either. */
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  cached_creds = grpc_google_default_credentials_create();
  GPR_ASSERT(cached_creds == &creds->base);

  /* Cleanup. */
  grpc_channel_credentials_release(cached_creds);
  grpc_channel_credentials_release(&creds->base);
  grpc_httpcli_set_override(NULL, NULL);
  grpc_override_well_known_credentials_path_getter(NULL);
}

static int default_creds_gce_detection_httpcli_get_failure_override(
    grpc_exec_ctx *exec_ctx, const grpc_httpcli_request *request,
    gpr_timespec deadline, grpc_closure *on_done,
    grpc_httpcli_response *response) {
  /* No magic header. */
  GPR_ASSERT(strcmp(request->http.path, "/") == 0);
  GPR_ASSERT(strcmp(request->host, "metadata.google.internal") == 0);
  *response = http_response(200, "");
  grpc_closure_sched(exec_ctx, on_done, GRPC_ERROR_NONE);
  return 1;
}

static void test_no_google_default_creds(void) {
  grpc_flush_cached_google_default_credentials();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);

  /* Simulate a successful detection of GCE. */
  grpc_httpcli_set_override(
      default_creds_gce_detection_httpcli_get_failure_override,
      httpcli_post_should_not_be_called);
  GPR_ASSERT(grpc_google_default_credentials_create() == NULL);

  /* Try a cached one. GCE detection should not occur anymore. */
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  GPR_ASSERT(grpc_google_default_credentials_create() == NULL);

  /* Cleanup. */
  grpc_httpcli_set_override(NULL, NULL);
  grpc_override_well_known_credentials_path_getter(NULL);
}

typedef enum {
  PLUGIN_INITIAL_STATE,
  PLUGIN_GET_METADATA_CALLED_STATE,
  PLUGIN_DESTROY_CALLED_STATE
} plugin_state;

typedef struct {
  const char *key;
  const char *value;
} plugin_metadata;

static const plugin_metadata plugin_md[] = {{"foo", "bar"}, {"hi", "there"}};

static void plugin_get_metadata_success(void *state,
                                        grpc_auth_metadata_context context,
                                        grpc_credentials_plugin_metadata_cb cb,
                                        void *user_data) {
  size_t i;
  grpc_metadata md[GPR_ARRAY_SIZE(plugin_md)];
  plugin_state *s = (plugin_state *)state;
  GPR_ASSERT(strcmp(context.service_url, test_service_url) == 0);
  GPR_ASSERT(strcmp(context.method_name, test_method) == 0);
  GPR_ASSERT(context.channel_auth_context == NULL);
  GPR_ASSERT(context.reserved == NULL);
  *s = PLUGIN_GET_METADATA_CALLED_STATE;
  for (i = 0; i < GPR_ARRAY_SIZE(plugin_md); i++) {
    memset(&md[i], 0, sizeof(grpc_metadata));
    md[i].key = grpc_slice_from_copied_string(plugin_md[i].key);
    md[i].value = grpc_slice_from_copied_string(plugin_md[i].value);
  }
  cb(user_data, md, GPR_ARRAY_SIZE(md), GRPC_STATUS_OK, NULL);
}

static const char *plugin_error_details = "Could not get metadata for plugin.";

static void plugin_get_metadata_failure(void *state,
                                        grpc_auth_metadata_context context,
                                        grpc_credentials_plugin_metadata_cb cb,
                                        void *user_data) {
  plugin_state *s = (plugin_state *)state;
  GPR_ASSERT(strcmp(context.service_url, test_service_url) == 0);
  GPR_ASSERT(strcmp(context.method_name, test_method) == 0);
  GPR_ASSERT(context.channel_auth_context == NULL);
  GPR_ASSERT(context.reserved == NULL);
  *s = PLUGIN_GET_METADATA_CALLED_STATE;
  cb(user_data, NULL, 0, GRPC_STATUS_UNAUTHENTICATED, plugin_error_details);
}

static void on_plugin_metadata_received_success(
    grpc_exec_ctx *exec_ctx, void *user_data, grpc_credentials_md *md_elems,
    size_t num_md, grpc_credentials_status status, const char *error_details) {
  size_t i = 0;
  GPR_ASSERT(user_data == NULL);
  GPR_ASSERT(md_elems != NULL);
  GPR_ASSERT(num_md == GPR_ARRAY_SIZE(plugin_md));
  for (i = 0; i < num_md; i++) {
    GPR_ASSERT(grpc_slice_str_cmp(md_elems[i].key, plugin_md[i].key) == 0);
    GPR_ASSERT(grpc_slice_str_cmp(md_elems[i].value, plugin_md[i].value) == 0);
  }
}

static void on_plugin_metadata_received_failure(
    grpc_exec_ctx *exec_ctx, void *user_data, grpc_credentials_md *md_elems,
    size_t num_md, grpc_credentials_status status, const char *error_details) {
  GPR_ASSERT(user_data == NULL);
  GPR_ASSERT(md_elems == NULL);
  GPR_ASSERT(num_md == 0);
  GPR_ASSERT(status == GRPC_CREDENTIALS_ERROR);
  GPR_ASSERT(error_details != NULL);
  GPR_ASSERT(strcmp(error_details, plugin_error_details) == 0);
}

static void plugin_destroy(void *state) {
  plugin_state *s = (plugin_state *)state;
  *s = PLUGIN_DESTROY_CALLED_STATE;
}

static void test_metadata_plugin_success(void) {
  grpc_call_credentials *creds;
  plugin_state state = PLUGIN_INITIAL_STATE;
  grpc_metadata_credentials_plugin plugin;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method, NULL,
                                            NULL};

  plugin.state = &state;
  plugin.get_metadata = plugin_get_metadata_success;
  plugin.destroy = plugin_destroy;

  creds = grpc_metadata_credentials_create_from_plugin(plugin, NULL);
  GPR_ASSERT(state == PLUGIN_INITIAL_STATE);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, creds, NULL, auth_md_ctx, on_plugin_metadata_received_success,
      NULL);
  GPR_ASSERT(state == PLUGIN_GET_METADATA_CALLED_STATE);
  grpc_call_credentials_release(creds);
  GPR_ASSERT(state == PLUGIN_DESTROY_CALLED_STATE);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_metadata_plugin_failure(void) {
  grpc_call_credentials *creds;
  plugin_state state = PLUGIN_INITIAL_STATE;
  grpc_metadata_credentials_plugin plugin;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method, NULL,
                                            NULL};

  plugin.state = &state;
  plugin.get_metadata = plugin_get_metadata_failure;
  plugin.destroy = plugin_destroy;

  creds = grpc_metadata_credentials_create_from_plugin(plugin, NULL);
  GPR_ASSERT(state == PLUGIN_INITIAL_STATE);
  grpc_call_credentials_get_request_metadata(
      &exec_ctx, creds, NULL, auth_md_ctx, on_plugin_metadata_received_failure,
      NULL);
  GPR_ASSERT(state == PLUGIN_GET_METADATA_CALLED_STATE);
  grpc_call_credentials_release(creds);
  GPR_ASSERT(state == PLUGIN_DESTROY_CALLED_STATE);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_get_well_known_google_credentials_file_path(void) {
  char *path;
  char *home = gpr_getenv("HOME");
  path = grpc_get_well_known_google_credentials_file_path();
  GPR_ASSERT(path != NULL);
  gpr_free(path);
#if defined(GPR_POSIX_ENV) || defined(GPR_LINUX_ENV)
  unsetenv("HOME");
  path = grpc_get_well_known_google_credentials_file_path();
  GPR_ASSERT(path == NULL);
  gpr_setenv("HOME", home);
  gpr_free(path);
#endif /* GPR_POSIX_ENV || GPR_LINUX_ENV */
  gpr_free(home);
}

static void test_channel_creds_duplicate_without_call_creds(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  grpc_channel_credentials *channel_creds =
      grpc_fake_transport_security_credentials_create();

  grpc_channel_credentials *dup =
      grpc_channel_credentials_duplicate_without_call_credentials(
          channel_creds);
  GPR_ASSERT(dup == channel_creds);
  grpc_channel_credentials_unref(&exec_ctx, dup);

  grpc_call_credentials *call_creds =
      grpc_access_token_credentials_create("blah", NULL);
  grpc_channel_credentials *composite_creds =
      grpc_composite_channel_credentials_create(channel_creds, call_creds,
                                                NULL);
  grpc_call_credentials_unref(&exec_ctx, call_creds);
  dup = grpc_channel_credentials_duplicate_without_call_credentials(
      composite_creds);
  GPR_ASSERT(dup == channel_creds);
  grpc_channel_credentials_unref(&exec_ctx, dup);

  grpc_channel_credentials_unref(&exec_ctx, channel_creds);
  grpc_channel_credentials_unref(&exec_ctx, composite_creds);

  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_empty_md_store();
  test_ref_unref_empty_md_store();
  test_add_to_empty_md_store();
  test_add_cstrings_to_empty_md_store();
  test_empty_preallocated_md_store();
  test_add_abunch_to_md_store();
  test_oauth2_token_fetcher_creds_parsing_ok();
  test_oauth2_token_fetcher_creds_parsing_bad_http_status();
  test_oauth2_token_fetcher_creds_parsing_empty_http_body();
  test_oauth2_token_fetcher_creds_parsing_invalid_json();
  test_oauth2_token_fetcher_creds_parsing_missing_token();
  test_oauth2_token_fetcher_creds_parsing_missing_token_type();
  test_oauth2_token_fetcher_creds_parsing_missing_token_lifetime();
  test_google_iam_creds();
  test_access_token_creds();
  test_channel_oauth2_composite_creds();
  test_oauth2_google_iam_composite_creds();
  test_channel_oauth2_google_iam_composite_creds();
  test_compute_engine_creds_success();
  test_compute_engine_creds_failure();
  test_refresh_token_creds_success();
  test_refresh_token_creds_failure();
  test_jwt_creds_success();
  test_jwt_creds_signing_failure();
  test_google_default_creds_auth_key();
  test_google_default_creds_refresh_token();
  test_google_default_creds_gce();
  test_no_google_default_creds();
  test_metadata_plugin_success();
  test_metadata_plugin_failure();
  test_get_well_known_google_credentials_file_path();
  test_channel_creds_duplicate_without_call_creds();
  grpc_shutdown();
  return 0;
}
