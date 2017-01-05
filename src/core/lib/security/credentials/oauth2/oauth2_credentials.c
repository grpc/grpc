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

#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"

#include <string.h>

#include "src/core/lib/security/util/json_util.h"
#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

//
// Auth Refresh Token.
//

int grpc_auth_refresh_token_is_valid(
    const grpc_auth_refresh_token *refresh_token) {
  return (refresh_token != NULL) &&
         strcmp(refresh_token->type, GRPC_AUTH_JSON_TYPE_INVALID);
}

grpc_auth_refresh_token grpc_auth_refresh_token_create_from_json(
    const grpc_json *json) {
  grpc_auth_refresh_token result;
  const char *prop_value;
  int success = 0;

  memset(&result, 0, sizeof(grpc_auth_refresh_token));
  result.type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (json == NULL) {
    gpr_log(GPR_ERROR, "Invalid json.");
    goto end;
  }

  prop_value = grpc_json_get_string_property(json, "type");
  if (prop_value == NULL ||
      strcmp(prop_value, GRPC_AUTH_JSON_TYPE_AUTHORIZED_USER)) {
    goto end;
  }
  result.type = GRPC_AUTH_JSON_TYPE_AUTHORIZED_USER;

  if (!grpc_copy_json_string_property(json, "client_secret",
                                      &result.client_secret) ||
      !grpc_copy_json_string_property(json, "client_id", &result.client_id) ||
      !grpc_copy_json_string_property(json, "refresh_token",
                                      &result.refresh_token)) {
    goto end;
  }
  success = 1;

end:
  if (!success) grpc_auth_refresh_token_destruct(&result);
  return result;
}

grpc_auth_refresh_token grpc_auth_refresh_token_create_from_string(
    const char *json_string) {
  char *scratchpad = gpr_strdup(json_string);
  grpc_json *json = grpc_json_parse_string(scratchpad);
  grpc_auth_refresh_token result =
      grpc_auth_refresh_token_create_from_json(json);
  if (json != NULL) grpc_json_destroy(json);
  gpr_free(scratchpad);
  return result;
}

void grpc_auth_refresh_token_destruct(grpc_auth_refresh_token *refresh_token) {
  if (refresh_token == NULL) return;
  refresh_token->type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (refresh_token->client_id != NULL) {
    gpr_free(refresh_token->client_id);
    refresh_token->client_id = NULL;
  }
  if (refresh_token->client_secret != NULL) {
    gpr_free(refresh_token->client_secret);
    refresh_token->client_secret = NULL;
  }
  if (refresh_token->refresh_token != NULL) {
    gpr_free(refresh_token->refresh_token);
    refresh_token->refresh_token = NULL;
  }
}

//
// Oauth2 Token Fetcher credentials.
//

static void oauth2_token_fetcher_destruct(grpc_exec_ctx *exec_ctx,
                                          grpc_call_credentials *creds) {
  grpc_oauth2_token_fetcher_credentials *c =
      (grpc_oauth2_token_fetcher_credentials *)creds;
  grpc_credentials_md_store_unref(exec_ctx, c->access_token_md);
  gpr_mu_destroy(&c->mu);
  grpc_httpcli_context_destroy(&c->httpcli_context);
}

grpc_credentials_status
grpc_oauth2_token_fetcher_credentials_parse_server_response(
    grpc_exec_ctx *exec_ctx, const grpc_http_response *response,
    grpc_credentials_md_store **token_md, gpr_timespec *token_lifetime) {
  char *null_terminated_body = NULL;
  char *new_access_token = NULL;
  grpc_credentials_status status = GRPC_CREDENTIALS_OK;
  grpc_json *json = NULL;

  if (response == NULL) {
    gpr_log(GPR_ERROR, "Received NULL response.");
    status = GRPC_CREDENTIALS_ERROR;
    goto end;
  }

  if (response->body_length > 0) {
    null_terminated_body = gpr_malloc(response->body_length + 1);
    null_terminated_body[response->body_length] = '\0';
    memcpy(null_terminated_body, response->body, response->body_length);
  }

  if (response->status != 200) {
    gpr_log(GPR_ERROR, "Call to http server ended with error %d [%s].",
            response->status,
            null_terminated_body != NULL ? null_terminated_body : "");
    status = GRPC_CREDENTIALS_ERROR;
    goto end;
  } else {
    grpc_json *access_token = NULL;
    grpc_json *token_type = NULL;
    grpc_json *expires_in = NULL;
    grpc_json *ptr;
    json = grpc_json_parse_string(null_terminated_body);
    if (json == NULL) {
      gpr_log(GPR_ERROR, "Could not parse JSON from %s", null_terminated_body);
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    if (json->type != GRPC_JSON_OBJECT) {
      gpr_log(GPR_ERROR, "Response should be a JSON object");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    for (ptr = json->child; ptr; ptr = ptr->next) {
      if (strcmp(ptr->key, "access_token") == 0) {
        access_token = ptr;
      } else if (strcmp(ptr->key, "token_type") == 0) {
        token_type = ptr;
      } else if (strcmp(ptr->key, "expires_in") == 0) {
        expires_in = ptr;
      }
    }
    if (access_token == NULL || access_token->type != GRPC_JSON_STRING) {
      gpr_log(GPR_ERROR, "Missing or invalid access_token in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    if (token_type == NULL || token_type->type != GRPC_JSON_STRING) {
      gpr_log(GPR_ERROR, "Missing or invalid token_type in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    if (expires_in == NULL || expires_in->type != GRPC_JSON_NUMBER) {
      gpr_log(GPR_ERROR, "Missing or invalid expires_in in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    gpr_asprintf(&new_access_token, "%s %s", token_type->value,
                 access_token->value);
    token_lifetime->tv_sec = strtol(expires_in->value, NULL, 10);
    token_lifetime->tv_nsec = 0;
    token_lifetime->clock_type = GPR_TIMESPAN;
    if (*token_md != NULL) grpc_credentials_md_store_unref(exec_ctx, *token_md);
    *token_md = grpc_credentials_md_store_create(1);
    grpc_credentials_md_store_add_cstrings(
        *token_md, GRPC_AUTHORIZATION_METADATA_KEY, new_access_token);
    status = GRPC_CREDENTIALS_OK;
  }

end:
  if (status != GRPC_CREDENTIALS_OK && (*token_md != NULL)) {
    grpc_credentials_md_store_unref(exec_ctx, *token_md);
    *token_md = NULL;
  }
  if (null_terminated_body != NULL) gpr_free(null_terminated_body);
  if (new_access_token != NULL) gpr_free(new_access_token);
  if (json != NULL) grpc_json_destroy(json);
  return status;
}

static void on_oauth2_token_fetcher_http_response(grpc_exec_ctx *exec_ctx,
                                                  void *user_data,
                                                  grpc_error *error) {
  grpc_credentials_metadata_request *r =
      (grpc_credentials_metadata_request *)user_data;
  grpc_oauth2_token_fetcher_credentials *c =
      (grpc_oauth2_token_fetcher_credentials *)r->creds;
  gpr_timespec token_lifetime;
  grpc_credentials_status status;

  GRPC_LOG_IF_ERROR("oauth_fetch", GRPC_ERROR_REF(error));

  gpr_mu_lock(&c->mu);
  status = grpc_oauth2_token_fetcher_credentials_parse_server_response(
      exec_ctx, &r->response, &c->access_token_md, &token_lifetime);
  if (status == GRPC_CREDENTIALS_OK) {
    c->token_expiration =
        gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), token_lifetime);
    r->cb(exec_ctx, r->user_data, c->access_token_md->entries,
          c->access_token_md->num_entries, GRPC_CREDENTIALS_OK, NULL);
  } else {
    c->token_expiration = gpr_inf_past(GPR_CLOCK_REALTIME);
    r->cb(exec_ctx, r->user_data, NULL, 0, status,
          "Error occured when fetching oauth2 token.");
  }
  gpr_mu_unlock(&c->mu);
  grpc_credentials_metadata_request_destroy(exec_ctx, r);
}

static void oauth2_token_fetcher_get_request_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *creds,
    grpc_polling_entity *pollent, grpc_auth_metadata_context context,
    grpc_credentials_metadata_cb cb, void *user_data) {
  grpc_oauth2_token_fetcher_credentials *c =
      (grpc_oauth2_token_fetcher_credentials *)creds;
  gpr_timespec refresh_threshold = gpr_time_from_seconds(
      GRPC_SECURE_TOKEN_REFRESH_THRESHOLD_SECS, GPR_TIMESPAN);
  grpc_credentials_md_store *cached_access_token_md = NULL;
  {
    gpr_mu_lock(&c->mu);
    if (c->access_token_md != NULL &&
        (gpr_time_cmp(
             gpr_time_sub(c->token_expiration, gpr_now(GPR_CLOCK_REALTIME)),
             refresh_threshold) > 0)) {
      cached_access_token_md =
          grpc_credentials_md_store_ref(c->access_token_md);
    }
    gpr_mu_unlock(&c->mu);
  }
  if (cached_access_token_md != NULL) {
    cb(exec_ctx, user_data, cached_access_token_md->entries,
       cached_access_token_md->num_entries, GRPC_CREDENTIALS_OK, NULL);
    grpc_credentials_md_store_unref(exec_ctx, cached_access_token_md);
  } else {
    c->fetch_func(
        exec_ctx,
        grpc_credentials_metadata_request_create(creds, cb, user_data),
        &c->httpcli_context, pollent, on_oauth2_token_fetcher_http_response,
        gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), refresh_threshold));
  }
}

static void init_oauth2_token_fetcher(grpc_oauth2_token_fetcher_credentials *c,
                                      grpc_fetch_oauth2_func fetch_func) {
  memset(c, 0, sizeof(grpc_oauth2_token_fetcher_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_OAUTH2;
  gpr_ref_init(&c->base.refcount, 1);
  gpr_mu_init(&c->mu);
  c->token_expiration = gpr_inf_past(GPR_CLOCK_REALTIME);
  c->fetch_func = fetch_func;
  grpc_httpcli_context_init(&c->httpcli_context);
}

//
//  Google Compute Engine credentials.
//

static grpc_call_credentials_vtable compute_engine_vtable = {
    oauth2_token_fetcher_destruct, oauth2_token_fetcher_get_request_metadata};

static void compute_engine_fetch_oauth2(
    grpc_exec_ctx *exec_ctx, grpc_credentials_metadata_request *metadata_req,
    grpc_httpcli_context *httpcli_context, grpc_polling_entity *pollent,
    grpc_iomgr_cb_func response_cb, gpr_timespec deadline) {
  grpc_http_header header = {"Metadata-Flavor", "Google"};
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = GRPC_COMPUTE_ENGINE_METADATA_HOST;
  request.http.path = GRPC_COMPUTE_ENGINE_METADATA_TOKEN_PATH;
  request.http.hdr_count = 1;
  request.http.hdrs = &header;
  /* TODO(ctiller): Carry the resource_quota in ctx and share it with the host
     channel. This would allow us to cancel an authentication query when under
     extreme memory pressure. */
  grpc_resource_quota *resource_quota =
      grpc_resource_quota_create("oauth2_credentials");
  grpc_httpcli_get(
      exec_ctx, httpcli_context, pollent, resource_quota, &request, deadline,
      grpc_closure_create(response_cb, metadata_req, grpc_schedule_on_exec_ctx),

      &metadata_req->response);
  grpc_resource_quota_unref_internal(exec_ctx, resource_quota);
}

grpc_call_credentials *grpc_google_compute_engine_credentials_create(
    void *reserved) {
  grpc_oauth2_token_fetcher_credentials *c =
      gpr_malloc(sizeof(grpc_oauth2_token_fetcher_credentials));
  GRPC_API_TRACE("grpc_compute_engine_credentials_create(reserved=%p)", 1,
                 (reserved));
  GPR_ASSERT(reserved == NULL);
  init_oauth2_token_fetcher(c, compute_engine_fetch_oauth2);
  c->base.vtable = &compute_engine_vtable;
  return &c->base;
}

//
// Google Refresh Token credentials.
//

static void refresh_token_destruct(grpc_exec_ctx *exec_ctx,
                                   grpc_call_credentials *creds) {
  grpc_google_refresh_token_credentials *c =
      (grpc_google_refresh_token_credentials *)creds;
  grpc_auth_refresh_token_destruct(&c->refresh_token);
  oauth2_token_fetcher_destruct(exec_ctx, &c->base.base);
}

static grpc_call_credentials_vtable refresh_token_vtable = {
    refresh_token_destruct, oauth2_token_fetcher_get_request_metadata};

static void refresh_token_fetch_oauth2(
    grpc_exec_ctx *exec_ctx, grpc_credentials_metadata_request *metadata_req,
    grpc_httpcli_context *httpcli_context, grpc_polling_entity *pollent,
    grpc_iomgr_cb_func response_cb, gpr_timespec deadline) {
  grpc_google_refresh_token_credentials *c =
      (grpc_google_refresh_token_credentials *)metadata_req->creds;
  grpc_http_header header = {"Content-Type",
                             "application/x-www-form-urlencoded"};
  grpc_httpcli_request request;
  char *body = NULL;
  gpr_asprintf(&body, GRPC_REFRESH_TOKEN_POST_BODY_FORMAT_STRING,
               c->refresh_token.client_id, c->refresh_token.client_secret,
               c->refresh_token.refresh_token);
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = GRPC_GOOGLE_OAUTH2_SERVICE_HOST;
  request.http.path = GRPC_GOOGLE_OAUTH2_SERVICE_TOKEN_PATH;
  request.http.hdr_count = 1;
  request.http.hdrs = &header;
  request.handshaker = &grpc_httpcli_ssl;
  /* TODO(ctiller): Carry the resource_quota in ctx and share it with the host
     channel. This would allow us to cancel an authentication query when under
     extreme memory pressure. */
  grpc_resource_quota *resource_quota =
      grpc_resource_quota_create("oauth2_credentials_refresh");
  grpc_httpcli_post(
      exec_ctx, httpcli_context, pollent, resource_quota, &request, body,
      strlen(body), deadline,
      grpc_closure_create(response_cb, metadata_req, grpc_schedule_on_exec_ctx),
      &metadata_req->response);
  grpc_resource_quota_unref_internal(exec_ctx, resource_quota);
  gpr_free(body);
}

grpc_call_credentials *
grpc_refresh_token_credentials_create_from_auth_refresh_token(
    grpc_auth_refresh_token refresh_token) {
  grpc_google_refresh_token_credentials *c;
  if (!grpc_auth_refresh_token_is_valid(&refresh_token)) {
    gpr_log(GPR_ERROR, "Invalid input for refresh token credentials creation");
    return NULL;
  }
  c = gpr_malloc(sizeof(grpc_google_refresh_token_credentials));
  memset(c, 0, sizeof(grpc_google_refresh_token_credentials));
  init_oauth2_token_fetcher(&c->base, refresh_token_fetch_oauth2);
  c->base.base.vtable = &refresh_token_vtable;
  c->refresh_token = refresh_token;
  return &c->base.base;
}

static char *create_loggable_refresh_token(grpc_auth_refresh_token *token) {
  if (strcmp(token->type, GRPC_AUTH_JSON_TYPE_INVALID) == 0) {
    return gpr_strdup("<Invalid json token>");
  }
  char *loggable_token = NULL;
  gpr_asprintf(&loggable_token,
               "{\n type: %s\n client_id: %s\n client_secret: "
               "<redacted>\n refresh_token: <redacted>\n}",
               token->type, token->client_id);
  return loggable_token;
}

grpc_call_credentials *grpc_google_refresh_token_credentials_create(
    const char *json_refresh_token, void *reserved) {
  grpc_auth_refresh_token token =
      grpc_auth_refresh_token_create_from_string(json_refresh_token);
  if (grpc_api_trace) {
    char *loggable_token = create_loggable_refresh_token(&token);
    gpr_log(GPR_INFO,
            "grpc_refresh_token_credentials_create(json_refresh_token=%s, "
            "reserved=%p)",
            loggable_token, reserved);
    gpr_free(loggable_token);
  }
  GPR_ASSERT(reserved == NULL);
  return grpc_refresh_token_credentials_create_from_auth_refresh_token(token);
}

//
// Oauth2 Access Token credentials.
//

static void access_token_destruct(grpc_exec_ctx *exec_ctx,
                                  grpc_call_credentials *creds) {
  grpc_access_token_credentials *c = (grpc_access_token_credentials *)creds;
  grpc_credentials_md_store_unref(exec_ctx, c->access_token_md);
}

static void access_token_get_request_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *creds,
    grpc_polling_entity *pollent, grpc_auth_metadata_context context,
    grpc_credentials_metadata_cb cb, void *user_data) {
  grpc_access_token_credentials *c = (grpc_access_token_credentials *)creds;
  cb(exec_ctx, user_data, c->access_token_md->entries, 1, GRPC_CREDENTIALS_OK,
     NULL);
}

static grpc_call_credentials_vtable access_token_vtable = {
    access_token_destruct, access_token_get_request_metadata};

grpc_call_credentials *grpc_access_token_credentials_create(
    const char *access_token, void *reserved) {
  grpc_access_token_credentials *c =
      gpr_malloc(sizeof(grpc_access_token_credentials));
  char *token_md_value;
  GRPC_API_TRACE(
      "grpc_access_token_credentials_create(access_token=<redacted>, "
      "reserved=%p)",
      1, (reserved));
  GPR_ASSERT(reserved == NULL);
  memset(c, 0, sizeof(grpc_access_token_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_OAUTH2;
  c->base.vtable = &access_token_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  c->access_token_md = grpc_credentials_md_store_create(1);
  gpr_asprintf(&token_md_value, "Bearer %s", access_token);
  grpc_credentials_md_store_add_cstrings(
      c->access_token_md, GRPC_AUTHORIZATION_METADATA_KEY, token_md_value);
  gpr_free(token_md_value);
  return &c->base;
}
