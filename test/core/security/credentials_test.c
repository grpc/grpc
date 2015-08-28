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

#include "src/core/security/credentials.h"

#include <string.h>

#include "src/core/httpcli/httpcli.h"
#include "src/core/security/json_token.h"
#include "src/core/support/env.h"
#include "src/core/support/file.h"
#include "src/core/support/string.h"

#include "test/core/util/test_config.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include <openssl/rsa.h>

static const char test_google_iam_authorization_token[] = "blahblahblhahb";
static const char test_google_iam_authority_selector[] = "respectmyauthoritah";
static const char test_oauth2_bearer_token[] =
    "Bearer blaaslkdjfaslkdfasdsfasf";
static const char test_root_cert[] = "I am the root!";

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
  response.body = (char *)body;
  response.body_length = strlen(body);
  return response;
}

static void test_empty_md_store(void) {
  grpc_credentials_md_store *store = grpc_credentials_md_store_create(0);
  GPR_ASSERT(store->num_entries == 0);
  GPR_ASSERT(store->allocated == 0);
  grpc_credentials_md_store_unref(store);
}

static void test_ref_unref_empty_md_store(void) {
  grpc_credentials_md_store *store = grpc_credentials_md_store_create(0);
  grpc_credentials_md_store_ref(store);
  grpc_credentials_md_store_ref(store);
  GPR_ASSERT(store->num_entries == 0);
  GPR_ASSERT(store->allocated == 0);
  grpc_credentials_md_store_unref(store);
  grpc_credentials_md_store_unref(store);
  grpc_credentials_md_store_unref(store);
}

static void test_add_to_empty_md_store(void) {
  grpc_credentials_md_store *store = grpc_credentials_md_store_create(0);
  const char *key_str = "hello";
  const char *value_str = "there blah blah blah blah blah blah blah";
  gpr_slice key = gpr_slice_from_copied_string(key_str);
  gpr_slice value = gpr_slice_from_copied_string(value_str);
  grpc_credentials_md_store_add(store, key, value);
  GPR_ASSERT(store->num_entries == 1);
  GPR_ASSERT(gpr_slice_cmp(key, store->entries[0].key) == 0);
  GPR_ASSERT(gpr_slice_cmp(value, store->entries[0].value) == 0);
  gpr_slice_unref(key);
  gpr_slice_unref(value);
  grpc_credentials_md_store_unref(store);
}

static void test_add_cstrings_to_empty_md_store(void) {
  grpc_credentials_md_store *store = grpc_credentials_md_store_create(0);
  const char *key_str = "hello";
  const char *value_str = "there blah blah blah blah blah blah blah";
  grpc_credentials_md_store_add_cstrings(store, key_str, value_str);
  GPR_ASSERT(store->num_entries == 1);
  GPR_ASSERT(gpr_slice_str_cmp(store->entries[0].key, key_str) == 0);
  GPR_ASSERT(gpr_slice_str_cmp(store->entries[0].value, value_str) == 0);
  grpc_credentials_md_store_unref(store);
}

static void test_empty_preallocated_md_store(void) {
  grpc_credentials_md_store *store = grpc_credentials_md_store_create(4);
  GPR_ASSERT(store->num_entries == 0);
  GPR_ASSERT(store->allocated == 4);
  GPR_ASSERT(store->entries != NULL);
  grpc_credentials_md_store_unref(store);
}

static void test_add_abunch_to_md_store(void) {
  grpc_credentials_md_store *store = grpc_credentials_md_store_create(4);
  size_t num_entries = 1000;
  const char *key_str = "hello";
  const char *value_str = "there blah blah blah blah blah blah blah";
  size_t i;
  for (i = 0; i < num_entries; i++) {
    grpc_credentials_md_store_add_cstrings(store, key_str, value_str);
  }
  for (i = 0; i < num_entries; i++) {
    GPR_ASSERT(gpr_slice_str_cmp(store->entries[i].key, key_str) == 0);
    GPR_ASSERT(gpr_slice_str_cmp(store->entries[i].value, value_str) == 0);
  }
  grpc_credentials_md_store_unref(store);
}

static void test_oauth2_token_fetcher_creds_parsing_ok(void) {
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(200, valid_oauth2_json_response);
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(token_lifetime.tv_sec == 3599);
  GPR_ASSERT(token_lifetime.tv_nsec == 0);
  GPR_ASSERT(token_md->num_entries == 1);
  GPR_ASSERT(gpr_slice_str_cmp(token_md->entries[0].key, "Authorization") == 0);
  GPR_ASSERT(gpr_slice_str_cmp(token_md->entries[0].value,
                               "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_") ==
             0);
  grpc_credentials_md_store_unref(token_md);
}

static void test_oauth2_token_fetcher_creds_parsing_bad_http_status(void) {
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(401, valid_oauth2_json_response);
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
}

static void test_oauth2_token_fetcher_creds_parsing_empty_http_body(void) {
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response = http_response(200, "");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
}

static void test_oauth2_token_fetcher_creds_parsing_invalid_json(void) {
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    " \"token_type\":\"Bearer\"");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
}

static void test_oauth2_token_fetcher_creds_parsing_missing_token(void) {
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response = http_response(200,
                                                 "{"
                                                 " \"expires_in\":3599, "
                                                 " \"token_type\":\"Bearer\"}");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
}

static void test_oauth2_token_fetcher_creds_parsing_missing_token_type(void) {
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    "}");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
}

static void test_oauth2_token_fetcher_creds_parsing_missing_token_lifetime(
    void) {
  grpc_credentials_md_store *token_md = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"token_type\":\"Bearer\"}");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_md, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
}

static void check_metadata(expected_md *expected, grpc_credentials_md *md_elems,
                           size_t num_md) {
  size_t i;
  for (i = 0; i < num_md; i++) {
    size_t j;
    for (j = 0; j < num_md; j++) {
      if (0 == gpr_slice_str_cmp(md_elems[j].key, expected[i].key)) {
        GPR_ASSERT(gpr_slice_str_cmp(md_elems[j].value, expected[i].value) ==
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

static void check_google_iam_metadata(void *user_data,
                                      grpc_credentials_md *md_elems,
                                      size_t num_md,
                                      grpc_credentials_status status) {
  grpc_credentials *c = (grpc_credentials *)user_data;
  expected_md emd[] = {{GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
                        test_google_iam_authorization_token},
                       {GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
                        test_google_iam_authority_selector}};
  GPR_ASSERT(status == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(num_md == 2);
  check_metadata(emd, md_elems, num_md);
  grpc_credentials_unref(c);
}

static void test_google_iam_creds(void) {
  grpc_credentials *creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      NULL);
  GPR_ASSERT(grpc_credentials_has_request_metadata(creds));
  GPR_ASSERT(grpc_credentials_has_request_metadata_only(creds));
  grpc_credentials_get_request_metadata(creds, NULL, test_service_url,
                                        check_google_iam_metadata, creds);
}

static void check_access_token_metadata(void *user_data,
                                        grpc_credentials_md *md_elems,
                                        size_t num_md,
                                        grpc_credentials_status status) {
  grpc_credentials *c = (grpc_credentials *)user_data;
  expected_md emd[] = {{GRPC_AUTHORIZATION_METADATA_KEY, "Bearer blah"}};
  GPR_ASSERT(status == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(num_md == 1);
  check_metadata(emd, md_elems, num_md);
  grpc_credentials_unref(c);
}

static void test_access_token_creds(void) {
  grpc_credentials *creds = grpc_access_token_credentials_create("blah", NULL);
  GPR_ASSERT(grpc_credentials_has_request_metadata(creds));
  GPR_ASSERT(grpc_credentials_has_request_metadata_only(creds));
  GPR_ASSERT(strcmp(creds->type, GRPC_CREDENTIALS_TYPE_OAUTH2) == 0);
  grpc_credentials_get_request_metadata(creds, NULL, test_service_url,
                                        check_access_token_metadata, creds);
}

static void check_ssl_oauth2_composite_metadata(
    void *user_data, grpc_credentials_md *md_elems, size_t num_md,
    grpc_credentials_status status) {
  grpc_credentials *c = (grpc_credentials *)user_data;
  expected_md emd[] = {
      {GRPC_AUTHORIZATION_METADATA_KEY, test_oauth2_bearer_token}};
  GPR_ASSERT(status == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(num_md == 1);
  check_metadata(emd, md_elems, num_md);
  grpc_credentials_unref(c);
}

static void test_ssl_oauth2_composite_creds(void) {
  grpc_credentials *ssl_creds =
      grpc_ssl_credentials_create(test_root_cert, NULL, NULL);
  const grpc_credentials_array *creds_array;
  grpc_credentials *oauth2_creds = grpc_md_only_test_credentials_create(
      "Authorization", test_oauth2_bearer_token, 0);
  grpc_credentials *composite_creds =
      grpc_composite_credentials_create(ssl_creds, oauth2_creds, NULL);
  grpc_credentials_unref(ssl_creds);
  grpc_credentials_unref(oauth2_creds);
  GPR_ASSERT(strcmp(composite_creds->type, GRPC_CREDENTIALS_TYPE_COMPOSITE) ==
             0);
  GPR_ASSERT(grpc_credentials_has_request_metadata(composite_creds));
  GPR_ASSERT(!grpc_credentials_has_request_metadata_only(composite_creds));
  creds_array = grpc_composite_credentials_get_credentials(composite_creds);
  GPR_ASSERT(creds_array->num_creds == 2);
  GPR_ASSERT(strcmp(creds_array->creds_array[0]->type,
                    GRPC_CREDENTIALS_TYPE_SSL) == 0);
  GPR_ASSERT(strcmp(creds_array->creds_array[1]->type,
                    GRPC_CREDENTIALS_TYPE_OAUTH2) == 0);
  grpc_credentials_get_request_metadata(composite_creds, NULL, test_service_url,
                                        check_ssl_oauth2_composite_metadata,
                                        composite_creds);
}

void test_ssl_fake_transport_security_composite_creds_failure(void) {
  grpc_credentials *ssl_creds = grpc_ssl_credentials_create(NULL, NULL, NULL);
  grpc_credentials *fake_transport_security_creds =
      grpc_fake_transport_security_credentials_create();

  /* 2 connector credentials: should not work. */
  GPR_ASSERT(grpc_composite_credentials_create(
                 ssl_creds, fake_transport_security_creds, NULL) == NULL);
  grpc_credentials_unref(ssl_creds);
  grpc_credentials_unref(fake_transport_security_creds);
}

static void check_ssl_oauth2_google_iam_composite_metadata(
    void *user_data, grpc_credentials_md *md_elems, size_t num_md,
    grpc_credentials_status status) {
  grpc_credentials *c = (grpc_credentials *)user_data;
  expected_md emd[] = {
      {GRPC_AUTHORIZATION_METADATA_KEY, test_oauth2_bearer_token},
      {GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
       test_google_iam_authorization_token},
      {GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
       test_google_iam_authority_selector}};
  GPR_ASSERT(status == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(num_md == 3);
  check_metadata(emd, md_elems, num_md);
  grpc_credentials_unref(c);
}

static void test_ssl_oauth2_google_iam_composite_creds(void) {
  grpc_credentials *ssl_creds =
      grpc_ssl_credentials_create(test_root_cert, NULL, NULL);
  const grpc_credentials_array *creds_array;
  grpc_credentials *oauth2_creds = grpc_md_only_test_credentials_create(
      "Authorization", test_oauth2_bearer_token, 0);
  grpc_credentials *aux_creds =
      grpc_composite_credentials_create(ssl_creds, oauth2_creds, NULL);
  grpc_credentials *google_iam_creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      NULL);
  grpc_credentials *composite_creds =
      grpc_composite_credentials_create(aux_creds, google_iam_creds, NULL);
  grpc_credentials_unref(ssl_creds);
  grpc_credentials_unref(oauth2_creds);
  grpc_credentials_unref(aux_creds);
  grpc_credentials_unref(google_iam_creds);
  GPR_ASSERT(strcmp(composite_creds->type, GRPC_CREDENTIALS_TYPE_COMPOSITE) ==
             0);
  GPR_ASSERT(grpc_credentials_has_request_metadata(composite_creds));
  GPR_ASSERT(!grpc_credentials_has_request_metadata_only(composite_creds));
  creds_array = grpc_composite_credentials_get_credentials(composite_creds);
  GPR_ASSERT(creds_array->num_creds == 3);
  GPR_ASSERT(strcmp(creds_array->creds_array[0]->type,
                    GRPC_CREDENTIALS_TYPE_SSL) == 0);
  GPR_ASSERT(strcmp(creds_array->creds_array[1]->type,
                    GRPC_CREDENTIALS_TYPE_OAUTH2) == 0);
  GPR_ASSERT(strcmp(creds_array->creds_array[2]->type,
                    GRPC_CREDENTIALS_TYPE_IAM) == 0);
  grpc_credentials_get_request_metadata(
      composite_creds, NULL, test_service_url,
      check_ssl_oauth2_google_iam_composite_metadata, composite_creds);
}

static void on_oauth2_creds_get_metadata_success(
    void *user_data, grpc_credentials_md *md_elems, size_t num_md,
    grpc_credentials_status status) {
  GPR_ASSERT(status == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(num_md == 1);
  GPR_ASSERT(gpr_slice_str_cmp(md_elems[0].key, "Authorization") == 0);
  GPR_ASSERT(gpr_slice_str_cmp(md_elems[0].value,
                               "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_") ==
             0);
  GPR_ASSERT(user_data != NULL);
  GPR_ASSERT(strcmp((const char *)user_data, test_user_data) == 0);
}

static void on_oauth2_creds_get_metadata_failure(
    void *user_data, grpc_credentials_md *md_elems, size_t num_md,
    grpc_credentials_status status) {
  GPR_ASSERT(status == GRPC_CREDENTIALS_ERROR);
  GPR_ASSERT(num_md == 0);
  GPR_ASSERT(user_data != NULL);
  GPR_ASSERT(strcmp((const char *)user_data, test_user_data) == 0);
}

static void validate_compute_engine_http_request(
    const grpc_httpcli_request *request) {
  GPR_ASSERT(request->handshaker != &grpc_httpcli_ssl);
  GPR_ASSERT(strcmp(request->host, "metadata") == 0);
  GPR_ASSERT(
      strcmp(request->path,
             "/computeMetadata/v1/instance/service-accounts/default/token") ==
      0);
  GPR_ASSERT(request->hdr_count == 1);
  GPR_ASSERT(strcmp(request->hdrs[0].key, "Metadata-Flavor") == 0);
  GPR_ASSERT(strcmp(request->hdrs[0].value, "Google") == 0);
}

static int compute_engine_httpcli_get_success_override(
    const grpc_httpcli_request *request, gpr_timespec deadline,
    grpc_httpcli_response_cb on_response, void *user_data) {
  grpc_httpcli_response response =
      http_response(200, valid_oauth2_json_response);
  validate_compute_engine_http_request(request);
  on_response(user_data, &response);
  return 1;
}

static int compute_engine_httpcli_get_failure_override(
    const grpc_httpcli_request *request, gpr_timespec deadline,
    grpc_httpcli_response_cb on_response, void *user_data) {
  grpc_httpcli_response response = http_response(403, "Not Authorized.");
  validate_compute_engine_http_request(request);
  on_response(user_data, &response);
  return 1;
}

static int httpcli_post_should_not_be_called(
    const grpc_httpcli_request *request, const char *body_bytes,
    size_t body_size, gpr_timespec deadline,
    grpc_httpcli_response_cb on_response, void *user_data) {
  GPR_ASSERT("HTTP POST should not be called" == NULL);
  return 1;
}

static int httpcli_get_should_not_be_called(
    const grpc_httpcli_request *request, gpr_timespec deadline,
    grpc_httpcli_response_cb on_response, void *user_data) {
  GPR_ASSERT("HTTP GET should not be called" == NULL);
  return 1;
}

static void test_compute_engine_creds_success(void) {
  grpc_credentials *compute_engine_creds =
      grpc_google_compute_engine_credentials_create(NULL);
  GPR_ASSERT(grpc_credentials_has_request_metadata(compute_engine_creds));
  GPR_ASSERT(grpc_credentials_has_request_metadata_only(compute_engine_creds));

  /* First request: http get should be called. */
  grpc_httpcli_set_override(compute_engine_httpcli_get_success_override,
                            httpcli_post_should_not_be_called);
  grpc_credentials_get_request_metadata(
      compute_engine_creds, NULL, test_service_url,
      on_oauth2_creds_get_metadata_success, (void *)test_user_data);

  /* Second request: the cached token should be served directly. */
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  grpc_credentials_get_request_metadata(
      compute_engine_creds, NULL, test_service_url,
      on_oauth2_creds_get_metadata_success, (void *)test_user_data);

  grpc_credentials_unref(compute_engine_creds);
  grpc_httpcli_set_override(NULL, NULL);
}

static void test_compute_engine_creds_failure(void) {
  grpc_credentials *compute_engine_creds =
      grpc_google_compute_engine_credentials_create(NULL);
  grpc_httpcli_set_override(compute_engine_httpcli_get_failure_override,
                            httpcli_post_should_not_be_called);
  GPR_ASSERT(grpc_credentials_has_request_metadata(compute_engine_creds));
  GPR_ASSERT(grpc_credentials_has_request_metadata_only(compute_engine_creds));
  grpc_credentials_get_request_metadata(
      compute_engine_creds, NULL, test_service_url,
      on_oauth2_creds_get_metadata_failure, (void *)test_user_data);
  grpc_credentials_unref(compute_engine_creds);
  grpc_httpcli_set_override(NULL, NULL);
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
  GPR_ASSERT(strcmp(request->path, GRPC_GOOGLE_OAUTH2_SERVICE_TOKEN_PATH) == 0);
  GPR_ASSERT(request->hdr_count == 1);
  GPR_ASSERT(strcmp(request->hdrs[0].key, "Content-Type") == 0);
  GPR_ASSERT(
      strcmp(request->hdrs[0].value, "application/x-www-form-urlencoded") == 0);
}

static int refresh_token_httpcli_post_success(
    const grpc_httpcli_request *request, const char *body, size_t body_size,
    gpr_timespec deadline, grpc_httpcli_response_cb on_response,
    void *user_data) {
  grpc_httpcli_response response =
      http_response(200, valid_oauth2_json_response);
  validate_refresh_token_http_request(request, body, body_size);
  on_response(user_data, &response);
  return 1;
}

static int refresh_token_httpcli_post_failure(
    const grpc_httpcli_request *request, const char *body, size_t body_size,
    gpr_timespec deadline, grpc_httpcli_response_cb on_response,
    void *user_data) {
  grpc_httpcli_response response = http_response(403, "Not Authorized.");
  validate_refresh_token_http_request(request, body, body_size);
  on_response(user_data, &response);
  return 1;
}

static void test_refresh_token_creds_success(void) {
  grpc_credentials *refresh_token_creds =
      grpc_google_refresh_token_credentials_create(test_refresh_token_str,
                                                   NULL);
  GPR_ASSERT(grpc_credentials_has_request_metadata(refresh_token_creds));
  GPR_ASSERT(grpc_credentials_has_request_metadata_only(refresh_token_creds));

  /* First request: http get should be called. */
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            refresh_token_httpcli_post_success);
  grpc_credentials_get_request_metadata(
      refresh_token_creds, NULL, test_service_url,
      on_oauth2_creds_get_metadata_success, (void *)test_user_data);

  /* Second request: the cached token should be served directly. */
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            httpcli_post_should_not_be_called);
  grpc_credentials_get_request_metadata(
      refresh_token_creds, NULL, test_service_url,
      on_oauth2_creds_get_metadata_success, (void *)test_user_data);

  grpc_credentials_unref(refresh_token_creds);
  grpc_httpcli_set_override(NULL, NULL);
}

static void test_refresh_token_creds_failure(void) {
  grpc_credentials *refresh_token_creds =
      grpc_google_refresh_token_credentials_create(test_refresh_token_str,
                                                   NULL);
  grpc_httpcli_set_override(httpcli_get_should_not_be_called,
                            refresh_token_httpcli_post_failure);
  GPR_ASSERT(grpc_credentials_has_request_metadata(refresh_token_creds));
  GPR_ASSERT(grpc_credentials_has_request_metadata_only(refresh_token_creds));
  grpc_credentials_get_request_metadata(
      refresh_token_creds, NULL, test_service_url,
      on_oauth2_creds_get_metadata_failure, (void *)test_user_data);
  grpc_credentials_unref(refresh_token_creds);
  grpc_httpcli_set_override(NULL, NULL);
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
  GPR_ASSERT(!gpr_time_cmp(token_lifetime, grpc_max_auth_token_lifetime));
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
}

static void on_jwt_creds_get_metadata_success(void *user_data,
                                              grpc_credentials_md *md_elems,
                                              size_t num_md,
                                              grpc_credentials_status status) {
  char *expected_md_value;
  gpr_asprintf(&expected_md_value, "Bearer %s", test_signed_jwt);
  GPR_ASSERT(status == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(num_md == 1);
  GPR_ASSERT(gpr_slice_str_cmp(md_elems[0].key, "Authorization") == 0);
  GPR_ASSERT(gpr_slice_str_cmp(md_elems[0].value, expected_md_value) == 0);
  GPR_ASSERT(user_data != NULL);
  GPR_ASSERT(strcmp((const char *)user_data, test_user_data) == 0);
  gpr_free(expected_md_value);
}

static void on_jwt_creds_get_metadata_failure(void *user_data,
                                              grpc_credentials_md *md_elems,
                                              size_t num_md,
                                              grpc_credentials_status status) {
  GPR_ASSERT(status == GRPC_CREDENTIALS_ERROR);
  GPR_ASSERT(num_md == 0);
  GPR_ASSERT(user_data != NULL);
  GPR_ASSERT(strcmp((const char *)user_data, test_user_data) == 0);
}

static void test_jwt_creds_success(void) {
  char *json_key_string = test_json_key_str();
  grpc_credentials *jwt_creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime, NULL);
  GPR_ASSERT(grpc_credentials_has_request_metadata(jwt_creds));
  GPR_ASSERT(grpc_credentials_has_request_metadata_only(jwt_creds));

  /* First request: jwt_encode_and_sign should be called. */
  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_success);
  grpc_credentials_get_request_metadata(jwt_creds, NULL, test_service_url,
                                        on_jwt_creds_get_metadata_success,
                                        (void *)test_user_data);

  /* Second request: the cached token should be served directly. */
  grpc_jwt_encode_and_sign_set_override(
      encode_and_sign_jwt_should_not_be_called);
  grpc_credentials_get_request_metadata(jwt_creds, NULL, test_service_url,
                                        on_jwt_creds_get_metadata_success,
                                        (void *)test_user_data);

  /* Third request: Different service url so jwt_encode_and_sign should be
     called again (no caching). */
  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_success);
  grpc_credentials_get_request_metadata(jwt_creds, NULL, other_test_service_url,
                                        on_jwt_creds_get_metadata_success,
                                        (void *)test_user_data);

  gpr_free(json_key_string);
  grpc_credentials_unref(jwt_creds);
  grpc_jwt_encode_and_sign_set_override(NULL);
}

static void test_jwt_creds_signing_failure(void) {
  char *json_key_string = test_json_key_str();
  grpc_credentials *jwt_creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime, NULL);
  GPR_ASSERT(grpc_credentials_has_request_metadata(jwt_creds));
  GPR_ASSERT(grpc_credentials_has_request_metadata_only(jwt_creds));

  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_failure);
  grpc_credentials_get_request_metadata(jwt_creds, NULL, test_service_url,
                                        on_jwt_creds_get_metadata_failure,
                                        (void *)test_user_data);

  gpr_free(json_key_string);
  grpc_credentials_unref(jwt_creds);
  grpc_jwt_encode_and_sign_set_override(NULL);
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

static grpc_credentials *composite_inner_creds(grpc_credentials *creds,
                                               const char *inner_creds_type) {
  size_t i;
  grpc_composite_credentials *composite;
  GPR_ASSERT(strcmp(creds->type, GRPC_CREDENTIALS_TYPE_COMPOSITE) == 0);
  composite = (grpc_composite_credentials *)creds;
  for (i = 0; i < composite->inner.num_creds; i++) {
    grpc_credentials *c = composite->inner.creds_array[i];
    if (strcmp(c->type, inner_creds_type) == 0) return c;
  }
  GPR_ASSERT(0); /* Not found. */
}

static void test_google_default_creds_auth_key(void) {
  grpc_service_account_jwt_access_credentials *jwt;
  grpc_credentials *creds;
  char *json_key = test_json_key_str();
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "json_key_google_default_creds", json_key);
  gpr_free(json_key);
  creds = grpc_google_default_credentials_create();
  GPR_ASSERT(creds != NULL);
  jwt = (grpc_service_account_jwt_access_credentials *)composite_inner_creds(
      creds, GRPC_CREDENTIALS_TYPE_JWT);
  GPR_ASSERT(
      strcmp(jwt->key.client_id,
             "777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent.com") ==
      0);
  grpc_credentials_unref(creds);
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
}

static void test_google_default_creds_access_token(void) {
  grpc_google_refresh_token_credentials *refresh;
  grpc_credentials *creds;
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "refresh_token_google_default_creds", test_refresh_token_str);
  creds = grpc_google_default_credentials_create();
  GPR_ASSERT(creds != NULL);
  refresh = (grpc_google_refresh_token_credentials *)composite_inner_creds(
      creds, GRPC_CREDENTIALS_TYPE_OAUTH2);
  GPR_ASSERT(strcmp(refresh->refresh_token.client_id,
                    "32555999999.apps.googleusercontent.com") == 0);
  grpc_credentials_unref(creds);
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
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
  test_ssl_oauth2_composite_creds();
  test_ssl_oauth2_google_iam_composite_creds();
  test_compute_engine_creds_success();
  test_compute_engine_creds_failure();
  test_refresh_token_creds_success();
  test_refresh_token_creds_failure();
  test_jwt_creds_success();
  test_jwt_creds_signing_failure();
  test_google_default_creds_auth_key();
  test_google_default_creds_access_token();
  return 0;
}
