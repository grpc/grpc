/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/security/credentials/jwt/jwt_credentials.h"

#include <string.h>

#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

static void jwt_reset_cache(grpc_exec_ctx *exec_ctx,
                            grpc_service_account_jwt_access_credentials *c) {
  if (c->cached.jwt_md != NULL) {
    grpc_credentials_md_store_unref(exec_ctx, c->cached.jwt_md);
    c->cached.jwt_md = NULL;
  }
  if (c->cached.service_url != NULL) {
    gpr_free(c->cached.service_url);
    c->cached.service_url = NULL;
  }
  c->cached.jwt_expiration = gpr_inf_past(GPR_CLOCK_REALTIME);
}

static void jwt_destruct(grpc_exec_ctx *exec_ctx,
                         grpc_call_credentials *creds) {
  grpc_service_account_jwt_access_credentials *c =
      (grpc_service_account_jwt_access_credentials *)creds;
  grpc_auth_json_key_destruct(&c->key);
  jwt_reset_cache(exec_ctx, c);
  gpr_mu_destroy(&c->cache_mu);
}

static void jwt_get_request_metadata(grpc_exec_ctx *exec_ctx,
                                     grpc_call_credentials *creds,
                                     grpc_polling_entity *pollent,
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
    jwt_reset_cache(exec_ctx, c);
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
       GRPC_CREDENTIALS_OK, NULL);
    grpc_credentials_md_store_unref(exec_ctx, jwt_md);
  } else {
    cb(exec_ctx, user_data, NULL, 0, GRPC_CREDENTIALS_ERROR,
       "Could not generate JWT.");
  }
}

static grpc_call_credentials_vtable jwt_vtable = {jwt_destruct,
                                                  jwt_get_request_metadata};

grpc_call_credentials *
grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
    grpc_exec_ctx *exec_ctx, grpc_auth_json_key key,
    gpr_timespec token_lifetime) {
  grpc_service_account_jwt_access_credentials *c;
  if (!grpc_auth_json_key_is_valid(&key)) {
    gpr_log(GPR_ERROR, "Invalid input for jwt credentials creation");
    return NULL;
  }
  c = gpr_zalloc(sizeof(grpc_service_account_jwt_access_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_JWT;
  gpr_ref_init(&c->base.refcount, 1);
  c->base.vtable = &jwt_vtable;
  c->key = key;
  c->jwt_lifetime = token_lifetime;
  gpr_mu_init(&c->cache_mu);
  jwt_reset_cache(exec_ctx, c);
  return &c->base;
}

static char *redact_private_key(const char *json_key) {
  char *json_copy = gpr_strdup(json_key);
  grpc_json *json = grpc_json_parse_string(json_copy);
  if (!json) {
    gpr_free(json_copy);
    return gpr_strdup("<Json failed to parse.>");
  }
  const char *redacted = "<redacted>";
  grpc_json *current = json->child;
  while (current) {
    if (current->type == GRPC_JSON_STRING &&
        strcmp(current->key, "private_key") == 0) {
      current->value = (char *)redacted;
      break;
    }
    current = current->next;
  }
  char *clean_json = grpc_json_dump_to_string(json, 2);
  gpr_free(json_copy);
  grpc_json_destroy(json);
  return clean_json;
}

grpc_call_credentials *grpc_service_account_jwt_access_credentials_create(
    const char *json_key, gpr_timespec token_lifetime, void *reserved) {
  if (GRPC_TRACER_ON(grpc_api_trace)) {
    char *clean_json = redact_private_key(json_key);
    gpr_log(GPR_INFO,
            "grpc_service_account_jwt_access_credentials_create("
            "json_key=%s, "
            "token_lifetime="
            "gpr_timespec { tv_sec: %" PRId64
            ", tv_nsec: %d, clock_type: %d }, "
            "reserved=%p)",
            clean_json, token_lifetime.tv_sec, token_lifetime.tv_nsec,
            (int)token_lifetime.clock_type, reserved);
    gpr_free(clean_json);
  }
  GPR_ASSERT(reserved == NULL);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_call_credentials *creds =
      grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
          &exec_ctx, grpc_auth_json_key_create_from_string(json_key),
          token_lifetime);
  grpc_exec_ctx_finish(&exec_ctx);
  return creds;
}
