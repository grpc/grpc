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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/credentials.h"

#include <openssl/rsa.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/slice.h>

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
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/support/tmpfile.h"
#include "test/core/util/test_config.h"

/* -- Mock channel credentials. -- */

static grpc_channel_credentials* grpc_mock_channel_credentials_create(
    const grpc_channel_credentials_vtable* vtable) {
  grpc_channel_credentials* c =
      static_cast<grpc_channel_credentials*>(gpr_malloc(sizeof(*c)));
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

static const char test_scope[] = "perm1 perm2";

static const char test_signed_jwt[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImY0OTRkN2M1YWU2MGRmOTcyNmM4YW"
    "U0MDcyZTViYTdmZDkwODg2YzcifQ";

static const char test_service_url[] = "https://foo.com/foo.v1";
static const char other_test_service_url[] = "https://bar.com/bar.v1";

static const char test_method[] = "ThisIsNotAMethod";

/* -- Utils. -- */

static char* test_json_key_str(void) {
  size_t result_len = strlen(test_json_key_str_part1) +
                      strlen(test_json_key_str_part2) +
                      strlen(test_json_key_str_part3);
  char* result = static_cast<char*>(gpr_malloc(result_len + 1));
  char* current = result;
  strcpy(result, test_json_key_str_part1);
  current += strlen(test_json_key_str_part1);
  strcpy(current, test_json_key_str_part2);
  current += strlen(test_json_key_str_part2);
  strcpy(current, test_json_key_str_part3);
  return result;
}

static grpc_httpcli_response http_response(int status, const char* body) {
  grpc_httpcli_response response;
  memset(&response, 0, sizeof(grpc_httpcli_response));
  response.status = status;
  response.body = gpr_strdup((char*)body);
  response.body_length = strlen(body);
  return response;
}

/* -- Tests. -- */

static void test_empty_md_array(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_mdelem_array md_array;
  memset(&md_array, 0, sizeof(md_array));
  GPR_ASSERT(md_array.md == nullptr);
  GPR_ASSERT(md_array.size == 0);
  grpc_credentials_mdelem_array_destroy(&exec_ctx, &md_array);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_add_to_empty_md_array(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_mdelem_array md_array;
  memset(&md_array, 0, sizeof(md_array));
  const char* key = "hello";
  const char* value = "there blah blah blah blah blah blah blah";
  grpc_mdelem md =
      grpc_mdelem_from_slices(&exec_ctx, grpc_slice_from_copied_string(key),
                              grpc_slice_from_copied_string(value));
  grpc_credentials_mdelem_array_add(&md_array, md);
  GPR_ASSERT(md_array.size == 1);
  GPR_ASSERT(grpc_mdelem_eq(md, md_array.md[0]));
  GRPC_MDELEM_UNREF(&exec_ctx, md);
  grpc_credentials_mdelem_array_destroy(&exec_ctx, &md_array);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_add_abunch_to_md_array(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_credentials_mdelem_array md_array;
  memset(&md_array, 0, sizeof(md_array));
  const char* key = "hello";
  const char* value = "there blah blah blah blah blah blah blah";
  grpc_mdelem md =
      grpc_mdelem_from_slices(&exec_ctx, grpc_slice_from_copied_string(key),
                              grpc_slice_from_copied_string(value));
  size_t num_entries = 1000;
  for (size_t i = 0; i < num_entries; ++i) {
    grpc_credentials_mdelem_array_add(&md_array, md);
  }
  for (size_t i = 0; i < num_entries; ++i) {
    GPR_ASSERT(grpc_mdelem_eq(md_array.md[i], md));
  }
  GRPC_MDELEM_UNREF(&exec_ctx, md);
  grpc_credentials_mdelem_array_destroy(&exec_ctx, &md_array);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_oauth2_token_fetcher_creds_parsing_ok(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
  grpc_httpcli_response response =
      http_response(200, valid_oauth2_json_response);
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &exec_ctx, &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_OK);
  GPR_ASSERT(token_lifetime == 3599 * GPR_MS_PER_SEC);
  GPR_ASSERT(grpc_slice_str_cmp(GRPC_MDKEY(token_md), "authorization") == 0);
  GPR_ASSERT(grpc_slice_str_cmp(GRPC_MDVALUE(token_md),
                                "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_") ==
             0);
  GRPC_MDELEM_UNREF(&exec_ctx, token_md);
  grpc_http_response_destroy(&response);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_oauth2_token_fetcher_creds_parsing_bad_http_status(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
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
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
  grpc_httpcli_response response = http_response(200, "");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &exec_ctx, &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_oauth2_token_fetcher_creds_parsing_invalid_json(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
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
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
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
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
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
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
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

typedef struct {
  const char* key;
  const char* value;
} expected_md;

typedef struct {
  grpc_error* expected_error;
  const expected_md* expected;
  size_t expected_size;
  grpc_credentials_mdelem_array md_array;
  grpc_closure on_request_metadata;
  grpc_call_credentials* creds;
  grpc_polling_entity pollent;
} request_metadata_state;

static void check_metadata(const expected_md* expected,
                           grpc_credentials_mdelem_array* md_array) {
  for (size_t i = 0; i < md_array->size; ++i) {
    size_t j;
    for (j = 0; j < md_array->size; ++j) {
      if (0 ==
          grpc_slice_str_cmp(GRPC_MDKEY(md_array->md[j]), expected[i].key)) {
        GPR_ASSERT(grpc_slice_str_cmp(GRPC_MDVALUE(md_array->md[j]),
                                      expected[i].value) == 0);
        break;
      }
    }
    if (j == md_array->size) {
      gpr_log(GPR_ERROR, "key %s not found", expected[i].key);
      GPR_ASSERT(0);
    }
  }
}

static void check_request_metadata(grpc_exec_ctx* exec_ctx, void* arg,
                                   grpc_error* error) {
  request_metadata_state* state = (request_metadata_state*)arg;
  gpr_log(GPR_INFO, "expected_error: %s",
          grpc_error_string(state->expected_error));
  gpr_log(GPR_INFO, "actual_error: %s", grpc_error_string(error));
  if (state->expected_error == GRPC_ERROR_NONE) {
    GPR_ASSERT(error == GRPC_ERROR_NONE);
  } else {
    grpc_slice expected_error;
    GPR_ASSERT(grpc_error_get_str(state->expected_error,
                                  GRPC_ERROR_STR_DESCRIPTION, &expected_error));
    grpc_slice actual_error;
    GPR_ASSERT(
        grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &actual_error));
    GPR_ASSERT(grpc_slice_cmp(expected_error, actual_error) == 0);
    GRPC_ERROR_UNREF(state->expected_error);
  }
  gpr_log(GPR_INFO, "expected_size=%" PRIdPTR " actual_size=%" PRIdPTR,
          state->expected_size, state->md_array.size);
  GPR_ASSERT(state->md_array.size == state->expected_size);
  check_metadata(state->expected, &state->md_array);
  grpc_credentials_mdelem_array_destroy(exec_ctx, &state->md_array);
  grpc_pollset_set_destroy(exec_ctx,
                           grpc_polling_entity_pollset_set(&state->pollent));
  gpr_free(state);
}

static request_metadata_state* make_request_metadata_state(
    grpc_error* expected_error, const expected_md* expected,
    size_t expected_size) {
  request_metadata_state* state =
      static_cast<request_metadata_state*>(gpr_zalloc(sizeof(*state)));
  state->expected_error = expected_error;
  state->expected = expected;
  state->expected_size = expected_size;
  state->pollent =
      grpc_polling_entity_create_from_pollset_set(grpc_pollset_set_create());
  GRPC_CLOSURE_INIT(&state->on_request_metadata, check_request_metadata, state,
                    grpc_schedule_on_exec_ctx);
  return state;
}

static void run_request_metadata_test(grpc_exec_ctx* exec_ctx,
                                      grpc_call_credentials* creds,
                                      grpc_auth_metadata_context auth_md_ctx,
                                      request_metadata_state* state) {
  grpc_error* error = GRPC_ERROR_NONE;
  if (grpc_call_credentials_get_request_metadata(
          exec_ctx, creds, &state->pollent, auth_md_ctx, &state->md_array,
          &state->on_request_metadata, &error)) {
    // Synchronous result.  Invoke the callback directly.
    check_request_metadata(exec_ctx, state, error);
    GRPC_ERROR_UNREF(error);
  }
}

static void test_google_iam_creds(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  expected_md emd[] = {{GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
                        test_google_iam_authorization_token},
                       {GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
                        test_google_iam_authority_selector}};
  request_metadata_state* state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_call_credentials* creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      nullptr);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, state);
  grpc_call_credentials_unref(&exec_ctx, creds);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_access_token_creds(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  expected_md emd[] = {{GRPC_AUTHORIZATION_METADATA_KEY, "Bearer blah"}};
  request_metadata_state* state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_call_credentials* creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  GPR_ASSERT(strcmp(creds->type, GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) == 0);
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, state);
  grpc_call_credentials_unref(&exec_ctx, creds);
  grpc_exec_ctx_finish(&exec_ctx);
}

static grpc_security_status check_channel_oauth2_create_security_connector(
    grpc_exec_ctx* exec_ctx, grpc_channel_credentials* c,
    grpc_call_credentials* call_creds, const char* target,
    const grpc_channel_args* args, grpc_channel_security_connector** sc,
    grpc_channel_args** new_args) {
  GPR_ASSERT(strcmp(c->type, "mock") == 0);
  GPR_ASSERT(call_creds != nullptr);
  GPR_ASSERT(strcmp(call_creds->type, GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) == 0);
  return GRPC_SECURITY_OK;
}

static void test_channel_oauth2_composite_creds(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_channel_args* new_args;
  grpc_channel_credentials_vtable vtable = {
      nullptr, check_channel_oauth2_create_security_connector, nullptr};
  grpc_channel_credentials* channel_creds =
      grpc_mock_channel_credentials_create(&vtable);
  grpc_call_credentials* oauth2_creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_channel_credentials* channel_oauth2_creds =
      grpc_composite_channel_credentials_create(channel_creds, oauth2_creds,
                                                nullptr);
  grpc_channel_credentials_release(channel_creds);
  grpc_call_credentials_release(oauth2_creds);
  GPR_ASSERT(grpc_channel_credentials_create_security_connector(
                 &exec_ctx, channel_oauth2_creds, nullptr, nullptr, nullptr,
                 &new_args) == GRPC_SECURITY_OK);
  grpc_channel_credentials_release(channel_oauth2_creds);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_oauth2_google_iam_composite_creds(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  expected_md emd[] = {
      {GRPC_AUTHORIZATION_METADATA_KEY, test_oauth2_bearer_token},
      {GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
       test_google_iam_authorization_token},
      {GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
       test_google_iam_authority_selector}};
  request_metadata_state* state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_call_credentials* oauth2_creds = grpc_md_only_test_credentials_create(
      &exec_ctx, "authorization", test_oauth2_bearer_token, 0);
  grpc_call_credentials* google_iam_creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      nullptr);
  grpc_call_credentials* composite_creds =
      grpc_composite_call_credentials_create(oauth2_creds, google_iam_creds,
                                             nullptr);
  grpc_call_credentials_unref(&exec_ctx, oauth2_creds);
  grpc_call_credentials_unref(&exec_ctx, google_iam_creds);
  GPR_ASSERT(
      strcmp(composite_creds->type, GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0);
  const grpc_call_credentials_array* creds_array =
      grpc_composite_call_credentials_get_credentials(composite_creds);
  GPR_ASSERT(creds_array->num_creds == 2);
  GPR_ASSERT(strcmp(creds_array->creds_array[0]->type,
                    GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) == 0);
  GPR_ASSERT(strcmp(creds_array->creds_array[1]->type,
                    GRPC_CALL_CREDENTIALS_TYPE_IAM) == 0);
  run_request_metadata_test(&exec_ctx, composite_creds, auth_md_ctx, state);
  grpc_call_credentials_unref(&exec_ctx, composite_creds);
  grpc_exec_ctx_finish(&exec_ctx);
}

static grpc_security_status
check_channel_oauth2_google_iam_create_security_connector(
    grpc_exec_ctx* exec_ctx, grpc_channel_credentials* c,
    grpc_call_credentials* call_creds, const char* target,
    const grpc_channel_args* args, grpc_channel_security_connector** sc,
    grpc_channel_args** new_args) {
  const grpc_call_credentials_array* creds_array;
  GPR_ASSERT(strcmp(c->type, "mock") == 0);
  GPR_ASSERT(call_creds != nullptr);
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
  grpc_channel_args* new_args;
  grpc_channel_credentials_vtable vtable = {
      nullptr, check_channel_oauth2_google_iam_create_security_connector,
      nullptr};
  grpc_channel_credentials* channel_creds =
      grpc_mock_channel_credentials_create(&vtable);
  grpc_call_credentials* oauth2_creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_channel_credentials* channel_oauth2_creds =
      grpc_composite_channel_credentials_create(channel_creds, oauth2_creds,
                                                nullptr);
  grpc_call_credentials* google_iam_creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      nullptr);
  grpc_channel_credentials* channel_oauth2_iam_creds =
      grpc_composite_channel_credentials_create(channel_oauth2_creds,
                                                google_iam_creds, nullptr);
  grpc_channel_credentials_release(channel_creds);
  grpc_call_credentials_release(oauth2_creds);
  grpc_channel_credentials_release(channel_oauth2_creds);
  grpc_call_credentials_release(google_iam_creds);

  GPR_ASSERT(grpc_channel_credentials_create_security_connector(
                 &exec_ctx, channel_oauth2_iam_creds, nullptr, nullptr, nullptr,
                 &new_args) == GRPC_SECURITY_OK);

  grpc_channel_credentials_release(channel_oauth2_iam_creds);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void validate_compute_engine_http_request(
    const grpc_httpcli_request* request) {
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
    grpc_exec_ctx* exec_ctx, const grpc_httpcli_request* request,
    grpc_millis deadline, grpc_closure* on_done,
    grpc_httpcli_response* response) {
  validate_compute_engine_http_request(request);
  *response = http_response(200, valid_oauth2_json_response);
  GRPC_CLOSURE_SCHED(exec_ctx, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int compute_engine_httpcli_get_failure_override(
    grpc_exec_ctx* exec_ctx, const grpc_httpcli_request* request,
    grpc_millis deadline, grpc_closure* on_done,
    grpc_httpcli_response* response) {
  validate_compute_engine_http_request(request);
  *response = http_response(403, "Not Authorized.");
  GRPC_CLOSURE_SCHED(exec_ctx, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int httpcli_post_should_not_be_called(
    grpc_exec_ctx* exec_ctx, const grpc_httpcli_request* request,
    const char* body_bytes, size_t body_size, grpc_millis deadline,
    grpc_closure* on_done, grpc_httpcli_response* response) {
  GPR_ASSERT("HTTP POST should not be called" == nullptr);
  return 1;
}

static int httpcli_get_should_not_be_called(grpc_exec_ctx* exec_ctx,
                                            const grpc_httpcli_request* request,
                                            grpc_millis deadline,
                                            grpc_closure* on_done,
                                            grpc_httpcli_response* response) {
  GPR_ASSERT("HTTP GET should not be called" == nullptr);
  return 1;
}

static void test_compute_engine_creds_success(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  expected_md emd[] = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  grpc_call_credentials* creds =
      grpc_google_compute_engine_credentials_create(nullptr);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};

  /* First request: http get should be called. */
  request_metadata_state* state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_httpcli_set_override(compute_engine_httpcli_get_success_override,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, state);
  grpc_exec_ctx_flush(&exec_ctx);

  /* Second request: the cached token should be served directly. */
  state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, state);
  grpc_exec_ctx_flush(&exec_ctx);

  grpc_call_credentials_unref(&exec_ctx, creds);
  grpc_httpcli_set_override(nullptr, nullptr);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_compute_engine_creds_failure(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  request_metadata_state* state = make_request_metadata_state(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error occured when fetching oauth2 token."),
      nullptr, 0);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_call_credentials* creds =
      grpc_google_compute_engine_credentials_create(nullptr);
  grpc_httpcli_set_override(compute_engine_httpcli_get_failure_override,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, state);
  grpc_call_credentials_unref(&exec_ctx, creds);
  grpc_httpcli_set_override(nullptr, nullptr);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void validate_refresh_token_http_request(
    const grpc_httpcli_request* request, const char* body, size_t body_size) {
  /* The content of the assertion is tested extensively in json_token_test. */
  char* expected_body = nullptr;
  GPR_ASSERT(body != nullptr);
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
    grpc_exec_ctx* exec_ctx, const grpc_httpcli_request* request,
    const char* body, size_t body_size, grpc_millis deadline,
    grpc_closure* on_done, grpc_httpcli_response* response) {
  validate_refresh_token_http_request(request, body, body_size);
  *response = http_response(200, valid_oauth2_json_response);
  GRPC_CLOSURE_SCHED(exec_ctx, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int refresh_token_httpcli_post_failure(
    grpc_exec_ctx* exec_ctx, const grpc_httpcli_request* request,
    const char* body, size_t body_size, grpc_millis deadline,
    grpc_closure* on_done, grpc_httpcli_response* response) {
  validate_refresh_token_http_request(request, body, body_size);
  *response = http_response(403, "Not Authorized.");
  GRPC_CLOSURE_SCHED(exec_ctx, on_done, GRPC_ERROR_NONE);
  return 1;
}

static void test_refresh_token_creds_success(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  expected_md emd[] = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_call_credentials* creds = grpc_google_refresh_token_credentials_create(
      test_refresh_token_str, nullptr);

  /* First request: http get should be called. */
  request_metadata_state* state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            refresh_token_httpcli_post_success);
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, state);
  grpc_exec_ctx_flush(&exec_ctx);

  /* Second request: the cached token should be served directly. */
  state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, state);
  grpc_exec_ctx_flush(&exec_ctx);

  grpc_call_credentials_unref(&exec_ctx, creds);
  grpc_httpcli_set_override(nullptr, nullptr);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_refresh_token_creds_failure(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  request_metadata_state* state = make_request_metadata_state(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error occured when fetching oauth2 token."),
      nullptr, 0);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_call_credentials* creds = grpc_google_refresh_token_credentials_create(
      test_refresh_token_str, nullptr);
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            refresh_token_httpcli_post_failure);
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, state);
  grpc_call_credentials_unref(&exec_ctx, creds);
  grpc_httpcli_set_override(nullptr, nullptr);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void validate_jwt_encode_and_sign_params(
    const grpc_auth_json_key* json_key, const char* scope,
    gpr_timespec token_lifetime) {
  GPR_ASSERT(grpc_auth_json_key_is_valid(json_key));
  GPR_ASSERT(json_key->private_key != nullptr);
  GPR_ASSERT(RSA_check_key(json_key->private_key));
  GPR_ASSERT(json_key->type != nullptr &&
             strcmp(json_key->type, "service_account") == 0);
  GPR_ASSERT(json_key->private_key_id != nullptr &&
             strcmp(json_key->private_key_id,
                    "e6b5137873db8d2ef81e06a47289e6434ec8a165") == 0);
  GPR_ASSERT(json_key->client_id != nullptr &&
             strcmp(json_key->client_id,
                    "777-abaslkan11hlb6nmim3bpspl31ud.apps."
                    "googleusercontent.com") == 0);
  GPR_ASSERT(json_key->client_email != nullptr &&
             strcmp(json_key->client_email,
                    "777-abaslkan11hlb6nmim3bpspl31ud@developer."
                    "gserviceaccount.com") == 0);
  if (scope != nullptr) GPR_ASSERT(strcmp(scope, test_scope) == 0);
  GPR_ASSERT(!gpr_time_cmp(token_lifetime, grpc_max_auth_token_lifetime()));
}

static char* encode_and_sign_jwt_success(const grpc_auth_json_key* json_key,
                                         const char* audience,
                                         gpr_timespec token_lifetime,
                                         const char* scope) {
  validate_jwt_encode_and_sign_params(json_key, scope, token_lifetime);
  return gpr_strdup(test_signed_jwt);
}

static char* encode_and_sign_jwt_failure(const grpc_auth_json_key* json_key,
                                         const char* audience,
                                         gpr_timespec token_lifetime,
                                         const char* scope) {
  validate_jwt_encode_and_sign_params(json_key, scope, token_lifetime);
  return nullptr;
}

static char* encode_and_sign_jwt_should_not_be_called(
    const grpc_auth_json_key* json_key, const char* audience,
    gpr_timespec token_lifetime, const char* scope) {
  GPR_ASSERT("grpc_jwt_encode_and_sign should not be called" == nullptr);
  return nullptr;
}

static grpc_service_account_jwt_access_credentials* creds_as_jwt(
    grpc_call_credentials* creds) {
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(strcmp(creds->type, GRPC_CALL_CREDENTIALS_TYPE_JWT) == 0);
  return (grpc_service_account_jwt_access_credentials*)creds;
}

static void test_jwt_creds_lifetime(void) {
  char* json_key_string = test_json_key_str();

  // Max lifetime.
  grpc_call_credentials* jwt_creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime(), nullptr);
  GPR_ASSERT(gpr_time_cmp(creds_as_jwt(jwt_creds)->jwt_lifetime,
                          grpc_max_auth_token_lifetime()) == 0);
  grpc_call_credentials_release(jwt_creds);

  // Shorter lifetime.
  gpr_timespec token_lifetime = {10, 0, GPR_TIMESPAN};
  GPR_ASSERT(gpr_time_cmp(grpc_max_auth_token_lifetime(), token_lifetime) > 0);
  jwt_creds = grpc_service_account_jwt_access_credentials_create(
      json_key_string, token_lifetime, nullptr);
  GPR_ASSERT(
      gpr_time_cmp(creds_as_jwt(jwt_creds)->jwt_lifetime, token_lifetime) == 0);
  grpc_call_credentials_release(jwt_creds);

  // Cropped lifetime.
  gpr_timespec add_to_max = {10, 0, GPR_TIMESPAN};
  token_lifetime = gpr_time_add(grpc_max_auth_token_lifetime(), add_to_max);
  jwt_creds = grpc_service_account_jwt_access_credentials_create(
      json_key_string, token_lifetime, nullptr);
  GPR_ASSERT(gpr_time_cmp(creds_as_jwt(jwt_creds)->jwt_lifetime,
                          grpc_max_auth_token_lifetime()) == 0);
  grpc_call_credentials_release(jwt_creds);

  gpr_free(json_key_string);
}

static void test_jwt_creds_success(void) {
  char* json_key_string = test_json_key_str();
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  char* expected_md_value;
  gpr_asprintf(&expected_md_value, "Bearer %s", test_signed_jwt);
  expected_md emd[] = {{"authorization", expected_md_value}};
  grpc_call_credentials* creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime(), nullptr);

  /* First request: jwt_encode_and_sign should be called. */
  request_metadata_state* state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_success);
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, state);
  grpc_exec_ctx_flush(&exec_ctx);

  /* Second request: the cached token should be served directly. */
  state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_jwt_encode_and_sign_set_override(
      encode_and_sign_jwt_should_not_be_called);
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, state);
  grpc_exec_ctx_flush(&exec_ctx);

  /* Third request: Different service url so jwt_encode_and_sign should be
     called again (no caching). */
  state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  auth_md_ctx.service_url = other_test_service_url;
  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_success);
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, state);
  grpc_exec_ctx_flush(&exec_ctx);

  grpc_call_credentials_unref(&exec_ctx, creds);
  gpr_free(json_key_string);
  gpr_free(expected_md_value);
  grpc_jwt_encode_and_sign_set_override(nullptr);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_jwt_creds_signing_failure(void) {
  char* json_key_string = test_json_key_str();
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  request_metadata_state* state = make_request_metadata_state(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Could not generate JWT."), nullptr,
      0);
  grpc_call_credentials* creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime(), nullptr);

  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_failure);
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, state);

  gpr_free(json_key_string);
  grpc_call_credentials_unref(&exec_ctx, creds);
  grpc_jwt_encode_and_sign_set_override(nullptr);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void set_google_default_creds_env_var_with_file_contents(
    const char* file_prefix, const char* contents) {
  size_t contents_len = strlen(contents);
  char* creds_file_name;
  FILE* creds_file = gpr_tmpfile(file_prefix, &creds_file_name);
  GPR_ASSERT(creds_file_name != nullptr);
  GPR_ASSERT(creds_file != nullptr);
  GPR_ASSERT(fwrite(contents, 1, contents_len, creds_file) == contents_len);
  fclose(creds_file);
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, creds_file_name);
  gpr_free(creds_file_name);
}

static void test_google_default_creds_auth_key(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_service_account_jwt_access_credentials* jwt;
  grpc_composite_channel_credentials* creds;
  char* json_key = test_json_key_str();
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "json_key_google_default_creds", json_key);
  gpr_free(json_key);
  creds = (grpc_composite_channel_credentials*)
      grpc_google_default_credentials_create();
  GPR_ASSERT(creds != nullptr);
  jwt = (grpc_service_account_jwt_access_credentials*)creds->call_creds;
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
  grpc_google_refresh_token_credentials* refresh;
  grpc_composite_channel_credentials* creds;
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "refresh_token_google_default_creds", test_refresh_token_str);
  creds = (grpc_composite_channel_credentials*)
      grpc_google_default_credentials_create();
  GPR_ASSERT(creds != nullptr);
  refresh = (grpc_google_refresh_token_credentials*)creds->call_creds;
  GPR_ASSERT(strcmp(refresh->refresh_token.client_id,
                    "32555999999.apps.googleusercontent.com") == 0);
  grpc_channel_credentials_unref(&exec_ctx, &creds->base);
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
  grpc_exec_ctx_finish(&exec_ctx);
}

static int default_creds_gce_detection_httpcli_get_success_override(
    grpc_exec_ctx* exec_ctx, const grpc_httpcli_request* request,
    grpc_millis deadline, grpc_closure* on_done,
    grpc_httpcli_response* response) {
  *response = http_response(200, "");
  grpc_http_header* headers =
      static_cast<grpc_http_header*>(gpr_malloc(sizeof(*headers) * 1));
  headers[0].key = gpr_strdup("Metadata-Flavor");
  headers[0].value = gpr_strdup("Google");
  response->hdr_count = 1;
  response->hdrs = headers;
  GPR_ASSERT(strcmp(request->http.path, "/") == 0);
  GPR_ASSERT(strcmp(request->host, "metadata.google.internal") == 0);
  GRPC_CLOSURE_SCHED(exec_ctx, on_done, GRPC_ERROR_NONE);
  return 1;
}

static char* null_well_known_creds_path_getter(void) { return nullptr; }

static void test_google_default_creds_gce(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  expected_md emd[] = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  request_metadata_state* state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_flush_cached_google_default_credentials();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);

  /* Simulate a successful detection of GCE. */
  grpc_httpcli_set_override(
      default_creds_gce_detection_httpcli_get_success_override,
      httpcli_post_should_not_be_called);
  grpc_composite_channel_credentials* creds =
      (grpc_composite_channel_credentials*)
          grpc_google_default_credentials_create();

  /* Verify that the default creds actually embeds a GCE creds. */
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(creds->call_creds != nullptr);
  grpc_httpcli_set_override(compute_engine_httpcli_get_success_override,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(&exec_ctx, creds->call_creds, auth_md_ctx, state);
  grpc_exec_ctx_flush(&exec_ctx);

  /* Check that we get a cached creds if we call
     grpc_google_default_credentials_create again.
     GCE detection should not occur anymore either. */
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  grpc_channel_credentials* cached_creds =
      grpc_google_default_credentials_create();
  GPR_ASSERT(cached_creds == &creds->base);

  /* Cleanup. */
  grpc_channel_credentials_unref(&exec_ctx, cached_creds);
  grpc_channel_credentials_unref(&exec_ctx, &creds->base);
  grpc_httpcli_set_override(nullptr, nullptr);
  grpc_override_well_known_credentials_path_getter(nullptr);
  grpc_exec_ctx_finish(&exec_ctx);
}

static int default_creds_gce_detection_httpcli_get_failure_override(
    grpc_exec_ctx* exec_ctx, const grpc_httpcli_request* request,
    grpc_millis deadline, grpc_closure* on_done,
    grpc_httpcli_response* response) {
  /* No magic header. */
  GPR_ASSERT(strcmp(request->http.path, "/") == 0);
  GPR_ASSERT(strcmp(request->host, "metadata.google.internal") == 0);
  *response = http_response(200, "");
  GRPC_CLOSURE_SCHED(exec_ctx, on_done, GRPC_ERROR_NONE);
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
  GPR_ASSERT(grpc_google_default_credentials_create() == nullptr);

  /* Try a cached one. GCE detection should not occur anymore. */
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  GPR_ASSERT(grpc_google_default_credentials_create() == nullptr);

  /* Cleanup. */
  grpc_httpcli_set_override(nullptr, nullptr);
  grpc_override_well_known_credentials_path_getter(nullptr);
}

typedef enum {
  PLUGIN_INITIAL_STATE,
  PLUGIN_GET_METADATA_CALLED_STATE,
  PLUGIN_DESTROY_CALLED_STATE
} plugin_state;

static const expected_md plugin_md[] = {{"foo", "bar"}, {"hi", "there"}};

static int plugin_get_metadata_success(
    void* state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void* user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t* num_creds_md, grpc_status_code* status,
    const char** error_details) {
  GPR_ASSERT(strcmp(context.service_url, test_service_url) == 0);
  GPR_ASSERT(strcmp(context.method_name, test_method) == 0);
  GPR_ASSERT(context.channel_auth_context == nullptr);
  GPR_ASSERT(context.reserved == nullptr);
  GPR_ASSERT(GPR_ARRAY_SIZE(plugin_md) <
             GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX);
  plugin_state* s = (plugin_state*)state;
  *s = PLUGIN_GET_METADATA_CALLED_STATE;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(plugin_md); ++i) {
    memset(&creds_md[i], 0, sizeof(grpc_metadata));
    creds_md[i].key = grpc_slice_from_copied_string(plugin_md[i].key);
    creds_md[i].value = grpc_slice_from_copied_string(plugin_md[i].value);
  }
  *num_creds_md = GPR_ARRAY_SIZE(plugin_md);
  return true;  // Synchronous return.
}

static const char* plugin_error_details = "Could not get metadata for plugin.";

static int plugin_get_metadata_failure(
    void* state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void* user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t* num_creds_md, grpc_status_code* status,
    const char** error_details) {
  GPR_ASSERT(strcmp(context.service_url, test_service_url) == 0);
  GPR_ASSERT(strcmp(context.method_name, test_method) == 0);
  GPR_ASSERT(context.channel_auth_context == nullptr);
  GPR_ASSERT(context.reserved == nullptr);
  plugin_state* s = (plugin_state*)state;
  *s = PLUGIN_GET_METADATA_CALLED_STATE;
  *status = GRPC_STATUS_UNAUTHENTICATED;
  *error_details = gpr_strdup(plugin_error_details);
  return true;  // Synchronous return.
}

static void plugin_destroy(void* state) {
  plugin_state* s = (plugin_state*)state;
  *s = PLUGIN_DESTROY_CALLED_STATE;
}

static void test_metadata_plugin_success(void) {
  plugin_state state = PLUGIN_INITIAL_STATE;
  grpc_metadata_credentials_plugin plugin;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  request_metadata_state* md_state = make_request_metadata_state(
      GRPC_ERROR_NONE, plugin_md, GPR_ARRAY_SIZE(plugin_md));

  plugin.state = &state;
  plugin.get_metadata = plugin_get_metadata_success;
  plugin.destroy = plugin_destroy;

  grpc_call_credentials* creds =
      grpc_metadata_credentials_create_from_plugin(plugin, nullptr);
  GPR_ASSERT(state == PLUGIN_INITIAL_STATE);
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, md_state);
  GPR_ASSERT(state == PLUGIN_GET_METADATA_CALLED_STATE);
  grpc_call_credentials_unref(&exec_ctx, creds);
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(state == PLUGIN_DESTROY_CALLED_STATE);
}

static void test_metadata_plugin_failure(void) {
  plugin_state state = PLUGIN_INITIAL_STATE;
  grpc_metadata_credentials_plugin plugin;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  char* expected_error;
  gpr_asprintf(&expected_error,
               "Getting metadata from plugin failed with error: %s",
               plugin_error_details);
  request_metadata_state* md_state = make_request_metadata_state(
      GRPC_ERROR_CREATE_FROM_COPIED_STRING(expected_error), nullptr, 0);
  gpr_free(expected_error);

  plugin.state = &state;
  plugin.get_metadata = plugin_get_metadata_failure;
  plugin.destroy = plugin_destroy;

  grpc_call_credentials* creds =
      grpc_metadata_credentials_create_from_plugin(plugin, nullptr);
  GPR_ASSERT(state == PLUGIN_INITIAL_STATE);
  run_request_metadata_test(&exec_ctx, creds, auth_md_ctx, md_state);
  GPR_ASSERT(state == PLUGIN_GET_METADATA_CALLED_STATE);
  grpc_call_credentials_unref(&exec_ctx, creds);
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(state == PLUGIN_DESTROY_CALLED_STATE);
}

static void test_get_well_known_google_credentials_file_path(void) {
  char* path;
  char* home = gpr_getenv("HOME");
  path = grpc_get_well_known_google_credentials_file_path();
  GPR_ASSERT(path != nullptr);
  gpr_free(path);
#if defined(GPR_POSIX_ENV) || defined(GPR_LINUX_ENV)
  unsetenv("HOME");
  path = grpc_get_well_known_google_credentials_file_path();
  GPR_ASSERT(path == nullptr);
  gpr_setenv("HOME", home);
  gpr_free(path);
#endif /* GPR_POSIX_ENV || GPR_LINUX_ENV */
  gpr_free(home);
}

static void test_channel_creds_duplicate_without_call_creds(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  grpc_channel_credentials* channel_creds =
      grpc_fake_transport_security_credentials_create();

  grpc_channel_credentials* dup =
      grpc_channel_credentials_duplicate_without_call_credentials(
          channel_creds);
  GPR_ASSERT(dup == channel_creds);
  grpc_channel_credentials_unref(&exec_ctx, dup);

  grpc_call_credentials* call_creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_channel_credentials* composite_creds =
      grpc_composite_channel_credentials_create(channel_creds, call_creds,
                                                nullptr);
  grpc_call_credentials_unref(&exec_ctx, call_creds);
  dup = grpc_channel_credentials_duplicate_without_call_credentials(
      composite_creds);
  GPR_ASSERT(dup == channel_creds);
  grpc_channel_credentials_unref(&exec_ctx, dup);

  grpc_channel_credentials_unref(&exec_ctx, channel_creds);
  grpc_channel_credentials_unref(&exec_ctx, composite_creds);

  grpc_exec_ctx_finish(&exec_ctx);
}

typedef struct {
  const char* url_scheme;
  const char* call_host;
  const char* call_method;
  const char* desired_service_url;
  const char* desired_method_name;
} auth_metadata_context_test_case;

static void test_auth_metadata_context(void) {
  auth_metadata_context_test_case test_cases[] = {
      // No service nor method.
      {"https", "www.foo.com", "", "https://www.foo.com", ""},
      // No method.
      {"https", "www.foo.com", "/Service", "https://www.foo.com/Service", ""},
      // Empty service and method.
      {"https", "www.foo.com", "//", "https://www.foo.com/", ""},
      // Empty method.
      {"https", "www.foo.com", "/Service/", "https://www.foo.com/Service", ""},
      // Malformed url.
      {"https", "www.foo.com:", "/Service/", "https://www.foo.com:/Service",
       ""},
      // https, default explicit port.
      {"https", "www.foo.com:443", "/Service/FooMethod",
       "https://www.foo.com/Service", "FooMethod"},
      // https, default implicit port.
      {"https", "www.foo.com", "/Service/FooMethod",
       "https://www.foo.com/Service", "FooMethod"},
      // https with ipv6 literal, default explicit port.
      {"https", "[1080:0:0:0:8:800:200C:417A]:443", "/Service/FooMethod",
       "https://[1080:0:0:0:8:800:200C:417A]/Service", "FooMethod"},
      // https with ipv6 literal, default implicit port.
      {"https", "[1080:0:0:0:8:800:200C:443]", "/Service/FooMethod",
       "https://[1080:0:0:0:8:800:200C:443]/Service", "FooMethod"},
      // https, custom port.
      {"https", "www.foo.com:8888", "/Service/FooMethod",
       "https://www.foo.com:8888/Service", "FooMethod"},
      // https with ipv6 literal, custom port.
      {"https", "[1080:0:0:0:8:800:200C:417A]:8888", "/Service/FooMethod",
       "https://[1080:0:0:0:8:800:200C:417A]:8888/Service", "FooMethod"},
      // custom url scheme, https default port.
      {"blah", "www.foo.com:443", "/Service/FooMethod",
       "blah://www.foo.com:443/Service", "FooMethod"}};
  for (uint32_t i = 0; i < GPR_ARRAY_SIZE(test_cases); i++) {
    const char* url_scheme = test_cases[i].url_scheme;
    grpc_slice call_host =
        grpc_slice_from_copied_string(test_cases[i].call_host);
    grpc_slice call_method =
        grpc_slice_from_copied_string(test_cases[i].call_method);
    grpc_auth_metadata_context auth_md_context;
    memset(&auth_md_context, 0, sizeof(auth_md_context));
    grpc_auth_metadata_context_build(url_scheme, call_host, call_method,
                                     nullptr, &auth_md_context);
    if (strcmp(auth_md_context.service_url,
               test_cases[i].desired_service_url) != 0) {
      gpr_log(GPR_ERROR, "Invalid service url, want: %s, got %s.",
              test_cases[i].desired_service_url, auth_md_context.service_url);
      GPR_ASSERT(false);
    }
    if (strcmp(auth_md_context.method_name,
               test_cases[i].desired_method_name) != 0) {
      gpr_log(GPR_ERROR, "Invalid method name, want: %s, got %s.",
              test_cases[i].desired_method_name, auth_md_context.method_name);
      GPR_ASSERT(false);
    }
    GPR_ASSERT(auth_md_context.channel_auth_context == nullptr);
    grpc_slice_unref(call_host);
    grpc_slice_unref(call_method);
    grpc_auth_metadata_context_reset(&auth_md_context);
  }
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_empty_md_array();
  test_add_to_empty_md_array();
  test_add_abunch_to_md_array();
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
  test_jwt_creds_lifetime();
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
  test_auth_metadata_context();
  grpc_shutdown();
  return 0;
}
