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
#include "src/core/surface/surface_em.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "third_party/cJSON/cJSON.h"

#include <string.h>
#include <stdio.h>

/* -- Constants. -- */

#define GRPC_COMPUTE_ENGINE_TOKEN_REFRESH_THRESHOLD_SECS 60
#define GRPC_COMPUTE_ENGINE_METADATA_HOST "metadata"
#define GRPC_COMPUTE_ENGINE_METADATA_TOKEN_PATH \
  "computeMetadata/v1/instance/service-accounts/default/token"
#define GRPC_AUTHORIZATION_METADATA_KEY "Authorization"

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
  if (creds == NULL || !grpc_credentials_has_request_metadata(creds)) return;
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

/* -- ComputeEngine credentials. -- */

typedef struct {
  grpc_credentials base;
  gpr_mu mu;
  grpc_mdctx *md_ctx;
  grpc_mdelem *access_token_md;
  gpr_timespec token_expiration;
} grpc_compute_engine_credentials;

static void compute_engine_destroy(grpc_credentials *creds) {
  grpc_compute_engine_credentials *c = (grpc_compute_engine_credentials *)creds;
  if (c->access_token_md != NULL) {
    grpc_mdelem_unref(c->access_token_md);
  }
  gpr_mu_destroy(&c->mu);
  grpc_mdctx_orphan(c->md_ctx);
  gpr_free(c);
}

static int compute_engine_has_request_metadata(const grpc_credentials *creds) {
  return 1;
}

static int compute_engine_has_request_metadata_only(
    const grpc_credentials *creds) {
  return 1;
}

grpc_credentials_status grpc_compute_engine_credentials_parse_server_response(
    const grpc_httpcli_response *response, grpc_mdctx *ctx,
    grpc_mdelem **token_elem, gpr_timespec *token_lifetime) {
  char *null_terminated_body = NULL;
  char *new_access_token = NULL;
  grpc_credentials_status status = GRPC_CREDENTIALS_OK;
  cJSON *json = NULL;

  if (response->status != 200) {
    gpr_log(GPR_ERROR, "Call to metadata server ended with error %d",
            response->status);
    status = GRPC_CREDENTIALS_ERROR;
    goto end;
  } else {
    cJSON *access_token = NULL;
    cJSON *token_type = NULL;
    cJSON *expires_in = NULL;
    size_t new_access_token_size = 0;
    null_terminated_body = gpr_malloc(response->body_length + 1);
    null_terminated_body[response->body_length] = '\0';
    memcpy(null_terminated_body, response->body, response->body_length);
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

static void on_compute_engine_token_response(
    void *user_data, const grpc_httpcli_response *response) {
  grpc_credentials_metadata_request *r =
      (grpc_credentials_metadata_request *)user_data;
  grpc_compute_engine_credentials *c =
      (grpc_compute_engine_credentials *)r->creds;
  gpr_timespec token_lifetime;
  grpc_credentials_status status;

  gpr_mu_lock(&c->mu);
  status = grpc_compute_engine_credentials_parse_server_response(
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

static void compute_engine_get_request_metadata(grpc_credentials *creds,
                                                grpc_credentials_metadata_cb cb,
                                                void *user_data) {
  grpc_compute_engine_credentials *c = (grpc_compute_engine_credentials *)creds;
  gpr_timespec refresh_threshold = {
      GRPC_COMPUTE_ENGINE_TOKEN_REFRESH_THRESHOLD_SECS, 0};

  gpr_mu_lock(&c->mu);
  if (c->access_token_md == NULL ||
      (gpr_time_cmp(gpr_time_sub(gpr_now(), c->token_expiration),
                    refresh_threshold) < 0)) {
    grpc_httpcli_header header = {"Metadata-Flavor", "Google"};
    grpc_httpcli_request request;
    request.host = GRPC_COMPUTE_ENGINE_METADATA_HOST;
    request.path = GRPC_COMPUTE_ENGINE_METADATA_TOKEN_PATH;
    request.hdr_count = 1;
    request.hdrs = &header;
    grpc_httpcli_get(
        &request, gpr_time_add(gpr_now(), refresh_threshold), grpc_surface_em(),
        on_compute_engine_token_response,
        grpc_credentials_metadata_request_create(creds, cb, user_data));
  } else {
    cb(user_data, &c->access_token_md, 1, GRPC_CREDENTIALS_OK);
  }
  gpr_mu_unlock(&c->mu);
}

static grpc_credentials_vtable compute_engine_vtable = {
    compute_engine_destroy, compute_engine_has_request_metadata,
    compute_engine_has_request_metadata_only,
    compute_engine_get_request_metadata};

grpc_credentials *grpc_compute_engine_credentials_create(void) {
  grpc_compute_engine_credentials *c =
      gpr_malloc(sizeof(grpc_compute_engine_credentials));
  memset(c, 0, sizeof(grpc_compute_engine_credentials));
  c->base.type = GRPC_CREDENTIALS_TYPE_OAUTH2;
  c->base.vtable = &compute_engine_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  gpr_mu_init(&c->mu);
  c->md_ctx = grpc_mdctx_create();
  c->token_expiration = gpr_inf_past;
  return &c->base;
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

void on_simulated_token_fetch_done(void *user_data, grpc_em_cb_status status) {
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
    GPR_ASSERT(grpc_em_add_callback(grpc_surface_em(),
                                    on_simulated_token_fetch_done,
                                    grpc_credentials_metadata_request_create(
                                        creds, cb, user_data)) == GRPC_EM_OK);
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


/* -- Composite credentials TODO(jboeuf). -- */

grpc_credentials *grpc_composite_credentials_create(grpc_credentials *creds1,
                                                    grpc_credentials *creds2) {
  return NULL;
}

/* -- Default credentials TODO(jboeuf). -- */

grpc_credentials *grpc_default_credentials_create(void) { return NULL; }
