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

#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/credentials/composite/composite_credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/credentials/google_default/google_default_credentials.h"
#include "src/core/lib/security/credentials/jwt/jwt_credentials.h"
#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/uri/uri_parser.h"
#include "test/core/util/test_config.h"

using grpc_core::internal::grpc_flush_cached_google_default_credentials;
using grpc_core::internal::set_gce_tenancy_checker_for_testing;

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

static const char valid_sts_json_response[] =
    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
    " \"expires_in\":3599, "
    " \"issued_token_type\":\"urn:ietf:params:oauth:token-type:access_token\", "
    " \"token_type\":\"Bearer\"}";

static const char test_scope[] = "perm1 perm2";

static const char test_signed_jwt[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImY0OTRkN2M1YWU2MGRmOTcyNmM4YW"
    "U0MDcyZTViYTdmZDkwODg2YzcifQ";
static const char test_signed_jwt_token_type[] =
    "urn:ietf:params:oauth:token-type:id_token";
static const char test_signed_jwt2[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImY0OTRkN2M1YWU2MGRmOTcyNmM5YW"
    "U2MDcyZTViYTdnZDkwODg5YzcifQ";
static const char test_signed_jwt_token_type2[] =
    "urn:ietf:params:oauth:token-type:jwt";
static const char test_signed_jwt_path_prefix[] = "test_sign_jwt";

static const char test_service_url[] = "https://foo.com/foo.v1";
static const char other_test_service_url[] = "https://bar.com/bar.v1";

static const char test_sts_endpoint_url[] =
    "https://foo.com:5555/v1/token-exchange";

static const char test_method[] = "ThisIsNotAMethod";

/*  -- Global state flags. -- */

static bool g_test_is_on_gce = false;

static bool g_test_gce_tenancy_checker_called = false;

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
  response = {};
  response.status = status;
  response.body = gpr_strdup(const_cast<char*>(body));
  response.body_length = strlen(body);
  return response;
}

/* -- Tests. -- */

static void test_empty_md_array(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_credentials_mdelem_array md_array;
  md_array = {};
  GPR_ASSERT(md_array.md == nullptr);
  GPR_ASSERT(md_array.size == 0);
  grpc_credentials_mdelem_array_destroy(&md_array);
}

static void test_add_to_empty_md_array(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_credentials_mdelem_array md_array;
  md_array = {};
  const char* key = "hello";
  const char* value = "there blah blah blah blah blah blah blah";
  grpc_mdelem md = grpc_mdelem_from_slices(
      grpc_slice_from_copied_string(key), grpc_slice_from_copied_string(value));
  grpc_credentials_mdelem_array_add(&md_array, md);
  GPR_ASSERT(md_array.size == 1);
  GPR_ASSERT(grpc_mdelem_eq(md, md_array.md[0]));
  GRPC_MDELEM_UNREF(md);
  grpc_credentials_mdelem_array_destroy(&md_array);
}

static void test_add_abunch_to_md_array(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_credentials_mdelem_array md_array;
  md_array = {};
  const char* key = "hello";
  const char* value = "there blah blah blah blah blah blah blah";
  grpc_mdelem md = grpc_mdelem_from_slices(
      grpc_slice_from_copied_string(key), grpc_slice_from_copied_string(value));
  size_t num_entries = 1000;
  for (size_t i = 0; i < num_entries; ++i) {
    grpc_credentials_mdelem_array_add(&md_array, md);
  }
  for (size_t i = 0; i < num_entries; ++i) {
    GPR_ASSERT(grpc_mdelem_eq(md_array.md[i], md));
  }
  GRPC_MDELEM_UNREF(md);
  grpc_credentials_mdelem_array_destroy(&md_array);
}

static void test_oauth2_token_fetcher_creds_parsing_ok(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
  grpc_httpcli_response response =
      http_response(200, valid_oauth2_json_response);
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(token_lifetime == 3599 * GPR_MS_PER_SEC);
  GPR_ASSERT(grpc_slice_str_cmp(GRPC_MDKEY(token_md), "authorization") == 0);
  GPR_ASSERT(grpc_slice_str_cmp(GRPC_MDVALUE(token_md),
                                "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_") ==
             0);
  GRPC_MDELEM_UNREF(token_md);
  grpc_http_response_destroy(&response);
}

static void test_oauth2_token_fetcher_creds_parsing_bad_http_status(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
  grpc_httpcli_response response =
      http_response(401, valid_oauth2_json_response);
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

static void test_oauth2_token_fetcher_creds_parsing_empty_http_body(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
  grpc_httpcli_response response = http_response(200, "");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

static void test_oauth2_token_fetcher_creds_parsing_invalid_json(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
  grpc_httpcli_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    " \"token_type\":\"Bearer\"");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

static void test_oauth2_token_fetcher_creds_parsing_missing_token(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
  grpc_httpcli_response response = http_response(200,
                                                 "{"
                                                 " \"expires_in\":3599, "
                                                 " \"token_type\":\"Bearer\"}");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

static void test_oauth2_token_fetcher_creds_parsing_missing_token_type(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
  grpc_httpcli_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    "}");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

static void test_oauth2_token_fetcher_creds_parsing_missing_token_lifetime(
    void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_mdelem token_md = GRPC_MDNULL;
  grpc_millis token_lifetime;
  grpc_httpcli_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"token_type\":\"Bearer\"}");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
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

static void check_request_metadata(void* arg, grpc_error* error) {
  request_metadata_state* state = static_cast<request_metadata_state*>(arg);
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
  grpc_credentials_mdelem_array_destroy(&state->md_array);
  grpc_pollset_set_destroy(grpc_polling_entity_pollset_set(&state->pollent));
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

static void run_request_metadata_test(grpc_call_credentials* creds,
                                      grpc_auth_metadata_context auth_md_ctx,
                                      request_metadata_state* state) {
  grpc_error* error = GRPC_ERROR_NONE;
  if (creds->get_request_metadata(&state->pollent, auth_md_ctx,
                                  &state->md_array, &state->on_request_metadata,
                                  &error)) {
    // Synchronous result.  Invoke the callback directly.
    check_request_metadata(state, error);
    GRPC_ERROR_UNREF(error);
  }
}

static void test_google_iam_creds(void) {
  grpc_core::ExecCtx exec_ctx;
  expected_md emd[] = {{GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
                        test_google_iam_authorization_token},
                       {GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
                        test_google_iam_authority_selector}};
  request_metadata_state* state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_call_credentials* creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      nullptr);
  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  run_request_metadata_test(creds, auth_md_ctx, state);
  creds->Unref();
}

static void test_access_token_creds(void) {
  grpc_core::ExecCtx exec_ctx;
  expected_md emd[] = {{GRPC_AUTHORIZATION_METADATA_KEY, "Bearer blah"}};
  request_metadata_state* state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_call_credentials* creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  GPR_ASSERT(strcmp(creds->type(), GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) == 0);
  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  run_request_metadata_test(creds, auth_md_ctx, state);
  creds->Unref();
}

namespace {
class check_channel_oauth2 final : public grpc_channel_credentials {
 public:
  check_channel_oauth2() : grpc_channel_credentials("mock") {}
  ~check_channel_oauth2() override = default;

  grpc_core::RefCountedPtr<grpc_channel_security_connector>
  create_security_connector(
      grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
      const char* /*target*/, const grpc_channel_args* /*args*/,
      grpc_channel_args** /*new_args*/) override {
    GPR_ASSERT(strcmp(type(), "mock") == 0);
    GPR_ASSERT(call_creds != nullptr);
    GPR_ASSERT(strcmp(call_creds->type(), GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) ==
               0);
    return nullptr;
  }
};
}  // namespace

static void test_channel_oauth2_composite_creds(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_channel_args* new_args;
  grpc_channel_credentials* channel_creds = new check_channel_oauth2();
  grpc_call_credentials* oauth2_creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_channel_credentials* channel_oauth2_creds =
      grpc_composite_channel_credentials_create(channel_creds, oauth2_creds,
                                                nullptr);
  grpc_channel_credentials_release(channel_creds);
  grpc_call_credentials_release(oauth2_creds);
  channel_oauth2_creds->create_security_connector(nullptr, nullptr, nullptr,
                                                  &new_args);
  grpc_channel_credentials_release(channel_oauth2_creds);
}

static void test_oauth2_google_iam_composite_creds(void) {
  grpc_core::ExecCtx exec_ctx;
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
      "authorization", test_oauth2_bearer_token, 0);

  /* Check security level of fake credentials. */
  GPR_ASSERT(oauth2_creds->min_security_level() == GRPC_SECURITY_NONE);

  grpc_call_credentials* google_iam_creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      nullptr);
  grpc_call_credentials* composite_creds =
      grpc_composite_call_credentials_create(oauth2_creds, google_iam_creds,
                                             nullptr);
  /* Check security level of composite credentials. */
  GPR_ASSERT(composite_creds->min_security_level() ==
             GRPC_PRIVACY_AND_INTEGRITY);

  oauth2_creds->Unref();
  google_iam_creds->Unref();
  GPR_ASSERT(strcmp(composite_creds->type(),
                    GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0);
  const grpc_composite_call_credentials::CallCredentialsList& creds_list =
      static_cast<const grpc_composite_call_credentials*>(composite_creds)
          ->inner();
  GPR_ASSERT(creds_list.size() == 2);
  GPR_ASSERT(strcmp(creds_list[0]->type(), GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) ==
             0);
  GPR_ASSERT(strcmp(creds_list[1]->type(), GRPC_CALL_CREDENTIALS_TYPE_IAM) ==
             0);
  run_request_metadata_test(composite_creds, auth_md_ctx, state);
  composite_creds->Unref();
}

namespace {
class check_channel_oauth2_google_iam final : public grpc_channel_credentials {
 public:
  check_channel_oauth2_google_iam() : grpc_channel_credentials("mock") {}
  ~check_channel_oauth2_google_iam() override = default;

  grpc_core::RefCountedPtr<grpc_channel_security_connector>
  create_security_connector(
      grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
      const char* /*target*/, const grpc_channel_args* /*args*/,
      grpc_channel_args** /*new_args*/) override {
    GPR_ASSERT(strcmp(type(), "mock") == 0);
    GPR_ASSERT(call_creds != nullptr);
    GPR_ASSERT(
        strcmp(call_creds->type(), GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0);
    const grpc_composite_call_credentials::CallCredentialsList& creds_list =
        static_cast<const grpc_composite_call_credentials*>(call_creds.get())
            ->inner();
    GPR_ASSERT(
        strcmp(creds_list[0]->type(), GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) == 0);
    GPR_ASSERT(strcmp(creds_list[1]->type(), GRPC_CALL_CREDENTIALS_TYPE_IAM) ==
               0);
    return nullptr;
  }
};
}  // namespace

static void test_channel_oauth2_google_iam_composite_creds(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_channel_args* new_args;
  grpc_channel_credentials* channel_creds =
      new check_channel_oauth2_google_iam();
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

  channel_oauth2_iam_creds->create_security_connector(nullptr, nullptr, nullptr,
                                                      &new_args);

  grpc_channel_credentials_release(channel_oauth2_iam_creds);
}

static void validate_compute_engine_http_request(
    const grpc_httpcli_request* request) {
  GPR_ASSERT(request->handshaker != &grpc_httpcli_ssl);
  GPR_ASSERT(strcmp(request->host, "metadata.google.internal.") == 0);
  GPR_ASSERT(
      strcmp(request->http.path,
             "/computeMetadata/v1/instance/service-accounts/default/token") ==
      0);
  GPR_ASSERT(request->http.hdr_count == 1);
  GPR_ASSERT(strcmp(request->http.hdrs[0].key, "Metadata-Flavor") == 0);
  GPR_ASSERT(strcmp(request->http.hdrs[0].value, "Google") == 0);
}

static int compute_engine_httpcli_get_success_override(
    const grpc_httpcli_request* request, grpc_millis /*deadline*/,
    grpc_closure* on_done, grpc_httpcli_response* response) {
  validate_compute_engine_http_request(request);
  *response = http_response(200, valid_oauth2_json_response);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int compute_engine_httpcli_get_failure_override(
    const grpc_httpcli_request* request, grpc_millis /*deadline*/,
    grpc_closure* on_done, grpc_httpcli_response* response) {
  validate_compute_engine_http_request(request);
  *response = http_response(403, "Not Authorized.");
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int httpcli_post_should_not_be_called(
    const grpc_httpcli_request* /*request*/, const char* /*body_bytes*/,
    size_t /*body_size*/, grpc_millis /*deadline*/, grpc_closure* /*on_done*/,
    grpc_httpcli_response* /*response*/) {
  GPR_ASSERT("HTTP POST should not be called" == nullptr);
  return 1;
}

static int httpcli_get_should_not_be_called(
    const grpc_httpcli_request* /*request*/, grpc_millis /*deadline*/,
    grpc_closure* /*on_done*/, grpc_httpcli_response* /*response*/) {
  GPR_ASSERT("HTTP GET should not be called" == nullptr);
  return 1;
}

static void test_compute_engine_creds_success() {
  grpc_core::ExecCtx exec_ctx;
  expected_md emd[] = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  const char expected_creds_debug_string[] =
      "GoogleComputeEngineTokenFetcherCredentials{"
      "OAuth2TokenFetcherCredentials}";
  grpc_call_credentials* creds =
      grpc_google_compute_engine_credentials_create(nullptr);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);

  /* First request: http get should be called. */
  request_metadata_state* state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_httpcli_set_override(compute_engine_httpcli_get_success_override,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(creds, auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();

  /* Second request: the cached token should be served directly. */
  state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(creds, auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();

  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);
  creds->Unref();
  grpc_httpcli_set_override(nullptr, nullptr);
}

static void test_compute_engine_creds_failure(void) {
  grpc_core::ExecCtx exec_ctx;
  const char expected_creds_debug_string[] =
      "GoogleComputeEngineTokenFetcherCredentials{"
      "OAuth2TokenFetcherCredentials}";
  request_metadata_state* state = make_request_metadata_state(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token."),
      nullptr, 0);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_call_credentials* creds =
      grpc_google_compute_engine_credentials_create(nullptr);
  grpc_httpcli_set_override(compute_engine_httpcli_get_failure_override,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(creds, auth_md_ctx, state);
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);
  creds->Unref();
  grpc_httpcli_set_override(nullptr, nullptr);
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
    const grpc_httpcli_request* request, const char* body, size_t body_size,
    grpc_millis /*deadline*/, grpc_closure* on_done,
    grpc_httpcli_response* response) {
  validate_refresh_token_http_request(request, body, body_size);
  *response = http_response(200, valid_oauth2_json_response);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int token_httpcli_post_failure(const grpc_httpcli_request* /*request*/,
                                      const char* /*body*/,
                                      size_t /*body_size*/,
                                      grpc_millis /*deadline*/,
                                      grpc_closure* on_done,
                                      grpc_httpcli_response* response) {
  *response = http_response(403, "Not Authorized.");
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static void test_refresh_token_creds_success(void) {
  grpc_core::ExecCtx exec_ctx;
  expected_md emd[] = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  const char expected_creds_debug_string[] =
      "GoogleRefreshToken{ClientID:32555999999.apps.googleusercontent.com,"
      "OAuth2TokenFetcherCredentials}";
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_call_credentials* creds = grpc_google_refresh_token_credentials_create(
      test_refresh_token_str, nullptr);

  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);

  /* First request: http put should be called. */
  request_metadata_state* state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            refresh_token_httpcli_post_success);
  run_request_metadata_test(creds, auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();

  /* Second request: the cached token should be served directly. */
  state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(creds, auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);

  creds->Unref();
  grpc_httpcli_set_override(nullptr, nullptr);
}

static void test_refresh_token_creds_failure(void) {
  grpc_core::ExecCtx exec_ctx;
  const char expected_creds_debug_string[] =
      "GoogleRefreshToken{ClientID:32555999999.apps.googleusercontent.com,"
      "OAuth2TokenFetcherCredentials}";
  request_metadata_state* state = make_request_metadata_state(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token."),
      nullptr, 0);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_call_credentials* creds = grpc_google_refresh_token_credentials_create(
      test_refresh_token_str, nullptr);
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            token_httpcli_post_failure);
  run_request_metadata_test(creds, auth_md_ctx, state);
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);

  creds->Unref();
  grpc_httpcli_set_override(nullptr, nullptr);
}

static void test_valid_sts_creds_options(void) {
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,        // sts_endpoint_url
      nullptr,                      // resource
      nullptr,                      // audience
      nullptr,                      // scope
      nullptr,                      // requested_token_type
      test_signed_jwt_path_prefix,  // subject_token_path
      test_signed_jwt_token_type,   // subject_token_type
      nullptr,                      // actor_token_path
      nullptr                       // actor_token_type
  };
  grpc_uri* sts_url;
  grpc_error* error =
      grpc_core::ValidateStsCredentialsOptions(&valid_options, &sts_url);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(sts_url != nullptr);
  absl::string_view host;
  absl::string_view port;
  GPR_ASSERT(grpc_core::SplitHostPort(sts_url->authority, &host, &port));
  GPR_ASSERT(host == "foo.com");
  GPR_ASSERT(port == "5555");
  grpc_uri_destroy(sts_url);
}

static void test_invalid_sts_creds_options(void) {
  grpc_sts_credentials_options invalid_options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      nullptr,                     // resource
      nullptr,                     // audience
      nullptr,                     // scope
      nullptr,                     // requested_token_type
      nullptr,                     // subject_token_path (Required)
      test_signed_jwt_token_type,  // subject_token_type
      nullptr,                     // actor_token_path
      nullptr                      // actor_token_type
  };
  grpc_uri* url_should_be_null;
  grpc_error* error = grpc_core::ValidateStsCredentialsOptions(
      &invalid_options, &url_should_be_null);
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  GRPC_ERROR_UNREF(error);
  GPR_ASSERT(url_should_be_null == nullptr);

  invalid_options = {
      test_sts_endpoint_url,        // sts_endpoint_url
      nullptr,                      // resource
      nullptr,                      // audience
      nullptr,                      // scope
      nullptr,                      // requested_token_type
      test_signed_jwt_path_prefix,  // subject_token_path
      nullptr,                      // subject_token_type (Required)
      nullptr,                      // actor_token_path
      nullptr                       // actor_token_type
  };
  error = grpc_core::ValidateStsCredentialsOptions(&invalid_options,
                                                   &url_should_be_null);
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  GRPC_ERROR_UNREF(error);
  GPR_ASSERT(url_should_be_null == nullptr);

  invalid_options = {
      nullptr,                      // sts_endpoint_url (Required)
      nullptr,                      // resource
      nullptr,                      // audience
      nullptr,                      // scope
      nullptr,                      // requested_token_type
      test_signed_jwt_path_prefix,  // subject_token_path
      test_signed_jwt_token_type,   // subject_token_type (Required)
      nullptr,                      // actor_token_path
      nullptr                       // actor_token_type
  };
  error = grpc_core::ValidateStsCredentialsOptions(&invalid_options,
                                                   &url_should_be_null);
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  GRPC_ERROR_UNREF(error);
  GPR_ASSERT(url_should_be_null == nullptr);

  invalid_options = {
      "not_a_valid_uri",            // sts_endpoint_url
      nullptr,                      // resource
      nullptr,                      // audience
      nullptr,                      // scope
      nullptr,                      // requested_token_type
      test_signed_jwt_path_prefix,  // subject_token_path
      test_signed_jwt_token_type,   // subject_token_type (Required)
      nullptr,                      // actor_token_path
      nullptr                       // actor_token_type
  };
  error = grpc_core::ValidateStsCredentialsOptions(&invalid_options,
                                                   &url_should_be_null);
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  GRPC_ERROR_UNREF(error);
  GPR_ASSERT(url_should_be_null == nullptr);

  invalid_options = {
      "ftp://ftp.is.not.a.valid.scheme/bar",  // sts_endpoint_url
      nullptr,                                // resource
      nullptr,                                // audience
      nullptr,                                // scope
      nullptr,                                // requested_token_type
      test_signed_jwt_path_prefix,            // subject_token_path
      test_signed_jwt_token_type,             // subject_token_type (Required)
      nullptr,                                // actor_token_path
      nullptr                                 // actor_token_type
  };
  error = grpc_core::ValidateStsCredentialsOptions(&invalid_options,
                                                   &url_should_be_null);
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  GRPC_ERROR_UNREF(error);
  GPR_ASSERT(url_should_be_null == nullptr);
}

static void validate_sts_token_http_request(const grpc_httpcli_request* request,
                                            const char* body, size_t body_size,
                                            bool expect_actor_token) {
  // Check that the body is constructed properly.
  GPR_ASSERT(body != nullptr);
  GPR_ASSERT(body_size != 0);
  GPR_ASSERT(request->handshaker == &grpc_httpcli_ssl);
  char* get_url_equivalent;
  gpr_asprintf(&get_url_equivalent, "%s?%s", test_sts_endpoint_url, body);
  grpc_uri* url = grpc_uri_parse(get_url_equivalent, false);
  GPR_ASSERT(strcmp(grpc_uri_get_query_arg(url, "resource"), "resource") == 0);
  GPR_ASSERT(strcmp(grpc_uri_get_query_arg(url, "audience"), "audience") == 0);
  GPR_ASSERT(strcmp(grpc_uri_get_query_arg(url, "scope"), "scope") == 0);
  GPR_ASSERT(strcmp(grpc_uri_get_query_arg(url, "requested_token_type"),
                    "requested_token_type") == 0);
  GPR_ASSERT(strcmp(grpc_uri_get_query_arg(url, "subject_token"),
                    test_signed_jwt) == 0);
  GPR_ASSERT(strcmp(grpc_uri_get_query_arg(url, "subject_token_type"),
                    test_signed_jwt_token_type) == 0);
  if (expect_actor_token) {
    GPR_ASSERT(strcmp(grpc_uri_get_query_arg(url, "actor_token"),
                      test_signed_jwt2) == 0);
    GPR_ASSERT(strcmp(grpc_uri_get_query_arg(url, "actor_token_type"),
                      test_signed_jwt_token_type2) == 0);
  } else {
    GPR_ASSERT(grpc_uri_get_query_arg(url, "actor_token") == nullptr);
    GPR_ASSERT(grpc_uri_get_query_arg(url, "actor_token_type") == nullptr);
  }
  grpc_uri_destroy(url);
  gpr_free(get_url_equivalent);

  // Check the rest of the request.
  GPR_ASSERT(strcmp(request->host, "foo.com:5555") == 0);
  GPR_ASSERT(strcmp(request->http.path, "/v1/token-exchange") == 0);
  GPR_ASSERT(request->http.hdr_count == 1);
  GPR_ASSERT(strcmp(request->http.hdrs[0].key, "Content-Type") == 0);
  GPR_ASSERT(strcmp(request->http.hdrs[0].value,
                    "application/x-www-form-urlencoded") == 0);
}

static int sts_token_httpcli_post_success(const grpc_httpcli_request* request,
                                          const char* body, size_t body_size,
                                          grpc_millis /*deadline*/,
                                          grpc_closure* on_done,
                                          grpc_httpcli_response* response) {
  validate_sts_token_http_request(request, body, body_size, true);
  *response = http_response(200, valid_sts_json_response);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int sts_token_httpcli_post_success_no_actor_token(
    const grpc_httpcli_request* request, const char* body, size_t body_size,
    grpc_millis /*deadline*/, grpc_closure* on_done,
    grpc_httpcli_response* response) {
  validate_sts_token_http_request(request, body, body_size, false);
  *response = http_response(200, valid_sts_json_response);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static char* write_tmp_jwt_file(const char* jwt_contents) {
  char* path;
  FILE* tmp = gpr_tmpfile(test_signed_jwt_path_prefix, &path);
  GPR_ASSERT(path != nullptr);
  GPR_ASSERT(tmp != nullptr);
  size_t jwt_length = strlen(jwt_contents);
  GPR_ASSERT(fwrite(jwt_contents, 1, jwt_length, tmp) == jwt_length);
  fclose(tmp);
  return path;
}

static void test_sts_creds_success(void) {
  grpc_core::ExecCtx exec_ctx;
  expected_md emd[] = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  const char expected_creds_debug_string[] =
      "StsTokenFetcherCredentials{Path:/v1/"
      "token-exchange,Authority:foo.com:5555,OAuth2TokenFetcherCredentials}";
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  char* subject_token_path = write_tmp_jwt_file(test_signed_jwt);
  char* actor_token_path = write_tmp_jwt_file(test_signed_jwt2);
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      "resource",                  // resource
      "audience",                  // audience
      "scope",                     // scope
      "requested_token_type",      // requested_token_type
      subject_token_path,          // subject_token_path
      test_signed_jwt_token_type,  // subject_token_type
      actor_token_path,            // actor_token_path
      test_signed_jwt_token_type2  // actor_token_type
  };
  grpc_call_credentials* creds =
      grpc_sts_credentials_create(&valid_options, nullptr);

  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);

  /* First request: http put should be called. */
  request_metadata_state* state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            sts_token_httpcli_post_success);
  run_request_metadata_test(creds, auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();

  /* Second request: the cached token should be served directly. */
  state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(creds, auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);

  creds->Unref();
  grpc_httpcli_set_override(nullptr, nullptr);
  gpr_free(subject_token_path);
  gpr_free(actor_token_path);
}

static void test_sts_creds_token_file_not_found(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,           // sts_endpoint_url
      "resource",                      // resource
      "audience",                      // audience
      "scope",                         // scope
      "requested_token_type",          // requested_token_type
      "/some/completely/random/path",  // subject_token_path
      test_signed_jwt_token_type,      // subject_token_type
      "",                              // actor_token_path
      ""                               // actor_token_type
  };
  grpc_call_credentials* creds =
      grpc_sts_credentials_create(&valid_options, nullptr);

  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);

  request_metadata_state* state = make_request_metadata_state(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token."),
      nullptr, 0);
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(creds, auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();

  /* Cleanup. */
  creds->Unref();
  grpc_httpcli_set_override(nullptr, nullptr);
}

static void test_sts_creds_no_actor_token_success(void) {
  grpc_core::ExecCtx exec_ctx;
  expected_md emd[] = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  const char expected_creds_debug_string[] =
      "StsTokenFetcherCredentials{Path:/v1/"
      "token-exchange,Authority:foo.com:5555,OAuth2TokenFetcherCredentials}";
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  char* subject_token_path = write_tmp_jwt_file(test_signed_jwt);
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      "resource",                  // resource
      "audience",                  // audience
      "scope",                     // scope
      "requested_token_type",      // requested_token_type
      subject_token_path,          // subject_token_path
      test_signed_jwt_token_type,  // subject_token_type
      "",                          // actor_token_path
      ""                           // actor_token_type
  };
  grpc_call_credentials* creds =
      grpc_sts_credentials_create(&valid_options, nullptr);

  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);

  /* First request: http put should be called. */
  request_metadata_state* state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            sts_token_httpcli_post_success_no_actor_token);
  run_request_metadata_test(creds, auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();

  /* Second request: the cached token should be served directly. */
  state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(creds, auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);

  creds->Unref();
  grpc_httpcli_set_override(nullptr, nullptr);
  gpr_free(subject_token_path);
}

static void test_sts_creds_load_token_failure(void) {
  const char expected_creds_debug_string[] =
      "StsTokenFetcherCredentials{Path:/v1/"
      "token-exchange,Authority:foo.com:5555,OAuth2TokenFetcherCredentials}";
  grpc_core::ExecCtx exec_ctx;
  request_metadata_state* state = make_request_metadata_state(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token."),
      nullptr, 0);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  char* test_signed_jwt_path = write_tmp_jwt_file(test_signed_jwt);
  grpc_sts_credentials_options options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      "resource",                  // resource
      "audience",                  // audience
      "scope",                     // scope
      "requested_token_type",      // requested_token_type
      "invalid_path",              // subject_token_path
      test_signed_jwt_token_type,  // subject_token_type
      nullptr,                     // actor_token_path
      nullptr                      // actor_token_type
  };
  grpc_call_credentials* creds = grpc_sts_credentials_create(&options, nullptr);
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(creds, auth_md_ctx, state);
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);

  creds->Unref();
  grpc_httpcli_set_override(nullptr, nullptr);
  gpr_free(test_signed_jwt_path);
}

static void test_sts_creds_http_failure(void) {
  const char expected_creds_debug_string[] =
      "StsTokenFetcherCredentials{Path:/v1/"
      "token-exchange,Authority:foo.com:5555,OAuth2TokenFetcherCredentials}";
  grpc_core::ExecCtx exec_ctx;
  request_metadata_state* state = make_request_metadata_state(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token."),
      nullptr, 0);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  char* test_signed_jwt_path = write_tmp_jwt_file(test_signed_jwt);
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      "resource",                  // resource
      "audience",                  // audience
      "scope",                     // scope
      "requested_token_type",      // requested_token_type
      test_signed_jwt_path,        // subject_token_path
      test_signed_jwt_token_type,  // subject_token_type
      nullptr,                     // actor_token_path
      nullptr                      // actor_token_type
  };
  grpc_call_credentials* creds =
      grpc_sts_credentials_create(&valid_options, nullptr);
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            token_httpcli_post_failure);
  run_request_metadata_test(creds, auth_md_ctx, state);
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);
  creds->Unref();
  grpc_httpcli_set_override(nullptr, nullptr);
  gpr_free(test_signed_jwt_path);
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
  GPR_ASSERT(gpr_time_cmp(token_lifetime, grpc_max_auth_token_lifetime()) == 0);
}

static char* encode_and_sign_jwt_success(const grpc_auth_json_key* json_key,
                                         const char* /*audience*/,
                                         gpr_timespec token_lifetime,
                                         const char* scope) {
  validate_jwt_encode_and_sign_params(json_key, scope, token_lifetime);
  return gpr_strdup(test_signed_jwt);
}

static char* encode_and_sign_jwt_failure(const grpc_auth_json_key* json_key,
                                         const char* /*audience*/,
                                         gpr_timespec token_lifetime,
                                         const char* scope) {
  validate_jwt_encode_and_sign_params(json_key, scope, token_lifetime);
  return nullptr;
}

static char* encode_and_sign_jwt_should_not_be_called(
    const grpc_auth_json_key* /*json_key*/, const char* /*audience*/,
    gpr_timespec /*token_lifetime*/, const char* /*scope*/) {
  GPR_ASSERT("grpc_jwt_encode_and_sign should not be called" == nullptr);
  return nullptr;
}

static grpc_service_account_jwt_access_credentials* creds_as_jwt(
    grpc_call_credentials* creds) {
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(strcmp(creds->type(), GRPC_CALL_CREDENTIALS_TYPE_JWT) == 0);
  return reinterpret_cast<grpc_service_account_jwt_access_credentials*>(creds);
}

static void test_jwt_creds_lifetime(void) {
  char* json_key_string = test_json_key_str();
  const char expected_creds_debug_string_prefix[] =
      "JWTAccessCredentials{ExpirationTime:";
  // Max lifetime.
  grpc_call_credentials* jwt_creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime(), nullptr);
  GPR_ASSERT(gpr_time_cmp(creds_as_jwt(jwt_creds)->jwt_lifetime(),
                          grpc_max_auth_token_lifetime()) == 0);
  /* Check security level. */
  GPR_ASSERT(jwt_creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  GPR_ASSERT(strncmp(expected_creds_debug_string_prefix,
                     jwt_creds->debug_string().c_str(),
                     strlen(expected_creds_debug_string_prefix)) == 0);
  grpc_call_credentials_release(jwt_creds);

  // Shorter lifetime.
  gpr_timespec token_lifetime = {10, 0, GPR_TIMESPAN};
  GPR_ASSERT(gpr_time_cmp(grpc_max_auth_token_lifetime(), token_lifetime) > 0);
  jwt_creds = grpc_service_account_jwt_access_credentials_create(
      json_key_string, token_lifetime, nullptr);
  GPR_ASSERT(gpr_time_cmp(creds_as_jwt(jwt_creds)->jwt_lifetime(),
                          token_lifetime) == 0);
  GPR_ASSERT(strncmp(expected_creds_debug_string_prefix,
                     jwt_creds->debug_string().c_str(),
                     strlen(expected_creds_debug_string_prefix)) == 0);
  grpc_call_credentials_release(jwt_creds);

  // Cropped lifetime.
  gpr_timespec add_to_max = {10, 0, GPR_TIMESPAN};
  token_lifetime = gpr_time_add(grpc_max_auth_token_lifetime(), add_to_max);
  jwt_creds = grpc_service_account_jwt_access_credentials_create(
      json_key_string, token_lifetime, nullptr);
  GPR_ASSERT(gpr_time_cmp(creds_as_jwt(jwt_creds)->jwt_lifetime(),
                          grpc_max_auth_token_lifetime()) == 0);
  GPR_ASSERT(strncmp(expected_creds_debug_string_prefix,
                     jwt_creds->debug_string().c_str(),
                     strlen(expected_creds_debug_string_prefix)) == 0);
  grpc_call_credentials_release(jwt_creds);

  gpr_free(json_key_string);
}

static void test_jwt_creds_success(void) {
  const char expected_creds_debug_string_prefix[] =
      "JWTAccessCredentials{ExpirationTime:";

  char* json_key_string = test_json_key_str();
  grpc_core::ExecCtx exec_ctx;
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
  run_request_metadata_test(creds, auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();

  /* Second request: the cached token should be served directly. */
  state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  grpc_jwt_encode_and_sign_set_override(
      encode_and_sign_jwt_should_not_be_called);
  run_request_metadata_test(creds, auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();

  /* Third request: Different service url so jwt_encode_and_sign should be
     called again (no caching). */
  state =
      make_request_metadata_state(GRPC_ERROR_NONE, emd, GPR_ARRAY_SIZE(emd));
  auth_md_ctx.service_url = other_test_service_url;
  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_success);
  run_request_metadata_test(creds, auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(strncmp(expected_creds_debug_string_prefix,
                     creds->debug_string().c_str(),
                     strlen(expected_creds_debug_string_prefix)) == 0);

  creds->Unref();
  gpr_free(json_key_string);
  gpr_free(expected_md_value);
  grpc_jwt_encode_and_sign_set_override(nullptr);
}

static void test_jwt_creds_signing_failure(void) {
  const char expected_creds_debug_string_prefix[] =
      "JWTAccessCredentials{ExpirationTime:";
  char* json_key_string = test_json_key_str();
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  request_metadata_state* state = make_request_metadata_state(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Could not generate JWT."), nullptr,
      0);
  grpc_call_credentials* creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime(), nullptr);

  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_failure);
  run_request_metadata_test(creds, auth_md_ctx, state);

  gpr_free(json_key_string);
  GPR_ASSERT(strncmp(expected_creds_debug_string_prefix,
                     creds->debug_string().c_str(),
                     strlen(expected_creds_debug_string_prefix)) == 0);

  creds->Unref();
  grpc_jwt_encode_and_sign_set_override(nullptr);
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
  grpc_core::ExecCtx exec_ctx;
  grpc_composite_channel_credentials* creds;
  char* json_key = test_json_key_str();
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "json_key_google_default_creds", json_key);
  gpr_free(json_key);
  creds = reinterpret_cast<grpc_composite_channel_credentials*>(
      grpc_google_default_credentials_create());
  auto* default_creds =
      reinterpret_cast<const grpc_google_default_channel_credentials*>(
          creds->inner_creds());
  GPR_ASSERT(default_creds->ssl_creds() != nullptr);
  auto* jwt =
      reinterpret_cast<const grpc_service_account_jwt_access_credentials*>(
          creds->call_creds());
  GPR_ASSERT(
      strcmp(jwt->key().client_id,
             "777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent.com") ==
      0);
  creds->Unref();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
}

static void test_google_default_creds_refresh_token(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_composite_channel_credentials* creds;
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "refresh_token_google_default_creds", test_refresh_token_str);
  creds = reinterpret_cast<grpc_composite_channel_credentials*>(
      grpc_google_default_credentials_create());
  auto* default_creds =
      reinterpret_cast<const grpc_google_default_channel_credentials*>(
          creds->inner_creds());
  GPR_ASSERT(default_creds->ssl_creds() != nullptr);
  auto* refresh =
      reinterpret_cast<const grpc_google_refresh_token_credentials*>(
          creds->call_creds());
  GPR_ASSERT(strcmp(refresh->refresh_token().client_id,
                    "32555999999.apps.googleusercontent.com") == 0);
  creds->Unref();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
}

static int default_creds_metadata_server_detection_httpcli_get_success_override(
    const grpc_httpcli_request* request, grpc_millis /*deadline*/,
    grpc_closure* on_done, grpc_httpcli_response* response) {
  *response = http_response(200, "");
  grpc_http_header* headers =
      static_cast<grpc_http_header*>(gpr_malloc(sizeof(*headers) * 1));
  headers[0].key = gpr_strdup("Metadata-Flavor");
  headers[0].value = gpr_strdup("Google");
  response->hdr_count = 1;
  response->hdrs = headers;
  GPR_ASSERT(strcmp(request->http.path, "/") == 0);
  GPR_ASSERT(strcmp(request->host, "metadata.google.internal.") == 0);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static char* null_well_known_creds_path_getter(void) { return nullptr; }

static bool test_gce_tenancy_checker(void) {
  g_test_gce_tenancy_checker_called = true;
  return g_test_is_on_gce;
}

static void test_google_default_creds_gce(void) {
  grpc_core::ExecCtx exec_ctx;
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
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = true;

  /* Simulate a successful detection of GCE. */
  grpc_composite_channel_credentials* creds =
      reinterpret_cast<grpc_composite_channel_credentials*>(
          grpc_google_default_credentials_create());

  /* Verify that the default creds actually embeds a GCE creds. */
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(creds->call_creds() != nullptr);
  grpc_httpcli_set_override(compute_engine_httpcli_get_success_override,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(creds->mutable_call_creds(), auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();

  GPR_ASSERT(g_test_gce_tenancy_checker_called == true);

  /* Cleanup. */
  creds->Unref();
  grpc_httpcli_set_override(nullptr, nullptr);
  grpc_override_well_known_credentials_path_getter(nullptr);
}

static void test_google_default_creds_non_gce(void) {
  grpc_core::ExecCtx exec_ctx;
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
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = false;
  /* Simulate a successful detection of metadata server. */
  grpc_httpcli_set_override(
      default_creds_metadata_server_detection_httpcli_get_success_override,
      httpcli_post_should_not_be_called);
  grpc_composite_channel_credentials* creds =
      reinterpret_cast<grpc_composite_channel_credentials*>(
          grpc_google_default_credentials_create());
  /* Verify that the default creds actually embeds a GCE creds. */
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(creds->call_creds() != nullptr);
  grpc_httpcli_set_override(compute_engine_httpcli_get_success_override,
                            httpcli_post_should_not_be_called);
  run_request_metadata_test(creds->mutable_call_creds(), auth_md_ctx, state);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(g_test_gce_tenancy_checker_called == true);
  /* Cleanup. */
  creds->Unref();
  grpc_httpcli_set_override(nullptr, nullptr);
  grpc_override_well_known_credentials_path_getter(nullptr);
}

static int default_creds_gce_detection_httpcli_get_failure_override(
    const grpc_httpcli_request* request, grpc_millis /*deadline*/,
    grpc_closure* on_done, grpc_httpcli_response* response) {
  /* No magic header. */
  GPR_ASSERT(strcmp(request->http.path, "/") == 0);
  GPR_ASSERT(strcmp(request->host, "metadata.google.internal.") == 0);
  *response = http_response(200, "");
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static void test_no_google_default_creds(void) {
  grpc_flush_cached_google_default_credentials();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = false;
  grpc_httpcli_set_override(
      default_creds_gce_detection_httpcli_get_failure_override,
      httpcli_post_should_not_be_called);
  /* Simulate a successful detection of GCE. */
  GPR_ASSERT(grpc_google_default_credentials_create() == nullptr);
  /* Try a second one. GCE detection should occur again. */
  g_test_gce_tenancy_checker_called = false;
  GPR_ASSERT(grpc_google_default_credentials_create() == nullptr);
  GPR_ASSERT(g_test_gce_tenancy_checker_called == true);
  /* Cleanup. */
  grpc_override_well_known_credentials_path_getter(nullptr);
  grpc_httpcli_set_override(nullptr, nullptr);
}

typedef enum {
  PLUGIN_INITIAL_STATE,
  PLUGIN_GET_METADATA_CALLED_STATE,
  PLUGIN_DESTROY_CALLED_STATE
} plugin_state;

static const expected_md plugin_md[] = {{"foo", "bar"}, {"hi", "there"}};

static int plugin_get_metadata_success(
    void* state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb /*cb*/, void* /*user_data*/,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t* num_creds_md, grpc_status_code* /*status*/,
    const char** /*error_details*/) {
  GPR_ASSERT(strcmp(context.service_url, test_service_url) == 0);
  GPR_ASSERT(strcmp(context.method_name, test_method) == 0);
  GPR_ASSERT(context.channel_auth_context == nullptr);
  GPR_ASSERT(context.reserved == nullptr);
  GPR_ASSERT(GPR_ARRAY_SIZE(plugin_md) <
             GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX);
  plugin_state* s = static_cast<plugin_state*>(state);
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
    grpc_credentials_plugin_metadata_cb /*cb*/, void* /*user_data*/,
    grpc_metadata /*creds_md*/[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t* /*num_creds_md*/, grpc_status_code* status,
    const char** error_details) {
  GPR_ASSERT(strcmp(context.service_url, test_service_url) == 0);
  GPR_ASSERT(strcmp(context.method_name, test_method) == 0);
  GPR_ASSERT(context.channel_auth_context == nullptr);
  GPR_ASSERT(context.reserved == nullptr);
  plugin_state* s = static_cast<plugin_state*>(state);
  *s = PLUGIN_GET_METADATA_CALLED_STATE;
  *status = GRPC_STATUS_UNAUTHENTICATED;
  *error_details = gpr_strdup(plugin_error_details);
  return true;  // Synchronous return.
}

static void plugin_destroy(void* state) {
  plugin_state* s = static_cast<plugin_state*>(state);
  *s = PLUGIN_DESTROY_CALLED_STATE;
}

static char* plugin_debug_string(void* state) {
  plugin_state* s = static_cast<plugin_state*>(state);
  char* ret = nullptr;
  switch (*s) {
    case PLUGIN_INITIAL_STATE:
      gpr_asprintf(&ret, "TestPluginCredentials{state:INITIAL}");
      break;
    case PLUGIN_GET_METADATA_CALLED_STATE:
      gpr_asprintf(&ret, "TestPluginCredentials{state:GET_METADATA_CALLED}");
      break;
    case PLUGIN_DESTROY_CALLED_STATE:
      gpr_asprintf(&ret, "TestPluginCredentials{state:DESTROY}");
      break;
    default:
      gpr_asprintf(&ret, "TestPluginCredentials{state:UNKNOWN}");
      break;
  }
  return ret;
}

static void test_metadata_plugin_success(void) {
  const char expected_creds_debug_string[] =
      "TestPluginCredentials{state:GET_METADATA_CALLED}";
  plugin_state state = PLUGIN_INITIAL_STATE;
  grpc_metadata_credentials_plugin plugin;
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  request_metadata_state* md_state = make_request_metadata_state(
      GRPC_ERROR_NONE, plugin_md, GPR_ARRAY_SIZE(plugin_md));

  plugin.state = &state;
  plugin.get_metadata = plugin_get_metadata_success;
  plugin.destroy = plugin_destroy;
  plugin.debug_string = plugin_debug_string;

  grpc_call_credentials* creds = grpc_metadata_credentials_create_from_plugin(
      plugin, GRPC_PRIVACY_AND_INTEGRITY, nullptr);
  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  GPR_ASSERT(state == PLUGIN_INITIAL_STATE);
  run_request_metadata_test(creds, auth_md_ctx, md_state);
  GPR_ASSERT(state == PLUGIN_GET_METADATA_CALLED_STATE);
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);
  creds->Unref();

  GPR_ASSERT(state == PLUGIN_DESTROY_CALLED_STATE);
}

static void test_metadata_plugin_failure(void) {
  const char expected_creds_debug_string[] =
      "TestPluginCredentials{state:GET_METADATA_CALLED}";

  plugin_state state = PLUGIN_INITIAL_STATE;
  grpc_metadata_credentials_plugin plugin;
  grpc_core::ExecCtx exec_ctx;
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
  plugin.debug_string = plugin_debug_string;

  grpc_call_credentials* creds = grpc_metadata_credentials_create_from_plugin(
      plugin, GRPC_PRIVACY_AND_INTEGRITY, nullptr);
  GPR_ASSERT(state == PLUGIN_INITIAL_STATE);
  run_request_metadata_test(creds, auth_md_ctx, md_state);
  GPR_ASSERT(state == PLUGIN_GET_METADATA_CALLED_STATE);
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);
  creds->Unref();

  GPR_ASSERT(state == PLUGIN_DESTROY_CALLED_STATE);
}

static void test_get_well_known_google_credentials_file_path(void) {
  char* path;
  char* home = gpr_getenv("HOME");
  bool restore_home_env = false;
#if defined(GRPC_BAZEL_BUILD) && \
    (defined(GPR_POSIX_ENV) || defined(GPR_LINUX_ENV))
  // when running under bazel locally, the HOME variable is not set
  // so we set it to some fake value
  restore_home_env = true;
  gpr_setenv("HOME", "/fake/home/for/bazel");
#endif /* defined(GRPC_BAZEL_BUILD) && (defined(GPR_POSIX_ENV) || \
          defined(GPR_LINUX_ENV)) */
  path = grpc_get_well_known_google_credentials_file_path();
  GPR_ASSERT(path != nullptr);
  gpr_free(path);
#if defined(GPR_POSIX_ENV) || defined(GPR_LINUX_ENV)
  restore_home_env = true;
  gpr_unsetenv("HOME");
  path = grpc_get_well_known_google_credentials_file_path();
  GPR_ASSERT(path == nullptr);
  gpr_free(path);
#endif /* GPR_POSIX_ENV || GPR_LINUX_ENV */
  if (restore_home_env) {
    if (home) {
      gpr_setenv("HOME", home);
    } else {
      gpr_unsetenv("HOME");
    }
  }
  gpr_free(home);
}

static void test_channel_creds_duplicate_without_call_creds(void) {
  const char expected_creds_debug_string[] =
      "AccessTokenCredentials{Token:present}";
  grpc_core::ExecCtx exec_ctx;

  grpc_channel_credentials* channel_creds =
      grpc_fake_transport_security_credentials_create();

  grpc_core::RefCountedPtr<grpc_channel_credentials> dup =
      channel_creds->duplicate_without_call_credentials();
  GPR_ASSERT(dup == channel_creds);
  dup.reset();

  grpc_call_credentials* call_creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_channel_credentials* composite_creds =
      grpc_composite_channel_credentials_create(channel_creds, call_creds,
                                                nullptr);
  GPR_ASSERT(strcmp(call_creds->debug_string().c_str(),
                    expected_creds_debug_string) == 0);

  call_creds->Unref();
  dup = composite_creds->duplicate_without_call_credentials();
  GPR_ASSERT(dup == channel_creds);
  dup.reset();

  channel_creds->Unref();
  composite_creds->Unref();
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
  grpc::testing::TestEnvironment env(argc, argv);
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
  test_valid_sts_creds_options();
  test_invalid_sts_creds_options();
  test_sts_creds_success();
  test_sts_creds_no_actor_token_success();
  test_sts_creds_load_token_failure();
  test_sts_creds_http_failure();
  test_sts_creds_token_file_not_found();
  test_jwt_creds_lifetime();
  test_jwt_creds_success();
  test_jwt_creds_signing_failure();
  test_google_default_creds_auth_key();
  test_google_default_creds_refresh_token();
  test_google_default_creds_gce();
  test_google_default_creds_non_gce();
  test_no_google_default_creds();
  test_metadata_plugin_success();
  test_metadata_plugin_failure();
  test_get_well_known_google_credentials_file_path();
  test_channel_creds_duplicate_without_call_creds();
  test_auth_metadata_context();
  grpc_shutdown();
  return 0;
}
