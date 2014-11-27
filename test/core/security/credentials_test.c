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

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_compute_engine_creds_parsing_ok();
  test_compute_engine_creds_parsing_bad_http_status();
  test_compute_engine_creds_parsing_empty_http_body();
  test_compute_engine_creds_parsing_invalid_json();
  test_compute_engine_creds_parsing_missing_token();
  test_compute_engine_creds_parsing_missing_token_type();
  test_compute_engine_creds_parsing_missing_token_lifetime();
  return 0;
}
