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

#include "src/core/security/credentials.h"

#include "src/core/httpcli/httpcli.h"
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "test/core/util/test_config.h"

#include <string.h>

static const char test_iam_authorization_token[] = "blahblahblhahb";
static const char test_iam_authority_selector[] = "respectmyauthoritah";
static const char test_oauth2_bearer_token[] =
    "Bearer blaaslkdjfaslkdfasdsfasf";
static const unsigned char test_root_cert[] = {0xDE, 0xAD, 0xBE, 0xEF};

typedef struct {
  const char *key;
  const char *value;
} expected_md;

static grpc_httpcli_response http_response(int status, char *body) {
  grpc_httpcli_response response;
  memset(&response, 0, sizeof(grpc_httpcli_response));
  response.status = status;
  response.body = body;
  response.body_length = strlen(body);
  return response;
}

static void test_compute_engine_creds_parsing_ok(void) {
  grpc_mdctx *ctx = grpc_mdctx_create();
  grpc_mdelem *token_elem = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    " \"token_type\":\"Bearer\"}");
  GPR_ASSERT(grpc_compute_engine_credentials_parse_server_response(
                 &response, ctx, &token_elem, &token_lifetime) ==
             GRPC_CREDENTIALS_OK);
  GPR_ASSERT(token_lifetime.tv_sec == 3599);
  GPR_ASSERT(token_lifetime.tv_nsec == 0);
  GPR_ASSERT(!strcmp(grpc_mdstr_as_c_string(token_elem->key), "Authorization"));
  GPR_ASSERT(!strcmp(grpc_mdstr_as_c_string(token_elem->value),
                     "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"));
  grpc_mdelem_unref(token_elem);
  grpc_mdctx_orphan(ctx);
}

static void test_compute_engine_creds_parsing_bad_http_status(void) {
  grpc_mdctx *ctx = grpc_mdctx_create();
  grpc_mdelem *token_elem = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(401,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    " \"token_type\":\"Bearer\"}");
  GPR_ASSERT(grpc_compute_engine_credentials_parse_server_response(
                 &response, ctx, &token_elem, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_mdctx_orphan(ctx);
}

static void test_compute_engine_creds_parsing_empty_http_body(void) {
  grpc_mdctx *ctx = grpc_mdctx_create();
  grpc_mdelem *token_elem = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response = http_response(200, "");
  GPR_ASSERT(grpc_compute_engine_credentials_parse_server_response(
                 &response, ctx, &token_elem, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_mdctx_orphan(ctx);
}

static void test_compute_engine_creds_parsing_invalid_json(void) {
  grpc_mdctx *ctx = grpc_mdctx_create();
  grpc_mdelem *token_elem = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    " \"token_type\":\"Bearer\"");
  GPR_ASSERT(grpc_compute_engine_credentials_parse_server_response(
                 &response, ctx, &token_elem, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_mdctx_orphan(ctx);
}

static void test_compute_engine_creds_parsing_missing_token(void) {
  grpc_mdctx *ctx = grpc_mdctx_create();
  grpc_mdelem *token_elem = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response = http_response(200,
                                                 "{"
                                                 " \"expires_in\":3599, "
                                                 " \"token_type\":\"Bearer\"}");
  GPR_ASSERT(grpc_compute_engine_credentials_parse_server_response(
                 &response, ctx, &token_elem, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_mdctx_orphan(ctx);
}

static void test_compute_engine_creds_parsing_missing_token_type(void) {
  grpc_mdctx *ctx = grpc_mdctx_create();
  grpc_mdelem *token_elem = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    "}");
  GPR_ASSERT(grpc_compute_engine_credentials_parse_server_response(
                 &response, ctx, &token_elem, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_mdctx_orphan(ctx);
}

static void test_compute_engine_creds_parsing_missing_token_lifetime(void) {
  grpc_mdctx *ctx = grpc_mdctx_create();
  grpc_mdelem *token_elem = NULL;
  gpr_timespec token_lifetime;
  grpc_httpcli_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"token_type\":\"Bearer\"}");
  GPR_ASSERT(grpc_compute_engine_credentials_parse_server_response(
                 &response, ctx, &token_elem, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_mdctx_orphan(ctx);
}

static void check_metadata(expected_md *expected, grpc_mdelem **md_elems,
                           size_t num_md) {
  size_t i;
  for (i = 0; i < num_md; i++) {
    size_t j;
    for (j = 0; j < num_md; j++) {
      if (0 == gpr_slice_str_cmp(md_elems[j]->key->slice, expected[i].key)) {
        GPR_ASSERT(0 == gpr_slice_str_cmp(md_elems[j]->value->slice,
                                          expected[i].value));
        break;
      }
    }
    if (j == num_md) {
      gpr_log(GPR_ERROR, "key %s not found", expected[i].key);
      GPR_ASSERT(0);
    }
  }
}

static void check_iam_metadata(void *user_data, grpc_mdelem **md_elems,
                               size_t num_md, grpc_credentials_status status) {
  grpc_credentials *c = (grpc_credentials *)user_data;
  expected_md emd[] = {
      {GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY, test_iam_authorization_token},
      {GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY, test_iam_authority_selector}};
  GPR_ASSERT(status == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(num_md == 2);
  check_metadata(emd, md_elems, num_md);
  grpc_credentials_unref(c);
}

static void test_iam_creds(void) {
  grpc_credentials *creds = grpc_iam_credentials_create(
      test_iam_authorization_token, test_iam_authority_selector);
  GPR_ASSERT(grpc_credentials_has_request_metadata(creds));
  GPR_ASSERT(grpc_credentials_has_request_metadata_only(creds));
  grpc_credentials_get_request_metadata(creds, check_iam_metadata, creds);
}

static void check_ssl_oauth2_composite_metadata(
    void *user_data, grpc_mdelem **md_elems, size_t num_md,
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
  grpc_credentials *ssl_creds = grpc_ssl_credentials_create(
      test_root_cert, sizeof(test_root_cert), NULL, 0, NULL, 0);
  const grpc_credentials_array *creds_array;
  grpc_credentials *oauth2_creds =
      grpc_fake_oauth2_credentials_create(test_oauth2_bearer_token, 0);
  grpc_credentials *composite_creds =
      grpc_composite_credentials_create(ssl_creds, oauth2_creds);
  grpc_credentials_unref(ssl_creds);
  grpc_credentials_unref(oauth2_creds);
  GPR_ASSERT(!strcmp(composite_creds->type, GRPC_CREDENTIALS_TYPE_COMPOSITE));
  GPR_ASSERT(grpc_credentials_has_request_metadata(composite_creds));
  GPR_ASSERT(!grpc_credentials_has_request_metadata_only(composite_creds));
  creds_array = grpc_composite_credentials_get_credentials(composite_creds);
  GPR_ASSERT(creds_array->num_creds == 2);
  GPR_ASSERT(
      !strcmp(creds_array->creds_array[0]->type, GRPC_CREDENTIALS_TYPE_SSL));
  GPR_ASSERT(
      !strcmp(creds_array->creds_array[1]->type, GRPC_CREDENTIALS_TYPE_OAUTH2));
  grpc_credentials_get_request_metadata(
      composite_creds, check_ssl_oauth2_composite_metadata, composite_creds);
}

static void check_ssl_oauth2_iam_composite_metadata(
    void *user_data, grpc_mdelem **md_elems, size_t num_md,
    grpc_credentials_status status) {
  grpc_credentials *c = (grpc_credentials *)user_data;
  expected_md emd[] = {
      {GRPC_AUTHORIZATION_METADATA_KEY, test_oauth2_bearer_token},
      {GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY, test_iam_authorization_token},
      {GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY, test_iam_authority_selector}};
  GPR_ASSERT(status == GRPC_CREDENTIALS_OK);
  GPR_ASSERT(num_md == 3);
  check_metadata(emd, md_elems, num_md);
  grpc_credentials_unref(c);
}

static void test_ssl_oauth2_iam_composite_creds(void) {
  grpc_credentials *ssl_creds = grpc_ssl_credentials_create(
      test_root_cert, sizeof(test_root_cert), NULL, 0, NULL, 0);
  const grpc_credentials_array *creds_array;
  grpc_credentials *oauth2_creds =
      grpc_fake_oauth2_credentials_create(test_oauth2_bearer_token, 0);
  grpc_credentials *aux_creds =
      grpc_composite_credentials_create(ssl_creds, oauth2_creds);
  grpc_credentials *iam_creds = grpc_iam_credentials_create(
      test_iam_authorization_token, test_iam_authority_selector);
  grpc_credentials *composite_creds =
      grpc_composite_credentials_create(aux_creds, iam_creds);
  grpc_credentials_unref(ssl_creds);
  grpc_credentials_unref(oauth2_creds);
  grpc_credentials_unref(aux_creds);
  grpc_credentials_unref(iam_creds);
  GPR_ASSERT(!strcmp(composite_creds->type, GRPC_CREDENTIALS_TYPE_COMPOSITE));
  GPR_ASSERT(grpc_credentials_has_request_metadata(composite_creds));
  GPR_ASSERT(!grpc_credentials_has_request_metadata_only(composite_creds));
  creds_array = grpc_composite_credentials_get_credentials(composite_creds);
  GPR_ASSERT(creds_array->num_creds == 3);
  GPR_ASSERT(
      !strcmp(creds_array->creds_array[0]->type, GRPC_CREDENTIALS_TYPE_SSL));
  GPR_ASSERT(
      !strcmp(creds_array->creds_array[1]->type, GRPC_CREDENTIALS_TYPE_OAUTH2));
  GPR_ASSERT(
      !strcmp(creds_array->creds_array[2]->type, GRPC_CREDENTIALS_TYPE_IAM));
  grpc_credentials_get_request_metadata(composite_creds,
                                        check_ssl_oauth2_iam_composite_metadata,
                                        composite_creds);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_compute_engine_creds_parsing_ok();
  test_compute_engine_creds_parsing_bad_http_status();
  test_compute_engine_creds_parsing_empty_http_body();
  test_compute_engine_creds_parsing_invalid_json();
  test_compute_engine_creds_parsing_missing_token();
  test_compute_engine_creds_parsing_missing_token_type();
  test_compute_engine_creds_parsing_missing_token_lifetime();
  test_iam_creds();
  test_ssl_oauth2_composite_creds();
  test_ssl_oauth2_iam_composite_creds();
  return 0;
}
