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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"
#include "src/core/lib/security/credentials/google_default/google_default_credentials.h"
#include "src/core/lib/security/credentials/jwt/jwt_credentials.h"
#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/api_trace.h"

/* -- Constants. -- */

#define GRPC_COMPUTE_ENGINE_DETECTION_HOST "metadata.google.internal"

/* -- Default credentials. -- */

static grpc_channel_credentials* default_credentials = nullptr;
static int compute_engine_detection_done = 0;
static gpr_mu g_state_mu;
static gpr_once g_once = GPR_ONCE_INIT;

static void init_default_credentials(void) { gpr_mu_init(&g_state_mu); }

typedef struct {
  grpc_polling_entity pollent;
  int is_done;
  int success;
  grpc_http_response response;
} compute_engine_detector;

/* Takes ownership of creds_path if not NULL. */
static grpc_error* create_default_creds_from_path(
    char* creds_path, grpc_call_credentials** creds) {
  grpc_json* json = nullptr;
  grpc_auth_json_key key;
  grpc_auth_refresh_token token;
  grpc_call_credentials* result = nullptr;
  grpc_slice creds_data = grpc_empty_slice();
  grpc_error* error = GRPC_ERROR_NONE;
  if (creds_path == nullptr) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("creds_path unset");
    goto end;
  }
  error = grpc_load_file(creds_path, 0, &creds_data);
  if (error != GRPC_ERROR_NONE) {
    goto end;
  }
  json = grpc_json_parse_string_with_len(
      reinterpret_cast<char*> GRPC_SLICE_START_PTR(creds_data),
      GRPC_SLICE_LENGTH(creds_data));
  if (json == nullptr) {
    error = grpc_error_set_str(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed to parse JSON"),
        GRPC_ERROR_STR_RAW_BYTES, grpc_slice_ref_internal(creds_data));
    goto end;
  }

  /* First, try an auth json key. */
  key = grpc_auth_json_key_create_from_json(json);
  if (grpc_auth_json_key_is_valid(&key)) {
    result =
        grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
            key, grpc_max_auth_token_lifetime());
    if (result == nullptr) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "grpc_service_account_jwt_access_credentials_create_from_auth_json_"
          "key failed");
    }
    goto end;
  }

  /* Then try a refresh token if the auth json key was invalid. */
  token = grpc_auth_refresh_token_create_from_json(json);
  if (grpc_auth_refresh_token_is_valid(&token)) {
    result =
        grpc_refresh_token_credentials_create_from_auth_refresh_token(token);
    if (result == nullptr) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "grpc_refresh_token_credentials_create_from_auth_refresh_token "
          "failed");
    }
    goto end;
  }

end:
  GPR_ASSERT((result == nullptr) + (error == GRPC_ERROR_NONE) == 1);
  if (creds_path != nullptr) gpr_free(creds_path);
  grpc_slice_unref_internal(creds_data);
  if (json != nullptr) grpc_json_destroy(json);
  *creds = result;
  return error;
}

grpc_channel_credentials* grpc_google_default_credentials_create(void) {
  grpc_channel_credentials* result = nullptr;
  grpc_call_credentials* call_creds = nullptr;
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "Failed to create Google credentials");
  grpc_error* err;
  grpc_core::ExecCtx exec_ctx;

  GRPC_API_TRACE("grpc_google_default_credentials_create(void)", 0, ());

  gpr_once_init(&g_once, init_default_credentials);

  gpr_mu_lock(&g_state_mu);

  if (default_credentials != nullptr) {
    result = grpc_channel_credentials_ref(default_credentials);
    goto end;
  }

  /* First, try the environment variable. */
  err = create_default_creds_from_path(
      gpr_getenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR), &call_creds);
  if (err == GRPC_ERROR_NONE) goto end;
  error = grpc_error_add_child(error, err);

  /* Then the well-known file. */
  err = create_default_creds_from_path(
      grpc_get_well_known_google_credentials_file_path(), &call_creds);
  if (err == GRPC_ERROR_NONE) goto end;
  error = grpc_error_add_child(error, err);

  /* At last try to see if we're on compute engine (do the detection only once
     since it requires a network test). */
  if (!compute_engine_detection_done) {
    int need_compute_engine_creds = grpc_alts_is_running_on_gcp();
    compute_engine_detection_done = 1;
    if (need_compute_engine_creds) {
      call_creds = grpc_google_compute_engine_credentials_create(nullptr);
      if (call_creds == nullptr) {
        error = grpc_error_add_child(
            error, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                       "Failed to get credentials from network"));
      }
    }
  }

end:
  if (result == nullptr) {
    if (call_creds != nullptr) {
      /* Blend with default ssl credentials and add a global reference so that
         it
         can be cached and re-served. */
      grpc_channel_credentials* ssl_creds =
          grpc_ssl_credentials_create(nullptr, nullptr, nullptr);
      default_credentials = grpc_channel_credentials_ref(
          grpc_composite_channel_credentials_create(ssl_creds, call_creds,
                                                    nullptr));
      GPR_ASSERT(default_credentials != nullptr);
      grpc_channel_credentials_unref(ssl_creds);
      grpc_call_credentials_unref(call_creds);
      result = default_credentials;
    } else {
      gpr_log(GPR_ERROR, "Could not create google default credentials.");
    }
  }
  gpr_mu_unlock(&g_state_mu);
  if (result == nullptr) {
    GRPC_LOG_IF_ERROR("grpc_google_default_credentials_create", error);
  } else {
    GRPC_ERROR_UNREF(error);
  }

  return result;
}

void grpc_flush_cached_google_default_credentials(void) {
  grpc_core::ExecCtx exec_ctx;
  gpr_once_init(&g_once, init_default_credentials);
  gpr_mu_lock(&g_state_mu);
  if (default_credentials != nullptr) {
    grpc_channel_credentials_unref(default_credentials);
    default_credentials = nullptr;
  }
  compute_engine_detection_done = 0;
  gpr_mu_unlock(&g_state_mu);
}

/* -- Well known credentials path. -- */

static grpc_well_known_credentials_path_getter creds_path_getter = nullptr;

char* grpc_get_well_known_google_credentials_file_path(void) {
  if (creds_path_getter != nullptr) return creds_path_getter();
  return grpc_get_well_known_google_credentials_file_path_impl();
}

void grpc_override_well_known_credentials_path_getter(
    grpc_well_known_credentials_path_getter getter) {
  creds_path_getter = getter;
}
