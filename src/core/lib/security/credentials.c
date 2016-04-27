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

#include "src/core/lib/security/credentials.h"

#include <stdio.h>
#include <string.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/http_client_filter.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

/* -- Common. -- */

struct grpc_credentials_metadata_request {
  grpc_call_credentials *creds;
  grpc_credentials_metadata_cb cb;
  void *user_data;
};

static grpc_credentials_metadata_request *
grpc_credentials_metadata_request_create(grpc_call_credentials *creds,
                                         grpc_credentials_metadata_cb cb,
                                         void *user_data) {
  grpc_credentials_metadata_request *r =
      gpr_malloc(sizeof(grpc_credentials_metadata_request));
  r->creds = grpc_call_credentials_ref(creds);
  r->cb = cb;
  r->user_data = user_data;
  return r;
}

static void grpc_credentials_metadata_request_destroy(
    grpc_credentials_metadata_request *r) {
  grpc_call_credentials_unref(r->creds);
  gpr_free(r);
}

grpc_channel_credentials *grpc_channel_credentials_ref(
    grpc_channel_credentials *creds) {
  if (creds == NULL) return NULL;
  gpr_ref(&creds->refcount);
  return creds;
}

void grpc_channel_credentials_unref(grpc_channel_credentials *creds) {
  if (creds == NULL) return;
  if (gpr_unref(&creds->refcount)) {
    if (creds->vtable->destruct != NULL) creds->vtable->destruct(creds);
    gpr_free(creds);
  }
}

void grpc_channel_credentials_release(grpc_channel_credentials *creds) {
  GRPC_API_TRACE("grpc_channel_credentials_release(creds=%p)", 1, (creds));
  grpc_channel_credentials_unref(creds);
}

grpc_call_credentials *grpc_call_credentials_ref(grpc_call_credentials *creds) {
  if (creds == NULL) return NULL;
  gpr_ref(&creds->refcount);
  return creds;
}

void grpc_call_credentials_unref(grpc_call_credentials *creds) {
  if (creds == NULL) return;
  if (gpr_unref(&creds->refcount)) {
    if (creds->vtable->destruct != NULL) creds->vtable->destruct(creds);
    gpr_free(creds);
  }
}

void grpc_call_credentials_release(grpc_call_credentials *creds) {
  GRPC_API_TRACE("grpc_call_credentials_release(creds=%p)", 1, (creds));
  grpc_call_credentials_unref(creds);
}

void grpc_call_credentials_get_request_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *creds,
    grpc_pollset *pollset, grpc_auth_metadata_context context,
    grpc_credentials_metadata_cb cb, void *user_data) {
  if (creds == NULL || creds->vtable->get_request_metadata == NULL) {
    if (cb != NULL) {
      cb(exec_ctx, user_data, NULL, 0, GRPC_CREDENTIALS_OK);
    }
    return;
  }
  creds->vtable->get_request_metadata(exec_ctx, creds, pollset, context, cb,
                                      user_data);
}

grpc_security_status grpc_channel_credentials_create_security_connector(
    grpc_channel_credentials *channel_creds, const char *target,
    const grpc_channel_args *args, grpc_channel_security_connector **sc,
    grpc_channel_args **new_args) {
  *new_args = NULL;
  if (channel_creds == NULL) {
    return GRPC_SECURITY_ERROR;
  }
  GPR_ASSERT(channel_creds->vtable->create_security_connector != NULL);
  return channel_creds->vtable->create_security_connector(
      channel_creds, NULL, target, args, sc, new_args);
}

grpc_server_credentials *grpc_server_credentials_ref(
    grpc_server_credentials *creds) {
  if (creds == NULL) return NULL;
  gpr_ref(&creds->refcount);
  return creds;
}

void grpc_server_credentials_unref(grpc_server_credentials *creds) {
  if (creds == NULL) return;
  if (gpr_unref(&creds->refcount)) {
    if (creds->vtable->destruct != NULL) creds->vtable->destruct(creds);
    if (creds->processor.destroy != NULL && creds->processor.state != NULL) {
      creds->processor.destroy(creds->processor.state);
    }
    gpr_free(creds);
  }
}

void grpc_server_credentials_release(grpc_server_credentials *creds) {
  GRPC_API_TRACE("grpc_server_credentials_release(creds=%p)", 1, (creds));
  grpc_server_credentials_unref(creds);
}

grpc_security_status grpc_server_credentials_create_security_connector(
    grpc_server_credentials *creds, grpc_server_security_connector **sc) {
  if (creds == NULL || creds->vtable->create_security_connector == NULL) {
    gpr_log(GPR_ERROR, "Server credentials cannot create security context.");
    return GRPC_SECURITY_ERROR;
  }
  return creds->vtable->create_security_connector(creds, sc);
}

void grpc_server_credentials_set_auth_metadata_processor(
    grpc_server_credentials *creds, grpc_auth_metadata_processor processor) {
  GRPC_API_TRACE(
      "grpc_server_credentials_set_auth_metadata_processor("
      "creds=%p, "
      "processor=grpc_auth_metadata_processor { process: %p, state: %p })",
      3, (creds, (void *)(intptr_t)processor.process, processor.state));
  if (creds == NULL) return;
  if (creds->processor.destroy != NULL && creds->processor.state != NULL) {
    creds->processor.destroy(creds->processor.state);
  }
  creds->processor = processor;
}

static void server_credentials_pointer_arg_destroy(void *p) {
  grpc_server_credentials_unref(p);
}

static void *server_credentials_pointer_arg_copy(void *p) {
  return grpc_server_credentials_ref(p);
}

static int server_credentials_pointer_cmp(void *a, void *b) {
  return GPR_ICMP(a, b);
}

static const grpc_arg_pointer_vtable cred_ptr_vtable = {
    server_credentials_pointer_arg_copy, server_credentials_pointer_arg_destroy,
    server_credentials_pointer_cmp};

grpc_arg grpc_server_credentials_to_arg(grpc_server_credentials *p) {
  grpc_arg arg;
  memset(&arg, 0, sizeof(grpc_arg));
  arg.type = GRPC_ARG_POINTER;
  arg.key = GRPC_SERVER_CREDENTIALS_ARG;
  arg.value.pointer.p = p;
  arg.value.pointer.vtable = &cred_ptr_vtable;
  return arg;
}

grpc_server_credentials *grpc_server_credentials_from_arg(const grpc_arg *arg) {
  if (strcmp(arg->key, GRPC_SERVER_CREDENTIALS_ARG) != 0) return NULL;
  if (arg->type != GRPC_ARG_POINTER) {
    gpr_log(GPR_ERROR, "Invalid type %d for arg %s", arg->type,
            GRPC_SERVER_CREDENTIALS_ARG);
    return NULL;
  }
  return arg->value.pointer.p;
}

grpc_server_credentials *grpc_find_server_credentials_in_args(
    const grpc_channel_args *args) {
  size_t i;
  if (args == NULL) return NULL;
  for (i = 0; i < args->num_args; i++) {
    grpc_server_credentials *p =
        grpc_server_credentials_from_arg(&args->args[i]);
    if (p != NULL) return p;
  }
  return NULL;
}

/* -- Ssl credentials. -- */

static void ssl_destruct(grpc_channel_credentials *creds) {
  grpc_ssl_credentials *c = (grpc_ssl_credentials *)creds;
  if (c->config.pem_root_certs != NULL) gpr_free(c->config.pem_root_certs);
  if (c->config.pem_private_key != NULL) gpr_free(c->config.pem_private_key);
  if (c->config.pem_cert_chain != NULL) gpr_free(c->config.pem_cert_chain);
}

static void ssl_server_destruct(grpc_server_credentials *creds) {
  grpc_ssl_server_credentials *c = (grpc_ssl_server_credentials *)creds;
  size_t i;
  for (i = 0; i < c->config.num_key_cert_pairs; i++) {
    if (c->config.pem_private_keys[i] != NULL) {
      gpr_free(c->config.pem_private_keys[i]);
    }
    if (c->config.pem_cert_chains[i] != NULL) {
      gpr_free(c->config.pem_cert_chains[i]);
    }
  }
  if (c->config.pem_private_keys != NULL) gpr_free(c->config.pem_private_keys);
  if (c->config.pem_private_keys_sizes != NULL) {
    gpr_free(c->config.pem_private_keys_sizes);
  }
  if (c->config.pem_cert_chains != NULL) gpr_free(c->config.pem_cert_chains);
  if (c->config.pem_cert_chains_sizes != NULL) {
    gpr_free(c->config.pem_cert_chains_sizes);
  }
  if (c->config.pem_root_certs != NULL) gpr_free(c->config.pem_root_certs);
}

static grpc_security_status ssl_create_security_connector(
    grpc_channel_credentials *creds, grpc_call_credentials *call_creds,
    const char *target, const grpc_channel_args *args,
    grpc_channel_security_connector **sc, grpc_channel_args **new_args) {
  grpc_ssl_credentials *c = (grpc_ssl_credentials *)creds;
  grpc_security_status status = GRPC_SECURITY_OK;
  size_t i = 0;
  const char *overridden_target_name = NULL;
  grpc_arg new_arg;

  for (i = 0; args && i < args->num_args; i++) {
    grpc_arg *arg = &args->args[i];
    if (strcmp(arg->key, GRPC_SSL_TARGET_NAME_OVERRIDE_ARG) == 0 &&
        arg->type == GRPC_ARG_STRING) {
      overridden_target_name = arg->value.string;
      break;
    }
  }
  status = grpc_ssl_channel_security_connector_create(
      call_creds, &c->config, target, overridden_target_name, sc);
  if (status != GRPC_SECURITY_OK) {
    return status;
  }
  new_arg.type = GRPC_ARG_STRING;
  new_arg.key = GRPC_ARG_HTTP2_SCHEME;
  new_arg.value.string = "https";
  *new_args = grpc_channel_args_copy_and_add(args, &new_arg, 1);
  return status;
}

static grpc_security_status ssl_server_create_security_connector(
    grpc_server_credentials *creds, grpc_server_security_connector **sc) {
  grpc_ssl_server_credentials *c = (grpc_ssl_server_credentials *)creds;
  return grpc_ssl_server_security_connector_create(&c->config, sc);
}

static grpc_channel_credentials_vtable ssl_vtable = {
    ssl_destruct, ssl_create_security_connector};

static grpc_server_credentials_vtable ssl_server_vtable = {
    ssl_server_destruct, ssl_server_create_security_connector};

static void ssl_copy_key_material(const char *input, unsigned char **output,
                                  size_t *output_size) {
  *output_size = strlen(input);
  *output = gpr_malloc(*output_size);
  memcpy(*output, input, *output_size);
}

static void ssl_build_config(const char *pem_root_certs,
                             grpc_ssl_pem_key_cert_pair *pem_key_cert_pair,
                             grpc_ssl_config *config) {
  if (pem_root_certs != NULL) {
    ssl_copy_key_material(pem_root_certs, &config->pem_root_certs,
                          &config->pem_root_certs_size);
  }
  if (pem_key_cert_pair != NULL) {
    GPR_ASSERT(pem_key_cert_pair->private_key != NULL);
    GPR_ASSERT(pem_key_cert_pair->cert_chain != NULL);
    ssl_copy_key_material(pem_key_cert_pair->private_key,
                          &config->pem_private_key,
                          &config->pem_private_key_size);
    ssl_copy_key_material(pem_key_cert_pair->cert_chain,
                          &config->pem_cert_chain,
                          &config->pem_cert_chain_size);
  }
}

static void ssl_build_server_config(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs,
    size_t num_key_cert_pairs,
    grpc_ssl_client_certificate_request_type client_certificate_request,
    grpc_ssl_server_config *config) {
  size_t i;
  config->client_certificate_request = client_certificate_request;
  if (pem_root_certs != NULL) {
    ssl_copy_key_material(pem_root_certs, &config->pem_root_certs,
                          &config->pem_root_certs_size);
  }
  if (num_key_cert_pairs > 0) {
    GPR_ASSERT(pem_key_cert_pairs != NULL);
    config->pem_private_keys =
        gpr_malloc(num_key_cert_pairs * sizeof(unsigned char *));
    config->pem_cert_chains =
        gpr_malloc(num_key_cert_pairs * sizeof(unsigned char *));
    config->pem_private_keys_sizes =
        gpr_malloc(num_key_cert_pairs * sizeof(size_t));
    config->pem_cert_chains_sizes =
        gpr_malloc(num_key_cert_pairs * sizeof(size_t));
  }
  config->num_key_cert_pairs = num_key_cert_pairs;
  for (i = 0; i < num_key_cert_pairs; i++) {
    GPR_ASSERT(pem_key_cert_pairs[i].private_key != NULL);
    GPR_ASSERT(pem_key_cert_pairs[i].cert_chain != NULL);
    ssl_copy_key_material(pem_key_cert_pairs[i].private_key,
                          &config->pem_private_keys[i],
                          &config->pem_private_keys_sizes[i]);
    ssl_copy_key_material(pem_key_cert_pairs[i].cert_chain,
                          &config->pem_cert_chains[i],
                          &config->pem_cert_chains_sizes[i]);
  }
}

grpc_channel_credentials *grpc_ssl_credentials_create(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pair,
    void *reserved) {
  grpc_ssl_credentials *c = gpr_malloc(sizeof(grpc_ssl_credentials));
  GRPC_API_TRACE(
      "grpc_ssl_credentials_create(pem_root_certs=%s, "
      "pem_key_cert_pair=%p, "
      "reserved=%p)",
      3, (pem_root_certs, pem_key_cert_pair, reserved));
  GPR_ASSERT(reserved == NULL);
  memset(c, 0, sizeof(grpc_ssl_credentials));
  c->base.type = GRPC_CHANNEL_CREDENTIALS_TYPE_SSL;
  c->base.vtable = &ssl_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  ssl_build_config(pem_root_certs, pem_key_cert_pair, &c->config);
  return &c->base;
}

grpc_server_credentials *grpc_ssl_server_credentials_create(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs,
    size_t num_key_cert_pairs, int force_client_auth, void *reserved) {
  return grpc_ssl_server_credentials_create_ex(
      pem_root_certs, pem_key_cert_pairs, num_key_cert_pairs,
      force_client_auth
          ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
          : GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
      reserved);
}

grpc_server_credentials *grpc_ssl_server_credentials_create_ex(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs,
    size_t num_key_cert_pairs,
    grpc_ssl_client_certificate_request_type client_certificate_request,
    void *reserved) {
  grpc_ssl_server_credentials *c =
      gpr_malloc(sizeof(grpc_ssl_server_credentials));
  GRPC_API_TRACE(
      "grpc_ssl_server_credentials_create_ex("
      "pem_root_certs=%s, pem_key_cert_pairs=%p, num_key_cert_pairs=%lu, "
      "client_certificate_request=%d, reserved=%p)",
      5, (pem_root_certs, pem_key_cert_pairs, (unsigned long)num_key_cert_pairs,
          client_certificate_request, reserved));
  GPR_ASSERT(reserved == NULL);
  memset(c, 0, sizeof(grpc_ssl_server_credentials));
  c->base.type = GRPC_CHANNEL_CREDENTIALS_TYPE_SSL;
  gpr_ref_init(&c->base.refcount, 1);
  c->base.vtable = &ssl_server_vtable;
  ssl_build_server_config(pem_root_certs, pem_key_cert_pairs,
                          num_key_cert_pairs, client_certificate_request,
                          &c->config);
  return &c->base;
}

/* -- Jwt credentials -- */

static void jwt_reset_cache(grpc_service_account_jwt_access_credentials *c) {
  if (c->cached.jwt_md != NULL) {
    grpc_credentials_md_store_unref(c->cached.jwt_md);
    c->cached.jwt_md = NULL;
  }
  if (c->cached.service_url != NULL) {
    gpr_free(c->cached.service_url);
    c->cached.service_url = NULL;
  }
  c->cached.jwt_expiration = gpr_inf_past(GPR_CLOCK_REALTIME);
}

static void jwt_destruct(grpc_call_credentials *creds) {
  grpc_service_account_jwt_access_credentials *c =
      (grpc_service_account_jwt_access_credentials *)creds;
  grpc_auth_json_key_destruct(&c->key);
  jwt_reset_cache(c);
  gpr_mu_destroy(&c->cache_mu);
}

static void jwt_get_request_metadata(grpc_exec_ctx *exec_ctx,
                                     grpc_call_credentials *creds,
                                     grpc_pollset *pollset,
                                     grpc_auth_metadata_context context,
                                     grpc_credentials_metadata_cb cb,
                                     void *user_data) {
  grpc_service_account_jwt_access_credentials *c =
      (grpc_service_account_jwt_access_credentials *)creds;
  gpr_timespec refresh_threshold = gpr_time_from_seconds(
      GRPC_SECURE_TOKEN_REFRESH_THRESHOLD_SECS, GPR_TIMESPAN);

  /* See if we can return a cached jwt. */
  grpc_credentials_md_store *jwt_md = NULL;
  {
    gpr_mu_lock(&c->cache_mu);
    if (c->cached.service_url != NULL &&
        strcmp(c->cached.service_url, context.service_url) == 0 &&
        c->cached.jwt_md != NULL &&
        (gpr_time_cmp(gpr_time_sub(c->cached.jwt_expiration,
                                   gpr_now(GPR_CLOCK_REALTIME)),
                      refresh_threshold) > 0)) {
      jwt_md = grpc_credentials_md_store_ref(c->cached.jwt_md);
    }
    gpr_mu_unlock(&c->cache_mu);
  }

  if (jwt_md == NULL) {
    char *jwt = NULL;
    /* Generate a new jwt. */
    gpr_mu_lock(&c->cache_mu);
    jwt_reset_cache(c);
    jwt = grpc_jwt_encode_and_sign(&c->key, context.service_url,
                                   c->jwt_lifetime, NULL);
    if (jwt != NULL) {
      char *md_value;
      gpr_asprintf(&md_value, "Bearer %s", jwt);
      gpr_free(jwt);
      c->cached.jwt_expiration =
          gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), c->jwt_lifetime);
      c->cached.service_url = gpr_strdup(context.service_url);
      c->cached.jwt_md = grpc_credentials_md_store_create(1);
      grpc_credentials_md_store_add_cstrings(
          c->cached.jwt_md, GRPC_AUTHORIZATION_METADATA_KEY, md_value);
      gpr_free(md_value);
      jwt_md = grpc_credentials_md_store_ref(c->cached.jwt_md);
    }
    gpr_mu_unlock(&c->cache_mu);
  }

  if (jwt_md != NULL) {
    cb(exec_ctx, user_data, jwt_md->entries, jwt_md->num_entries,
       GRPC_CREDENTIALS_OK);
    grpc_credentials_md_store_unref(jwt_md);
  } else {
    cb(exec_ctx, user_data, NULL, 0, GRPC_CREDENTIALS_ERROR);
  }
}

static grpc_call_credentials_vtable jwt_vtable = {jwt_destruct,
                                                  jwt_get_request_metadata};

grpc_call_credentials *
grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
    grpc_auth_json_key key, gpr_timespec token_lifetime) {
  grpc_service_account_jwt_access_credentials *c;
  if (!grpc_auth_json_key_is_valid(&key)) {
    gpr_log(GPR_ERROR, "Invalid input for jwt credentials creation");
    return NULL;
  }
  c = gpr_malloc(sizeof(grpc_service_account_jwt_access_credentials));
  memset(c, 0, sizeof(grpc_service_account_jwt_access_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_JWT;
  gpr_ref_init(&c->base.refcount, 1);
  c->base.vtable = &jwt_vtable;
  c->key = key;
  c->jwt_lifetime = token_lifetime;
  gpr_mu_init(&c->cache_mu);
  jwt_reset_cache(c);
  return &c->base;
}

grpc_call_credentials *grpc_service_account_jwt_access_credentials_create(
    const char *json_key, gpr_timespec token_lifetime, void *reserved) {
  GRPC_API_TRACE(
      "grpc_service_account_jwt_access_credentials_create("
      "json_key=%s, "
      "token_lifetime="
      "gpr_timespec { tv_sec: %lld, tv_nsec: %d, clock_type: %d }, "
      "reserved=%p)",
      5,
      (json_key, (long long)token_lifetime.tv_sec, (int)token_lifetime.tv_nsec,
       (int)token_lifetime.clock_type, reserved));
  GPR_ASSERT(reserved == NULL);
  return grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
      grpc_auth_json_key_create_from_string(json_key), token_lifetime);
}

/* -- Oauth2TokenFetcher credentials -- */

static void oauth2_token_fetcher_destruct(grpc_call_credentials *creds) {
  grpc_oauth2_token_fetcher_credentials *c =
      (grpc_oauth2_token_fetcher_credentials *)creds;
  grpc_credentials_md_store_unref(c->access_token_md);
  gpr_mu_destroy(&c->mu);
  grpc_httpcli_context_destroy(&c->httpcli_context);
}

grpc_credentials_status
grpc_oauth2_token_fetcher_credentials_parse_server_response(
    const grpc_http_response *response, grpc_credentials_md_store **token_md,
    gpr_timespec *token_lifetime) {
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
    if (*token_md != NULL) grpc_credentials_md_store_unref(*token_md);
    *token_md = grpc_credentials_md_store_create(1);
    grpc_credentials_md_store_add_cstrings(
        *token_md, GRPC_AUTHORIZATION_METADATA_KEY, new_access_token);
    status = GRPC_CREDENTIALS_OK;
  }

end:
  if (status != GRPC_CREDENTIALS_OK && (*token_md != NULL)) {
    grpc_credentials_md_store_unref(*token_md);
    *token_md = NULL;
  }
  if (null_terminated_body != NULL) gpr_free(null_terminated_body);
  if (new_access_token != NULL) gpr_free(new_access_token);
  if (json != NULL) grpc_json_destroy(json);
  return status;
}

static void on_oauth2_token_fetcher_http_response(
    grpc_exec_ctx *exec_ctx, void *user_data,
    const grpc_http_response *response) {
  grpc_credentials_metadata_request *r =
      (grpc_credentials_metadata_request *)user_data;
  grpc_oauth2_token_fetcher_credentials *c =
      (grpc_oauth2_token_fetcher_credentials *)r->creds;
  gpr_timespec token_lifetime;
  grpc_credentials_status status;

  gpr_mu_lock(&c->mu);
  status = grpc_oauth2_token_fetcher_credentials_parse_server_response(
      response, &c->access_token_md, &token_lifetime);
  if (status == GRPC_CREDENTIALS_OK) {
    c->token_expiration =
        gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), token_lifetime);
    r->cb(exec_ctx, r->user_data, c->access_token_md->entries,
          c->access_token_md->num_entries, status);
  } else {
    c->token_expiration = gpr_inf_past(GPR_CLOCK_REALTIME);
    r->cb(exec_ctx, r->user_data, NULL, 0, status);
  }
  gpr_mu_unlock(&c->mu);
  grpc_credentials_metadata_request_destroy(r);
}

static void oauth2_token_fetcher_get_request_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *creds,
    grpc_pollset *pollset, grpc_auth_metadata_context context,
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
       cached_access_token_md->num_entries, GRPC_CREDENTIALS_OK);
    grpc_credentials_md_store_unref(cached_access_token_md);
  } else {
    c->fetch_func(
        exec_ctx,
        grpc_credentials_metadata_request_create(creds, cb, user_data),
        &c->httpcli_context, pollset, on_oauth2_token_fetcher_http_response,
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

/* -- GoogleComputeEngine credentials. -- */

static grpc_call_credentials_vtable compute_engine_vtable = {
    oauth2_token_fetcher_destruct, oauth2_token_fetcher_get_request_metadata};

static void compute_engine_fetch_oauth2(
    grpc_exec_ctx *exec_ctx, grpc_credentials_metadata_request *metadata_req,
    grpc_httpcli_context *httpcli_context, grpc_pollset *pollset,
    grpc_httpcli_response_cb response_cb, gpr_timespec deadline) {
  grpc_http_header header = {"Metadata-Flavor", "Google"};
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = GRPC_COMPUTE_ENGINE_METADATA_HOST;
  request.http.path = GRPC_COMPUTE_ENGINE_METADATA_TOKEN_PATH;
  request.http.hdr_count = 1;
  request.http.hdrs = &header;
  grpc_httpcli_get(exec_ctx, httpcli_context, pollset, &request, deadline,
                   response_cb, metadata_req);
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

/* -- GoogleRefreshToken credentials. -- */

static void refresh_token_destruct(grpc_call_credentials *creds) {
  grpc_google_refresh_token_credentials *c =
      (grpc_google_refresh_token_credentials *)creds;
  grpc_auth_refresh_token_destruct(&c->refresh_token);
  oauth2_token_fetcher_destruct(&c->base.base);
}

static grpc_call_credentials_vtable refresh_token_vtable = {
    refresh_token_destruct, oauth2_token_fetcher_get_request_metadata};

static void refresh_token_fetch_oauth2(
    grpc_exec_ctx *exec_ctx, grpc_credentials_metadata_request *metadata_req,
    grpc_httpcli_context *httpcli_context, grpc_pollset *pollset,
    grpc_httpcli_response_cb response_cb, gpr_timespec deadline) {
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
  grpc_httpcli_post(exec_ctx, httpcli_context, pollset, &request, body,
                    strlen(body), deadline, response_cb, metadata_req);
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

grpc_call_credentials *grpc_google_refresh_token_credentials_create(
    const char *json_refresh_token, void *reserved) {
  GRPC_API_TRACE(
      "grpc_refresh_token_credentials_create(json_refresh_token=%s, "
      "reserved=%p)",
      2, (json_refresh_token, reserved));
  GPR_ASSERT(reserved == NULL);
  return grpc_refresh_token_credentials_create_from_auth_refresh_token(
      grpc_auth_refresh_token_create_from_string(json_refresh_token));
}

/* -- Metadata-only credentials. -- */

static void md_only_test_destruct(grpc_call_credentials *creds) {
  grpc_md_only_test_credentials *c = (grpc_md_only_test_credentials *)creds;
  grpc_credentials_md_store_unref(c->md_store);
}

static void on_simulated_token_fetch_done(grpc_exec_ctx *exec_ctx,
                                          void *user_data, bool success) {
  grpc_credentials_metadata_request *r =
      (grpc_credentials_metadata_request *)user_data;
  grpc_md_only_test_credentials *c = (grpc_md_only_test_credentials *)r->creds;
  r->cb(exec_ctx, r->user_data, c->md_store->entries, c->md_store->num_entries,
        GRPC_CREDENTIALS_OK);
  grpc_credentials_metadata_request_destroy(r);
}

static void md_only_test_get_request_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *creds,
    grpc_pollset *pollset, grpc_auth_metadata_context context,
    grpc_credentials_metadata_cb cb, void *user_data) {
  grpc_md_only_test_credentials *c = (grpc_md_only_test_credentials *)creds;

  if (c->is_async) {
    grpc_credentials_metadata_request *cb_arg =
        grpc_credentials_metadata_request_create(creds, cb, user_data);
    grpc_executor_enqueue(
        grpc_closure_create(on_simulated_token_fetch_done, cb_arg), true);
  } else {
    cb(exec_ctx, user_data, c->md_store->entries, 1, GRPC_CREDENTIALS_OK);
  }
}

static grpc_call_credentials_vtable md_only_test_vtable = {
    md_only_test_destruct, md_only_test_get_request_metadata};

grpc_call_credentials *grpc_md_only_test_credentials_create(
    const char *md_key, const char *md_value, int is_async) {
  grpc_md_only_test_credentials *c =
      gpr_malloc(sizeof(grpc_md_only_test_credentials));
  memset(c, 0, sizeof(grpc_md_only_test_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_OAUTH2;
  c->base.vtable = &md_only_test_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  c->md_store = grpc_credentials_md_store_create(1);
  grpc_credentials_md_store_add_cstrings(c->md_store, md_key, md_value);
  c->is_async = is_async;
  return &c->base;
}

/* -- Oauth2 Access Token credentials. -- */

static void access_token_destruct(grpc_call_credentials *creds) {
  grpc_access_token_credentials *c = (grpc_access_token_credentials *)creds;
  grpc_credentials_md_store_unref(c->access_token_md);
}

static void access_token_get_request_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *creds,
    grpc_pollset *pollset, grpc_auth_metadata_context context,
    grpc_credentials_metadata_cb cb, void *user_data) {
  grpc_access_token_credentials *c = (grpc_access_token_credentials *)creds;
  cb(exec_ctx, user_data, c->access_token_md->entries, 1, GRPC_CREDENTIALS_OK);
}

static grpc_call_credentials_vtable access_token_vtable = {
    access_token_destruct, access_token_get_request_metadata};

grpc_call_credentials *grpc_access_token_credentials_create(
    const char *access_token, void *reserved) {
  grpc_access_token_credentials *c =
      gpr_malloc(sizeof(grpc_access_token_credentials));
  char *token_md_value;
  GRPC_API_TRACE(
      "grpc_access_token_credentials_create(access_token=%s, "
      "reserved=%p)",
      2, (access_token, reserved));
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

/* -- Fake transport security credentials. -- */

static grpc_security_status fake_transport_security_create_security_connector(
    grpc_channel_credentials *c, grpc_call_credentials *call_creds,
    const char *target, const grpc_channel_args *args,
    grpc_channel_security_connector **sc, grpc_channel_args **new_args) {
  *sc = grpc_fake_channel_security_connector_create(call_creds);
  return GRPC_SECURITY_OK;
}

static grpc_security_status
fake_transport_security_server_create_security_connector(
    grpc_server_credentials *c, grpc_server_security_connector **sc) {
  *sc = grpc_fake_server_security_connector_create();
  return GRPC_SECURITY_OK;
}

static grpc_channel_credentials_vtable
    fake_transport_security_credentials_vtable = {
        NULL, fake_transport_security_create_security_connector};

static grpc_server_credentials_vtable
    fake_transport_security_server_credentials_vtable = {
        NULL, fake_transport_security_server_create_security_connector};

grpc_channel_credentials *grpc_fake_transport_security_credentials_create(
    void) {
  grpc_channel_credentials *c = gpr_malloc(sizeof(grpc_channel_credentials));
  memset(c, 0, sizeof(grpc_channel_credentials));
  c->type = GRPC_CHANNEL_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY;
  c->vtable = &fake_transport_security_credentials_vtable;
  gpr_ref_init(&c->refcount, 1);
  return c;
}

grpc_server_credentials *grpc_fake_transport_security_server_credentials_create(
    void) {
  grpc_server_credentials *c = gpr_malloc(sizeof(grpc_server_credentials));
  memset(c, 0, sizeof(grpc_server_credentials));
  c->type = GRPC_CHANNEL_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY;
  gpr_ref_init(&c->refcount, 1);
  c->vtable = &fake_transport_security_server_credentials_vtable;
  return c;
}

/* -- Composite call credentials. -- */

typedef struct {
  grpc_composite_call_credentials *composite_creds;
  size_t creds_index;
  grpc_credentials_md_store *md_elems;
  grpc_auth_metadata_context auth_md_context;
  void *user_data;
  grpc_pollset *pollset;
  grpc_credentials_metadata_cb cb;
} grpc_composite_call_credentials_metadata_context;

static void composite_call_destruct(grpc_call_credentials *creds) {
  grpc_composite_call_credentials *c = (grpc_composite_call_credentials *)creds;
  size_t i;
  for (i = 0; i < c->inner.num_creds; i++) {
    grpc_call_credentials_unref(c->inner.creds_array[i]);
  }
  gpr_free(c->inner.creds_array);
}

static void composite_call_md_context_destroy(
    grpc_composite_call_credentials_metadata_context *ctx) {
  grpc_credentials_md_store_unref(ctx->md_elems);
  gpr_free(ctx);
}

static void composite_call_metadata_cb(grpc_exec_ctx *exec_ctx, void *user_data,
                                       grpc_credentials_md *md_elems,
                                       size_t num_md,
                                       grpc_credentials_status status) {
  grpc_composite_call_credentials_metadata_context *ctx =
      (grpc_composite_call_credentials_metadata_context *)user_data;
  if (status != GRPC_CREDENTIALS_OK) {
    ctx->cb(exec_ctx, ctx->user_data, NULL, 0, status);
    return;
  }

  /* Copy the metadata in the context. */
  if (num_md > 0) {
    size_t i;
    for (i = 0; i < num_md; i++) {
      grpc_credentials_md_store_add(ctx->md_elems, md_elems[i].key,
                                    md_elems[i].value);
    }
  }

  /* See if we need to get some more metadata. */
  if (ctx->creds_index < ctx->composite_creds->inner.num_creds) {
    grpc_call_credentials *inner_creds =
        ctx->composite_creds->inner.creds_array[ctx->creds_index++];
    grpc_call_credentials_get_request_metadata(
        exec_ctx, inner_creds, ctx->pollset, ctx->auth_md_context,
        composite_call_metadata_cb, ctx);
    return;
  }

  /* We're done!. */
  ctx->cb(exec_ctx, ctx->user_data, ctx->md_elems->entries,
          ctx->md_elems->num_entries, GRPC_CREDENTIALS_OK);
  composite_call_md_context_destroy(ctx);
}

static void composite_call_get_request_metadata(
    grpc_exec_ctx *exec_ctx, grpc_call_credentials *creds,
    grpc_pollset *pollset, grpc_auth_metadata_context auth_md_context,
    grpc_credentials_metadata_cb cb, void *user_data) {
  grpc_composite_call_credentials *c = (grpc_composite_call_credentials *)creds;
  grpc_composite_call_credentials_metadata_context *ctx;

  ctx = gpr_malloc(sizeof(grpc_composite_call_credentials_metadata_context));
  memset(ctx, 0, sizeof(grpc_composite_call_credentials_metadata_context));
  ctx->auth_md_context = auth_md_context;
  ctx->user_data = user_data;
  ctx->cb = cb;
  ctx->composite_creds = c;
  ctx->pollset = pollset;
  ctx->md_elems = grpc_credentials_md_store_create(c->inner.num_creds);
  grpc_call_credentials_get_request_metadata(
      exec_ctx, c->inner.creds_array[ctx->creds_index++], pollset,
      auth_md_context, composite_call_metadata_cb, ctx);
}

static grpc_call_credentials_vtable composite_call_credentials_vtable = {
    composite_call_destruct, composite_call_get_request_metadata};

static grpc_call_credentials_array get_creds_array(
    grpc_call_credentials **creds_addr) {
  grpc_call_credentials_array result;
  grpc_call_credentials *creds = *creds_addr;
  result.creds_array = creds_addr;
  result.num_creds = 1;
  if (strcmp(creds->type, GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0) {
    result = *grpc_composite_call_credentials_get_credentials(creds);
  }
  return result;
}

grpc_call_credentials *grpc_composite_call_credentials_create(
    grpc_call_credentials *creds1, grpc_call_credentials *creds2,
    void *reserved) {
  size_t i;
  size_t creds_array_byte_size;
  grpc_call_credentials_array creds1_array;
  grpc_call_credentials_array creds2_array;
  grpc_composite_call_credentials *c;
  GRPC_API_TRACE(
      "grpc_composite_call_credentials_create(creds1=%p, creds2=%p, "
      "reserved=%p)",
      3, (creds1, creds2, reserved));
  GPR_ASSERT(reserved == NULL);
  GPR_ASSERT(creds1 != NULL);
  GPR_ASSERT(creds2 != NULL);
  c = gpr_malloc(sizeof(grpc_composite_call_credentials));
  memset(c, 0, sizeof(grpc_composite_call_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE;
  c->base.vtable = &composite_call_credentials_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  creds1_array = get_creds_array(&creds1);
  creds2_array = get_creds_array(&creds2);
  c->inner.num_creds = creds1_array.num_creds + creds2_array.num_creds;
  creds_array_byte_size = c->inner.num_creds * sizeof(grpc_call_credentials *);
  c->inner.creds_array = gpr_malloc(creds_array_byte_size);
  memset(c->inner.creds_array, 0, creds_array_byte_size);
  for (i = 0; i < creds1_array.num_creds; i++) {
    grpc_call_credentials *cur_creds = creds1_array.creds_array[i];
    c->inner.creds_array[i] = grpc_call_credentials_ref(cur_creds);
  }
  for (i = 0; i < creds2_array.num_creds; i++) {
    grpc_call_credentials *cur_creds = creds2_array.creds_array[i];
    c->inner.creds_array[i + creds1_array.num_creds] =
        grpc_call_credentials_ref(cur_creds);
  }
  return &c->base;
}

const grpc_call_credentials_array *
grpc_composite_call_credentials_get_credentials(grpc_call_credentials *creds) {
  const grpc_composite_call_credentials *c =
      (const grpc_composite_call_credentials *)creds;
  GPR_ASSERT(strcmp(creds->type, GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0);
  return &c->inner;
}

grpc_call_credentials *grpc_credentials_contains_type(
    grpc_call_credentials *creds, const char *type,
    grpc_call_credentials **composite_creds) {
  size_t i;
  if (strcmp(creds->type, type) == 0) {
    if (composite_creds != NULL) *composite_creds = NULL;
    return creds;
  } else if (strcmp(creds->type, GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0) {
    const grpc_call_credentials_array *inner_creds_array =
        grpc_composite_call_credentials_get_credentials(creds);
    for (i = 0; i < inner_creds_array->num_creds; i++) {
      if (strcmp(type, inner_creds_array->creds_array[i]->type) == 0) {
        if (composite_creds != NULL) *composite_creds = creds;
        return inner_creds_array->creds_array[i];
      }
    }
  }
  return NULL;
}

/* -- IAM credentials. -- */

static void iam_destruct(grpc_call_credentials *creds) {
  grpc_google_iam_credentials *c = (grpc_google_iam_credentials *)creds;
  grpc_credentials_md_store_unref(c->iam_md);
}

static void iam_get_request_metadata(grpc_exec_ctx *exec_ctx,
                                     grpc_call_credentials *creds,
                                     grpc_pollset *pollset,
                                     grpc_auth_metadata_context context,
                                     grpc_credentials_metadata_cb cb,
                                     void *user_data) {
  grpc_google_iam_credentials *c = (grpc_google_iam_credentials *)creds;
  cb(exec_ctx, user_data, c->iam_md->entries, c->iam_md->num_entries,
     GRPC_CREDENTIALS_OK);
}

static grpc_call_credentials_vtable iam_vtable = {iam_destruct,
                                                  iam_get_request_metadata};

grpc_call_credentials *grpc_google_iam_credentials_create(
    const char *token, const char *authority_selector, void *reserved) {
  grpc_google_iam_credentials *c;
  GRPC_API_TRACE(
      "grpc_iam_credentials_create(token=%s, authority_selector=%s, "
      "reserved=%p)",
      3, (token, authority_selector, reserved));
  GPR_ASSERT(reserved == NULL);
  GPR_ASSERT(token != NULL);
  GPR_ASSERT(authority_selector != NULL);
  c = gpr_malloc(sizeof(grpc_google_iam_credentials));
  memset(c, 0, sizeof(grpc_google_iam_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_IAM;
  c->base.vtable = &iam_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  c->iam_md = grpc_credentials_md_store_create(2);
  grpc_credentials_md_store_add_cstrings(
      c->iam_md, GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY, token);
  grpc_credentials_md_store_add_cstrings(
      c->iam_md, GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY, authority_selector);
  return &c->base;
}

/* -- Plugin credentials. -- */

typedef struct {
  void *user_data;
  grpc_credentials_metadata_cb cb;
} grpc_metadata_plugin_request;

static void plugin_destruct(grpc_call_credentials *creds) {
  grpc_plugin_credentials *c = (grpc_plugin_credentials *)creds;
  if (c->plugin.state != NULL && c->plugin.destroy != NULL) {
    c->plugin.destroy(c->plugin.state);
  }
}

static void plugin_md_request_metadata_ready(void *request,
                                             const grpc_metadata *md,
                                             size_t num_md,
                                             grpc_status_code status,
                                             const char *error_details) {
  /* called from application code */
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_metadata_plugin_request *r = (grpc_metadata_plugin_request *)request;
  if (status != GRPC_STATUS_OK) {
    if (error_details != NULL) {
      gpr_log(GPR_ERROR, "Getting metadata from plugin failed with error: %s",
              error_details);
    }
    r->cb(&exec_ctx, r->user_data, NULL, 0, GRPC_CREDENTIALS_ERROR);
  } else {
    size_t i;
    grpc_credentials_md *md_array = NULL;
    if (num_md > 0) {
      md_array = gpr_malloc(num_md * sizeof(grpc_credentials_md));
      for (i = 0; i < num_md; i++) {
        md_array[i].key = gpr_slice_from_copied_string(md[i].key);
        md_array[i].value =
            gpr_slice_from_copied_buffer(md[i].value, md[i].value_length);
      }
    }
    r->cb(&exec_ctx, r->user_data, md_array, num_md, GRPC_CREDENTIALS_OK);
    if (md_array != NULL) {
      for (i = 0; i < num_md; i++) {
        gpr_slice_unref(md_array[i].key);
        gpr_slice_unref(md_array[i].value);
      }
      gpr_free(md_array);
    }
  }
  gpr_free(r);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void plugin_get_request_metadata(grpc_exec_ctx *exec_ctx,
                                        grpc_call_credentials *creds,
                                        grpc_pollset *pollset,
                                        grpc_auth_metadata_context context,
                                        grpc_credentials_metadata_cb cb,
                                        void *user_data) {
  grpc_plugin_credentials *c = (grpc_plugin_credentials *)creds;
  if (c->plugin.get_metadata != NULL) {
    grpc_metadata_plugin_request *request = gpr_malloc(sizeof(*request));
    memset(request, 0, sizeof(*request));
    request->user_data = user_data;
    request->cb = cb;
    c->plugin.get_metadata(c->plugin.state, context,
                           plugin_md_request_metadata_ready, request);
  } else {
    cb(exec_ctx, user_data, NULL, 0, GRPC_CREDENTIALS_OK);
  }
}

static grpc_call_credentials_vtable plugin_vtable = {
    plugin_destruct, plugin_get_request_metadata};

grpc_call_credentials *grpc_metadata_credentials_create_from_plugin(
    grpc_metadata_credentials_plugin plugin, void *reserved) {
  grpc_plugin_credentials *c = gpr_malloc(sizeof(*c));
  GRPC_API_TRACE("grpc_metadata_credentials_create_from_plugin(reserved=%p)", 1,
                 (reserved));
  GPR_ASSERT(reserved == NULL);
  memset(c, 0, sizeof(*c));
  c->base.type = plugin.type;
  c->base.vtable = &plugin_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  c->plugin = plugin;
  return &c->base;
}

/* -- Composite channel credentials. -- */

static void composite_channel_destruct(grpc_channel_credentials *creds) {
  grpc_composite_channel_credentials *c =
      (grpc_composite_channel_credentials *)creds;
  grpc_channel_credentials_unref(c->inner_creds);
  grpc_call_credentials_unref(c->call_creds);
}

static grpc_security_status composite_channel_create_security_connector(
    grpc_channel_credentials *creds, grpc_call_credentials *call_creds,
    const char *target, const grpc_channel_args *args,
    grpc_channel_security_connector **sc, grpc_channel_args **new_args) {
  grpc_composite_channel_credentials *c =
      (grpc_composite_channel_credentials *)creds;
  grpc_security_status status = GRPC_SECURITY_ERROR;

  GPR_ASSERT(c->inner_creds != NULL && c->call_creds != NULL &&
             c->inner_creds->vtable != NULL &&
             c->inner_creds->vtable->create_security_connector != NULL);
  /* If we are passed a call_creds, create a call composite to pass it
     downstream. */
  if (call_creds != NULL) {
    grpc_call_credentials *composite_call_creds =
        grpc_composite_call_credentials_create(c->call_creds, call_creds, NULL);
    status = c->inner_creds->vtable->create_security_connector(
        c->inner_creds, composite_call_creds, target, args, sc, new_args);
    grpc_call_credentials_unref(composite_call_creds);
  } else {
    status = c->inner_creds->vtable->create_security_connector(
        c->inner_creds, c->call_creds, target, args, sc, new_args);
  }
  return status;
}

static grpc_channel_credentials_vtable composite_channel_credentials_vtable = {
    composite_channel_destruct, composite_channel_create_security_connector};

grpc_channel_credentials *grpc_composite_channel_credentials_create(
    grpc_channel_credentials *channel_creds, grpc_call_credentials *call_creds,
    void *reserved) {
  grpc_composite_channel_credentials *c = gpr_malloc(sizeof(*c));
  memset(c, 0, sizeof(*c));
  GPR_ASSERT(channel_creds != NULL && call_creds != NULL && reserved == NULL);
  GRPC_API_TRACE(
      "grpc_composite_channel_credentials_create(channel_creds=%p, "
      "call_creds=%p, reserved=%p)",
      3, (channel_creds, call_creds, reserved));
  c->base.type = channel_creds->type;
  c->base.vtable = &composite_channel_credentials_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  c->inner_creds = grpc_channel_credentials_ref(channel_creds);
  c->call_creds = grpc_call_credentials_ref(call_creds);
  return &c->base;
}
