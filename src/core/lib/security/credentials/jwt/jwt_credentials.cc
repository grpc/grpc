/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/security/credentials/jwt/jwt_credentials.h"

#include <inttypes.h>
#include <string.h>

#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

static void jwt_reset_cache(grpc_exec_ctx* exec_ctx,
                            grpc_service_account_jwt_access_credentials* c) {
  GRPC_MDELEM_UNREF(exec_ctx, c->cached.jwt_md);
  c->cached.jwt_md = GRPC_MDNULL;
  if (c->cached.service_url != nullptr) {
    gpr_free(c->cached.service_url);
    c->cached.service_url = nullptr;
  }
  c->cached.jwt_expiration = gpr_inf_past(GPR_CLOCK_REALTIME);
}

static void jwt_destruct(grpc_exec_ctx* exec_ctx,
                         grpc_call_credentials* creds) {
  grpc_service_account_jwt_access_credentials* c =
      (grpc_service_account_jwt_access_credentials*)creds;
  grpc_auth_json_key_destruct(&c->key);
  jwt_reset_cache(exec_ctx, c);
  gpr_mu_destroy(&c->cache_mu);
}

static bool jwt_get_request_metadata(grpc_exec_ctx* exec_ctx,
                                     grpc_call_credentials* creds,
                                     grpc_polling_entity* pollent,
                                     grpc_auth_metadata_context context,
                                     grpc_credentials_mdelem_array* md_array,
                                     grpc_closure* on_request_metadata,
                                     grpc_error** error) {
  grpc_service_account_jwt_access_credentials* c =
      (grpc_service_account_jwt_access_credentials*)creds;
  gpr_timespec refresh_threshold = gpr_time_from_seconds(
      GRPC_SECURE_TOKEN_REFRESH_THRESHOLD_SECS, GPR_TIMESPAN);

  /* See if we can return a cached jwt. */
  grpc_mdelem jwt_md = GRPC_MDNULL;
  {
    gpr_mu_lock(&c->cache_mu);
    if (c->cached.service_url != nullptr &&
        strcmp(c->cached.service_url, context.service_url) == 0 &&
        !GRPC_MDISNULL(c->cached.jwt_md) &&
        (gpr_time_cmp(gpr_time_sub(c->cached.jwt_expiration,
                                   gpr_now(GPR_CLOCK_REALTIME)),
                      refresh_threshold) > 0)) {
      jwt_md = GRPC_MDELEM_REF(c->cached.jwt_md);
    }
    gpr_mu_unlock(&c->cache_mu);
  }

  if (GRPC_MDISNULL(jwt_md)) {
    char* jwt = nullptr;
    /* Generate a new jwt. */
    gpr_mu_lock(&c->cache_mu);
    jwt_reset_cache(exec_ctx, c);
    jwt = grpc_jwt_encode_and_sign(&c->key, context.service_url,
                                   c->jwt_lifetime, nullptr);
    if (jwt != nullptr) {
      char* md_value;
      gpr_asprintf(&md_value, "Bearer %s", jwt);
      gpr_free(jwt);
      c->cached.jwt_expiration =
          gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), c->jwt_lifetime);
      c->cached.service_url = gpr_strdup(context.service_url);
      c->cached.jwt_md = grpc_mdelem_from_slices(
          exec_ctx,
          grpc_slice_from_static_string(GRPC_AUTHORIZATION_METADATA_KEY),
          grpc_slice_from_copied_string(md_value));
      gpr_free(md_value);
      jwt_md = GRPC_MDELEM_REF(c->cached.jwt_md);
    }
    gpr_mu_unlock(&c->cache_mu);
  }

  if (!GRPC_MDISNULL(jwt_md)) {
    grpc_credentials_mdelem_array_add(md_array, jwt_md);
    GRPC_MDELEM_UNREF(exec_ctx, jwt_md);
  } else {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Could not generate JWT.");
  }
  return true;
}

static void jwt_cancel_get_request_metadata(
    grpc_exec_ctx* exec_ctx, grpc_call_credentials* c,
    grpc_credentials_mdelem_array* md_array, grpc_error* error) {
  GRPC_ERROR_UNREF(error);
}

static grpc_call_credentials_vtable jwt_vtable = {
    jwt_destruct, jwt_get_request_metadata, jwt_cancel_get_request_metadata};

grpc_call_credentials*
grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
    grpc_exec_ctx* exec_ctx, grpc_auth_json_key key,
    gpr_timespec token_lifetime) {
  grpc_service_account_jwt_access_credentials* c;
  if (!grpc_auth_json_key_is_valid(&key)) {
    gpr_log(GPR_ERROR, "Invalid input for jwt credentials creation");
    return nullptr;
  }
  c = (grpc_service_account_jwt_access_credentials*)gpr_zalloc(
      sizeof(grpc_service_account_jwt_access_credentials));
  c->base.type = GRPC_CALL_CREDENTIALS_TYPE_JWT;
  gpr_ref_init(&c->base.refcount, 1);
  c->base.vtable = &jwt_vtable;
  c->key = key;
  gpr_timespec max_token_lifetime = grpc_max_auth_token_lifetime();
  if (gpr_time_cmp(token_lifetime, max_token_lifetime) > 0) {
    gpr_log(GPR_INFO,
            "Cropping token lifetime to maximum allowed value (%d secs).",
            (int)max_token_lifetime.tv_sec);
    token_lifetime = grpc_max_auth_token_lifetime();
  }
  c->jwt_lifetime = token_lifetime;
  gpr_mu_init(&c->cache_mu);
  jwt_reset_cache(exec_ctx, c);
  return &c->base;
}

static char* redact_private_key(const char* json_key) {
  char* json_copy = gpr_strdup(json_key);
  grpc_json* json = grpc_json_parse_string(json_copy);
  if (!json) {
    gpr_free(json_copy);
    return gpr_strdup("<Json failed to parse.>");
  }
  const char* redacted = "<redacted>";
  grpc_json* current = json->child;
  while (current) {
    if (current->type == GRPC_JSON_STRING &&
        strcmp(current->key, "private_key") == 0) {
      current->value = (char*)redacted;
      break;
    }
    current = current->next;
  }
  char* clean_json = grpc_json_dump_to_string(json, 2);
  gpr_free(json_copy);
  grpc_json_destroy(json);
  return clean_json;
}

grpc_call_credentials* grpc_service_account_jwt_access_credentials_create(
    const char* json_key, gpr_timespec token_lifetime, void* reserved) {
  if (grpc_api_trace.enabled()) {
    char* clean_json = redact_private_key(json_key);
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
  GPR_ASSERT(reserved == nullptr);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_call_credentials* creds =
      grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
          &exec_ctx, grpc_auth_json_key_create_from_string(json_key),
          token_lifetime);
  grpc_exec_ctx_finish(&exec_ctx);
  return creds;
}
