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
#include "src/core/iomgr/iomgr.h"
#include "src/core/security/json_token.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "third_party/cJSON/cJSON.h"

#include <string.h>
#include <stdio.h>

/* -- Constants. -- */

#define GRPC_OAUTH2_TOKEN_REFRESH_THRESHOLD_SECS 60

#define GRPC_COMPUTE_ENGINE_METADATA_HOST "metadata"
#define GRPC_COMPUTE_ENGINE_METADATA_TOKEN_PATH \
  "/computeMetadata/v1/instance/service-accounts/default/token"

#define GRPC_SERVICE_ACCOUNT_HOST "www.googleapis.com"
#define GRPC_SERVICE_ACCOUNT_TOKEN_PATH "/oauth2/v3/token"
#define GRPC_SERVICE_ACCOUNT_POST_BODY_PREFIX                         \
  "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer&" \
  "assertion="

/* -- Common. -- */

typedef struct {
  grpc_credentials *creds;
  grpc_credentials_metadata_cb cb;
  void *user_data;
} grpc_credentials_metadata_request;

static grpc_credentials_metadata_request *
grpc_credentials_metadata_request_create(grpc_credentials *creds,
                                         grpc_credentials_metadata_cb cb,
                                         void *user_data) {
  grpc_credentials_metadata_request *r =
      gpr_malloc(sizeof(grpc_credentials_metadata_request));
  r->creds = grpc_credentials_ref(creds);
  r->cb = cb;
  r->user_data = user_data;
  return r;
}

static void grpc_credentials_metadata_request_destroy(
    grpc_credentials_metadata_request *r) {
  grpc_credentials_unref(r->creds);
  gpr_free(r);
}

grpc_credentials *grpc_credentials_ref(grpc_credentials *creds) {
  if (creds == NULL) return NULL;
  gpr_ref(&creds->refcount);
  return creds;
}

void grpc_credentials_unref(grpc_credentials *creds) {
  if (creds == NULL) return;
  if (gpr_unref(&creds->refcount)) creds->vtable->destroy(creds);
}

void grpc_credentials_release(grpc_credentials *creds) {
  grpc_credentials_unref(creds);
}

int grpc_credentials_has_request_metadata(grpc_credentials *creds) {
  if (creds == NULL) return 0;
  return creds->vtable->has_request_metadata(creds);
}

int grpc_credentials_has_request_metadata_only(grpc_credentials *creds) {
  if (creds == NULL) return 0;
  return creds->vtable->has_request_metadata_only(creds);
}

void grpc_credentials_get_request_metadata(grpc_credentials *creds,
                                           grpc_credentials_metadata_cb cb,
                                           void *user_data) {
  if (creds == NULL || !grpc_credentials_has_request_metadata(creds) ||
      creds->vtable->get_request_metadata == NULL) {
    if (cb != NULL) {
      cb(user_data, NULL, 0, GRPC_CREDENTIALS_OK);
    }
    return;
  }
  creds->vtable->get_request_metadata(creds, cb, user_data);
}

void grpc_server_credentials_release(grpc_server_credentials *creds) {
  if (creds == NULL) return;
  creds->vtable->destroy(creds);
}

/* -- Ssl credentials. -- */

typedef struct {
  grpc_credentials base;
  grpc_ssl_config config;
} grpc_ssl_credentials;

typedef struct {
  grpc_server_credentials base;
  grpc_ssl_config config;
} grpc_ssl_server_credentials;

static void ssl_destroy(grpc_credentials *creds) {
  grpc_ssl_credentials *c = (grpc_ssl_credentials *)creds;
  if (c->config.pem_root_certs != NULL) gpr_free(c->config.pem_root_certs);
  if (c->config.pem_private_key != NULL) gpr_free(c->config.pem_private_key);
  if (c->config.pem_cert_chain != NULL) gpr_free(c->config.pem_cert_chain);
  gpr_free(creds);
}

static void ssl_server_destroy(grpc_server_credentials *creds) {
  grpc_ssl_server_credentials *c = (grpc_ssl_server_credentials *)creds;
  if (c->config.pem_root_certs != NULL) gpr_free(c->config.pem_root_certs);
  if (c->config.pem_private_key != NULL) gpr_free(c->config.pem_private_key);
  if (c->config.pem_cert_chain != NULL) gpr_free(c->config.pem_cert_chain);
  gpr_free(creds);
}

static int ssl_has_request_metadata(const grpc_credentials *creds) { return 0; }

static int ssl_has_request_metadata_only(const grpc_credentials *creds) {
  return 0;
}

static grpc_credentials_vtable ssl_vtable = {
    ssl_destroy, ssl_has_request_metadata, ssl_has_request_metadata_only, NULL};

static grpc_server_credentials_vtable ssl_server_vtable = {ssl_server_destroy};

const grpc_ssl_config *grpc_ssl_credentials_get_config(
    const grpc_credentials *creds) {
  if (creds == NULL || strcmp(creds->type, GRPC_CREDENTIALS_TYPE_SSL)) {
    return NULL;
  } else {
    grpc_ssl_credentials *c = (grpc_ssl_credentials *)creds;
    return &c->config;
  }
}

const grpc_ssl_config *grpc_ssl_server_credentials_get_config(
    const grpc_server_credentials *creds) {
  if (creds == NULL || strcmp(creds->type, GRPC_CREDENTIALS_TYPE_SSL)) {
    return NULL;
  } else {
    grpc_ssl_server_credentials *c = (grpc_ssl_server_credentials *)creds;
    return &c->config;
  }
}

static void ssl_build_config(const unsigned char *pem_root_certs,
                             size_t pem_root_certs_size,
                             const unsigned char *pem_private_key,
                             size_t pem_private_key_size,
                             const unsigned char *pem_cert_chain,
                             size_t pem_cert_chain_size,
                             grpc_ssl_config *config) {
  if (pem_root_certs != NULL) {
    config->pem_root_certs = gpr_malloc(pem_root_certs_size);
    memcpy(config->pem_root_certs, pem_root_certs, pem_root_certs_size);
    config->pem_root_certs_size = pem_root_certs_size;
  }
  if (pem_private_key != NULL) {
    config->pem_private_key = gpr_malloc(pem_private_key_size);
    memcpy(config->pem_private_key, pem_private_key, pem_private_key_size);
    config->pem_private_key_size = pem_private_key_size;
  }
  if (pem_cert_chain != NULL) {
    config->pem_cert_chain = gpr_malloc(pem_cert_chain_size);
    memcpy(config->pem_cert_chain, pem_cert_chain, pem_cert_chain_size);
    config->pem_cert_chain_size = pem_cert_chain_size;
  }
}

grpc_credentials *grpc_ssl_credentials_create(
    const unsigned char *pem_root_certs, size_t pem_root_certs_size,
    const unsigned char *pem_private_key, size_t pem_private_key_size,
    const unsigned char *pem_cert_chain, size_t pem_cert_chain_size) {
  grpc_ssl_credentials *c = gpr_malloc(sizeof(grpc_ssl_credentials));
  memset(c, 0, sizeof(grpc_ssl_credentials));
  c->base.type = GRPC_CREDENTIALS_TYPE_SSL;
  c->base.vtable = &ssl_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  ssl_build_config(pem_root_certs, pem_root_certs_size, pem_private_key,
                   pem_private_key_size, pem_cert_chain, pem_cert_chain_size,
                   &c->config);
  return &c->base;
}

grpc_server_credentials *grpc_ssl_server_credentials_create(
    const unsigned char *pem_root_certs, size_t pem_root_certs_size,
    const unsigned char *pem_private_key, size_t pem_private_key_size,
    const unsigned char *pem_cert_chain, size_t pem_cert_chain_size) {
  grpc_ssl_server_credentials *c =
      gpr_malloc(sizeof(grpc_ssl_server_credentials));
  memset(c, 0, sizeof(grpc_ssl_server_credentials));
  c->base.type = GRPC_CREDENTIALS_TYPE_SSL;
  c->base.vtable = &ssl_server_vtable;
  ssl_build_config(pem_root_certs, pem_root_certs_size, pem_private_key,
                   pem_private_key_size, pem_cert_chain, pem_cert_chain_size,
                   &c->config);
  return &c->base;
}

/* -- Oauth2TokenFetcher credentials -- */

/* This object is a base for credentials that need to acquire an oauth2 token
   from an http service. */

typedef void (*grpc_fetch_oauth2_func)(grpc_credentials_metadata_request *req,
                                       grpc_httpcli_response_cb response_cb,
                                       gpr_timespec deadline);

typedef struct {
  grpc_credentials base;
  gpr_mu mu;
  grpc_mdctx *md_ctx;
  grpc_mdelem *access_token_md;
  gpr_timespec token_expiration;
  grpc_fetch_oauth2_func fetch_func;
} grpc_oauth2_token_fetcher_credentials;

static void oauth2_token_fetcher_destroy(grpc_credentials *creds) {
  grpc_oauth2_token_fetcher_credentials *c =
      (grpc_oauth2_token_fetcher_credentials *)creds;
  if (c->access_token_md != NULL) {
    grpc_mdelem_unref(c->access_token_md);
  }
  gpr_mu_destroy(&c->mu);
  grpc_mdctx_orphan(c->md_ctx);
  gpr_free(c);
}

static int oauth2_token_fetcher_has_request_metadata(
    const grpc_credentials *creds) {
  return 1;
}

static int oauth2_token_fetcher_has_request_metadata_only(
    const grpc_credentials *creds) {
  return 1;
}

grpc_credentials_status
grpc_oauth2_token_fetcher_credentials_parse_server_response(
    const grpc_httpcli_response *response, grpc_mdctx *ctx,
    grpc_mdelem **token_elem, gpr_timespec *token_lifetime) {
  char *null_terminated_body = NULL;
  char *new_access_token = NULL;
  grpc_credentials_status status = GRPC_CREDENTIALS_OK;
  cJSON *json = NULL;

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
    cJSON *access_token = NULL;
    cJSON *token_type = NULL;
    cJSON *expires_in = NULL;
    size_t new_access_token_size = 0;
    json = cJSON_Parse(null_terminated_body);
    if (json == NULL) {
      gpr_log(GPR_ERROR, "Could not parse JSON from %s", null_terminated_body);
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    if (json->type != cJSON_Object) {
      gpr_log(GPR_ERROR, "Response should be a JSON object");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    access_token = cJSON_GetObjectItem(json, "access_token");
    if (access_token == NULL || access_token->type != cJSON_String) {
      gpr_log(GPR_ERROR, "Missing or invalid access_token in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    token_type = cJSON_GetObjectItem(json, "token_type");
    if (token_type == NULL || token_type->type != cJSON_String) {
      gpr_log(GPR_ERROR, "Missing or invalid token_type in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    expires_in = cJSON_GetObjectItem(json, "expires_in");
    if (expires_in == NULL || expires_in->type != cJSON_Number) {
      gpr_log(GPR_ERROR, "Missing or invalid expires_in in JSON.");
      status = GRPC_CREDENTIALS_ERROR;
      goto end;
    }
    new_access_token_size = strlen(token_type->valuestring) + 1 +
                            strlen(access_token->valuestring) + 1;
    new_access_token = gpr_malloc(new_access_token_size);
    /* C89 does not have snprintf :(. */
    sprintf(new_access_token, "%s %s", token_type->valuestring,
            access_token->valuestring);
    token_lifetime->tv_sec = expires_in->valueint;
    token_lifetime->tv_nsec = 0;
    if (*token_elem != NULL) grpc_mdelem_unref(*token_elem);
    *token_elem = grpc_mdelem_from_strings(ctx, GRPC_AUTHORIZATION_METADATA_KEY,
                                           new_access_token);
    status = GRPC_CREDENTIALS_OK;
  }

end:
  if (status != GRPC_CREDENTIALS_OK && (*token_elem != NULL)) {
    grpc_mdelem_unref(*token_elem);
    *token_elem = NULL;
  }
  if (null_terminated_body != NULL) gpr_free(null_terminated_body);
  if (new_access_token != NULL) gpr_free(new_access_token);
  if (json != NULL) cJSON_Delete(json);
  return status;
}

static void on_oauth2_token_fetcher_http_response(
    void *user_data, const grpc_httpcli_response *response) {
  grpc_credentials_metadata_request *r =
      (grpc_credentials_metadata_request *)user_data;
  grpc_oauth2_token_fetcher_credentials *c =
      (grpc_oauth2_token_fetcher_credentials *)r->creds;
  gpr_timespec token_lifetime;
  grpc_credentials_status status;

  gpr_mu_lock(&c->mu);
  status = grpc_oauth2_token_fetcher_credentials_parse_server_response(
      response, c->md_ctx, &c->access_token_md, &token_lifetime);
  if (status == GRPC_CREDENTIALS_OK) {
    c->token_expiration = gpr_time_add(gpr_now(), token_lifetime);
    r->cb(r->user_data, &c->access_token_md, 1, status);
  } else {
    c->token_expiration = gpr_inf_past;
    r->cb(r->user_data, NULL, 0, status);
  }
  gpr_mu_unlock(&c->mu);
  grpc_credentials_metadata_request_destroy(r);
}

static void oauth2_token_fetcher_get_request_metadata(
    grpc_credentials *creds, grpc_credentials_metadata_cb cb, void *user_data) {
  grpc_oauth2_token_fetcher_credentials *c =
      (grpc_oauth2_token_fetcher_credentials *)creds;
  gpr_timespec refresh_threshold = {GRPC_OAUTH2_TOKEN_REFRESH_THRESHOLD_SECS,
                                    0};
  grpc_mdelem *cached_access_token_md = NULL;
  {
    gpr_mu_lock(&c->mu);
    if (c->access_token_md != NULL &&
        (gpr_time_cmp(gpr_time_sub(c->token_expiration, gpr_now()),
                      refresh_threshold) > 0)) {
      cached_access_token_md = grpc_mdelem_ref(c->access_token_md);
    }
    gpr_mu_unlock(&c->mu);
  }
  if (cached_access_token_md != NULL) {
    cb(user_data, &cached_access_token_md, 1, GRPC_CREDENTIALS_OK);
    grpc_mdelem_unref(cached_access_token_md);
  } else {
    c->fetch_func(
        grpc_credentials_metadata_request_create(creds, cb, user_data),
        on_oauth2_token_fetcher_http_response,
        gpr_time_add(gpr_now(), refresh_threshold));
  }
}

static void init_oauth2_token_fetcher(grpc_oauth2_token_fetcher_credentials *c,
                                      grpc_fetch_oauth2_func fetch_func) {
  memset(c, 0, sizeof(grpc_oauth2_token_fetcher_credentials));
  c->base.type = GRPC_CREDENTIALS_TYPE_OAUTH2;
  gpr_ref_init(&c->base.refcount, 1);
  gpr_mu_init(&c->mu);
  c->md_ctx = grpc_mdctx_create();
  c->token_expiration = gpr_inf_past;
  c->fetch_func = fetch_func;
}

/* -- ComputeEngine credentials. -- */

static grpc_credentials_vtable compute_engine_vtable = {
    oauth2_token_fetcher_destroy, oauth2_token_fetcher_has_request_metadata,
    oauth2_token_fetcher_has_request_metadata_only,
    oauth2_token_fetcher_get_request_metadata};

static void compute_engine_fetch_oauth2(
    grpc_credentials_metadata_request *metadata_req,
    grpc_httpcli_response_cb response_cb, gpr_timespec deadline) {
  grpc_httpcli_header header = {"Metadata-Flavor", "Google"};
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = GRPC_COMPUTE_ENGINE_METADATA_HOST;
  request.path = GRPC_COMPUTE_ENGINE_METADATA_TOKEN_PATH;
  request.hdr_count = 1;
  request.hdrs = &header;
  grpc_httpcli_get(&request, deadline, response_cb, metadata_req);
}

grpc_credentials *grpc_compute_engine_credentials_create(void) {
  grpc_oauth2_token_fetcher_credentials *c =
      gpr_malloc(sizeof(grpc_oauth2_token_fetcher_credentials));
  init_oauth2_token_fetcher(c, compute_engine_fetch_oauth2);
  c->base.vtable = &compute_engine_vtable;
  return &c->base;
}

/* -- ServiceAccount credentials. -- */

typedef struct {
  grpc_oauth2_token_fetcher_credentials base;
  grpc_auth_json_key key;
  char *scope;
  gpr_timespec token_lifetime;
} grpc_service_account_credentials;

static void service_account_destroy(grpc_credentials *creds) {
  grpc_service_account_credentials *c =
      (grpc_service_account_credentials *)creds;
  if (c->scope != NULL) gpr_free(c->scope);
  grpc_auth_json_key_destruct(&c->key);
  oauth2_token_fetcher_destroy(&c->base.base);
}

static grpc_credentials_vtable service_account_vtable = {
    service_account_destroy, oauth2_token_fetcher_has_request_metadata,
    oauth2_token_fetcher_has_request_metadata_only,
    oauth2_token_fetcher_get_request_metadata};

static void service_account_fetch_oauth2(
    grpc_credentials_metadata_request *metadata_req,
    grpc_httpcli_response_cb response_cb, gpr_timespec deadline) {
  grpc_service_account_credentials *c =
      (grpc_service_account_credentials *)metadata_req->creds;
  grpc_httpcli_header header = {"Content-Type",
                                "application/x-www-form-urlencoded"};
  grpc_httpcli_request request;
  char *body = NULL;
  char *jwt = grpc_jwt_encode_and_sign(&c->key, c->scope, c->token_lifetime);
  if (jwt == NULL) {
    grpc_httpcli_response response;
    memset(&response, 0, sizeof(grpc_httpcli_response));
    response.status = 400; /* Invalid request. */
    gpr_log(GPR_ERROR, "Could not create signed jwt.");
    /* Do not even send the request, just call the response callback. */
    response_cb(metadata_req, &response);
    return;
  }
  body = gpr_malloc(strlen(GRPC_SERVICE_ACCOUNT_POST_BODY_PREFIX) +
                    strlen(jwt) + 1);
  sprintf(body, "%s%s", GRPC_SERVICE_ACCOUNT_POST_BODY_PREFIX, jwt);
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = GRPC_SERVICE_ACCOUNT_HOST;
  request.path = GRPC_SERVICE_ACCOUNT_TOKEN_PATH;
  request.hdr_count = 1;
  request.hdrs = &header;
  request.use_ssl = 1;
  grpc_httpcli_post(&request, body, strlen(body), deadline, response_cb,
                    metadata_req);
  gpr_free(body);
  gpr_free(jwt);
}

grpc_credentials *grpc_service_account_credentials_create(
    const char *json_key, const char *scope, gpr_timespec token_lifetime) {
  grpc_service_account_credentials *c;
  grpc_auth_json_key key = grpc_auth_json_key_create_from_string(json_key);

  if (scope == NULL || (strlen(scope) == 0) ||
      !grpc_auth_json_key_is_valid(&key)) {
    gpr_log(GPR_ERROR,
            "Invalid input for service account credentials creation");
    return NULL;
  }
  c = gpr_malloc(sizeof(grpc_service_account_credentials));
  memset(c, 0, sizeof(grpc_service_account_credentials));
  init_oauth2_token_fetcher(&c->base, service_account_fetch_oauth2);
  c->base.base.vtable = &service_account_vtable;
  c->scope = gpr_strdup(scope);
  c->key = key;
  c->token_lifetime = token_lifetime;
  return &c->base.base;
}

/* -- Fake Oauth2 credentials. -- */

typedef struct {
  grpc_credentials base;
  grpc_mdctx *md_ctx;
  grpc_mdelem *access_token_md;
  int is_async;
} grpc_fake_oauth2_credentials;

static void fake_oauth2_destroy(grpc_credentials *creds) {
  grpc_fake_oauth2_credentials *c = (grpc_fake_oauth2_credentials *)creds;
  if (c->access_token_md != NULL) {
    grpc_mdelem_unref(c->access_token_md);
  }
  grpc_mdctx_orphan(c->md_ctx);
  gpr_free(c);
}

static int fake_oauth2_has_request_metadata(const grpc_credentials *creds) {
  return 1;
}

static int fake_oauth2_has_request_metadata_only(
    const grpc_credentials *creds) {
  return 1;
}

void on_simulated_token_fetch_done(void *user_data,
                                   grpc_iomgr_cb_status status) {
  grpc_credentials_metadata_request *r =
      (grpc_credentials_metadata_request *)user_data;
  grpc_fake_oauth2_credentials *c = (grpc_fake_oauth2_credentials *)r->creds;
  GPR_ASSERT(status == GRPC_CALLBACK_SUCCESS);
  r->cb(r->user_data, &c->access_token_md, 1, GRPC_CREDENTIALS_OK);
  grpc_credentials_metadata_request_destroy(r);
}

static void fake_oauth2_get_request_metadata(grpc_credentials *creds,
                                             grpc_credentials_metadata_cb cb,
                                             void *user_data) {
  grpc_fake_oauth2_credentials *c = (grpc_fake_oauth2_credentials *)creds;

  if (c->is_async) {
    grpc_iomgr_add_callback(
        on_simulated_token_fetch_done,
        grpc_credentials_metadata_request_create(creds, cb, user_data));
  } else {
    cb(user_data, &c->access_token_md, 1, GRPC_CREDENTIALS_OK);
  }
}

static grpc_credentials_vtable fake_oauth2_vtable = {
    fake_oauth2_destroy, fake_oauth2_has_request_metadata,
    fake_oauth2_has_request_metadata_only, fake_oauth2_get_request_metadata};

grpc_credentials *grpc_fake_oauth2_credentials_create(
    const char *token_md_value, int is_async) {
  grpc_fake_oauth2_credentials *c =
      gpr_malloc(sizeof(grpc_fake_oauth2_credentials));
  memset(c, 0, sizeof(grpc_fake_oauth2_credentials));
  c->base.type = GRPC_CREDENTIALS_TYPE_OAUTH2;
  c->base.vtable = &fake_oauth2_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  c->md_ctx = grpc_mdctx_create();
  c->access_token_md = grpc_mdelem_from_strings(
      c->md_ctx, GRPC_AUTHORIZATION_METADATA_KEY, token_md_value);
  c->is_async = is_async;
  return &c->base;
}

/* -- Fake transport security credentials. -- */

static void fake_transport_security_credentials_destroy(
    grpc_credentials *creds) {
  gpr_free(creds);
}

static void fake_transport_security_server_credentials_destroy(
    grpc_server_credentials *creds) {
  gpr_free(creds);
}

static int fake_transport_security_has_request_metadata(
    const grpc_credentials *creds) {
  return 0;
}

static int fake_transport_security_has_request_metadata_only(
    const grpc_credentials *creds) {
  return 0;
}

static grpc_credentials_vtable fake_transport_security_credentials_vtable = {
    fake_transport_security_credentials_destroy,
    fake_transport_security_has_request_metadata,
    fake_transport_security_has_request_metadata_only, NULL};

static grpc_server_credentials_vtable
    fake_transport_security_server_credentials_vtable = {
        fake_transport_security_server_credentials_destroy};

grpc_credentials *grpc_fake_transport_security_credentials_create(void) {
  grpc_credentials *c = gpr_malloc(sizeof(grpc_credentials));
  memset(c, 0, sizeof(grpc_credentials));
  c->type = GRPC_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY;
  c->vtable = &fake_transport_security_credentials_vtable;
  gpr_ref_init(&c->refcount, 1);
  return c;
}

grpc_server_credentials *
grpc_fake_transport_security_server_credentials_create() {
  grpc_server_credentials *c = gpr_malloc(sizeof(grpc_server_credentials));
  memset(c, 0, sizeof(grpc_server_credentials));
  c->type = GRPC_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY;
  c->vtable = &fake_transport_security_server_credentials_vtable;
  return c;
}

/* -- Composite credentials. -- */

typedef struct {
  grpc_credentials base;
  grpc_credentials_array inner;
} grpc_composite_credentials;

typedef struct {
  grpc_composite_credentials *composite_creds;
  size_t creds_index;
  grpc_mdelem **md_elems;
  size_t num_md;
  void *user_data;
  grpc_credentials_metadata_cb cb;
} grpc_composite_credentials_metadata_context;

static void composite_destroy(grpc_credentials *creds) {
  grpc_composite_credentials *c = (grpc_composite_credentials *)creds;
  size_t i;
  for (i = 0; i < c->inner.num_creds; i++) {
    grpc_credentials_unref(c->inner.creds_array[i]);
  }
  gpr_free(c->inner.creds_array);
  gpr_free(creds);
}

static int composite_has_request_metadata(const grpc_credentials *creds) {
  const grpc_composite_credentials *c =
      (const grpc_composite_credentials *)creds;
  size_t i;
  for (i = 0; i < c->inner.num_creds; i++) {
    if (grpc_credentials_has_request_metadata(c->inner.creds_array[i])) {
      return 1;
    }
  }
  return 0;
}

static int composite_has_request_metadata_only(const grpc_credentials *creds) {
  const grpc_composite_credentials *c =
      (const grpc_composite_credentials *)creds;
  size_t i;
  for (i = 0; i < c->inner.num_creds; i++) {
    if (!grpc_credentials_has_request_metadata_only(c->inner.creds_array[i])) {
      return 0;
    }
  }
  return 1;
}

static void composite_md_context_destroy(
    grpc_composite_credentials_metadata_context *ctx) {
  size_t i;
  for (i = 0; i < ctx->num_md; i++) {
    grpc_mdelem_unref(ctx->md_elems[i]);
  }
  gpr_free(ctx->md_elems);
  gpr_free(ctx);
}

static void composite_metadata_cb(void *user_data, grpc_mdelem **md_elems,
                                  size_t num_md,
                                  grpc_credentials_status status) {
  grpc_composite_credentials_metadata_context *ctx =
      (grpc_composite_credentials_metadata_context *)user_data;
  size_t i;
  if (status != GRPC_CREDENTIALS_OK) {
    ctx->cb(ctx->user_data, NULL, 0, status);
    return;
  }

  /* Copy the metadata in the context. */
  if (num_md > 0) {
    ctx->md_elems = gpr_realloc(ctx->md_elems,
                                (ctx->num_md + num_md) * sizeof(grpc_mdelem *));
    for (i = 0; i < num_md; i++) {
      ctx->md_elems[i + ctx->num_md] = grpc_mdelem_ref(md_elems[i]);
    }
    ctx->num_md += num_md;
  }

  /* See if we need to get some more metadata. */
  while (ctx->creds_index < ctx->composite_creds->inner.num_creds) {
    grpc_credentials *inner_creds =
        ctx->composite_creds->inner.creds_array[ctx->creds_index++];
    if (grpc_credentials_has_request_metadata(inner_creds)) {
      grpc_credentials_get_request_metadata(inner_creds, composite_metadata_cb,
                                            ctx);
      return;
    }
  }

  /* We're done!. */
  ctx->cb(ctx->user_data, ctx->md_elems, ctx->num_md, GRPC_CREDENTIALS_OK);
  composite_md_context_destroy(ctx);
}

static void composite_get_request_metadata(grpc_credentials *creds,
                                           grpc_credentials_metadata_cb cb,
                                           void *user_data) {
  grpc_composite_credentials *c = (grpc_composite_credentials *)creds;
  grpc_composite_credentials_metadata_context *ctx;
  if (!grpc_credentials_has_request_metadata(creds)) {
    cb(user_data, NULL, 0, GRPC_CREDENTIALS_OK);
    return;
  }
  ctx = gpr_malloc(sizeof(grpc_composite_credentials_metadata_context));
  memset(ctx, 0, sizeof(grpc_composite_credentials_metadata_context));
  ctx->user_data = user_data;
  ctx->cb = cb;
  ctx->composite_creds = c;
  while (ctx->creds_index < c->inner.num_creds) {
    grpc_credentials *inner_creds = c->inner.creds_array[ctx->creds_index++];
    if (grpc_credentials_has_request_metadata(inner_creds)) {
      grpc_credentials_get_request_metadata(inner_creds, composite_metadata_cb,
                                            ctx);
      return;
    }
  }
  GPR_ASSERT(0); /* Should have exited before. */
}

static grpc_credentials_vtable composite_credentials_vtable = {
    composite_destroy, composite_has_request_metadata,
    composite_has_request_metadata_only, composite_get_request_metadata};

static grpc_credentials_array get_creds_array(grpc_credentials **creds_addr) {
  grpc_credentials_array result;
  grpc_credentials *creds = *creds_addr;
  result.creds_array = creds_addr;
  result.num_creds = 1;
  if (!strcmp(creds->type, GRPC_CREDENTIALS_TYPE_COMPOSITE)) {
    result = *grpc_composite_credentials_get_credentials(creds);
  }
  return result;
}

grpc_credentials *grpc_composite_credentials_create(grpc_credentials *creds1,
                                                    grpc_credentials *creds2) {
  size_t i;
  grpc_credentials_array creds1_array;
  grpc_credentials_array creds2_array;
  grpc_composite_credentials *c;
  GPR_ASSERT(creds1 != NULL);
  GPR_ASSERT(creds2 != NULL);
  c = gpr_malloc(sizeof(grpc_composite_credentials));
  memset(c, 0, sizeof(grpc_composite_credentials));
  c->base.type = GRPC_CREDENTIALS_TYPE_COMPOSITE;
  c->base.vtable = &composite_credentials_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  creds1_array = get_creds_array(&creds1);
  creds2_array = get_creds_array(&creds2);
  c->inner.num_creds = creds1_array.num_creds + creds2_array.num_creds;
  c->inner.creds_array =
      gpr_malloc(c->inner.num_creds * sizeof(grpc_credentials *));
  for (i = 0; i < creds1_array.num_creds; i++) {
    c->inner.creds_array[i] = grpc_credentials_ref(creds1_array.creds_array[i]);
  }
  for (i = 0; i < creds2_array.num_creds; i++) {
    c->inner.creds_array[i + creds1_array.num_creds] =
        grpc_credentials_ref(creds2_array.creds_array[i]);
  }
  return &c->base;
}

const grpc_credentials_array *grpc_composite_credentials_get_credentials(
    grpc_credentials *creds) {
  const grpc_composite_credentials *c =
      (const grpc_composite_credentials *)creds;
  GPR_ASSERT(!strcmp(creds->type, GRPC_CREDENTIALS_TYPE_COMPOSITE));
  return &c->inner;
}

/* -- IAM credentials. -- */

typedef struct {
  grpc_credentials base;
  grpc_mdctx *md_ctx;
  grpc_mdelem *token_md;
  grpc_mdelem *authority_selector_md;
} grpc_iam_credentials;

static void iam_destroy(grpc_credentials *creds) {
  grpc_iam_credentials *c = (grpc_iam_credentials *)creds;
  grpc_mdelem_unref(c->token_md);
  grpc_mdelem_unref(c->authority_selector_md);
  grpc_mdctx_orphan(c->md_ctx);
  gpr_free(c);
}

static int iam_has_request_metadata(const grpc_credentials *creds) { return 1; }

static int iam_has_request_metadata_only(const grpc_credentials *creds) {
  return 1;
}

static void iam_get_request_metadata(grpc_credentials *creds,
                                     grpc_credentials_metadata_cb cb,
                                     void *user_data) {
  grpc_iam_credentials *c = (grpc_iam_credentials *)creds;
  grpc_mdelem *md_array[2];
  md_array[0] = c->token_md;
  md_array[1] = c->authority_selector_md;
  cb(user_data, md_array, 2, GRPC_CREDENTIALS_OK);
}

static grpc_credentials_vtable iam_vtable = {
    iam_destroy, iam_has_request_metadata, iam_has_request_metadata_only,
    iam_get_request_metadata};

grpc_credentials *grpc_iam_credentials_create(const char *token,
                                              const char *authority_selector) {
  grpc_iam_credentials *c;
  GPR_ASSERT(token != NULL);
  GPR_ASSERT(authority_selector != NULL);
  c = gpr_malloc(sizeof(grpc_iam_credentials));
  memset(c, 0, sizeof(grpc_iam_credentials));
  c->base.type = GRPC_CREDENTIALS_TYPE_IAM;
  c->base.vtable = &iam_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  c->md_ctx = grpc_mdctx_create();
  c->token_md = grpc_mdelem_from_strings(
      c->md_ctx, GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY, token);
  c->authority_selector_md = grpc_mdelem_from_strings(
      c->md_ctx, GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY, authority_selector);
  return &c->base;
}

/* -- Default credentials TODO(jboeuf). -- */

grpc_credentials *grpc_default_credentials_create(void) { return NULL; }

