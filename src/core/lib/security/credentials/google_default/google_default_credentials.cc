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
static gpr_mu* g_polling_mu;
static gpr_once g_once = GPR_ONCE_INIT;

static void init_default_credentials(void) { gpr_mu_init(&g_state_mu); }

typedef struct {
  grpc_polling_entity pollent;
  int is_done;
  int success;
  grpc_http_response response;
} compute_engine_detector;

static void on_compute_engine_detection_http_response(void* user_data,
                                                      grpc_error* error) {
  compute_engine_detector* detector =
      static_cast<compute_engine_detector*>(user_data);
  if (error == GRPC_ERROR_NONE && detector->response.status == 200 &&
      detector->response.hdr_count > 0) {
    /* Internet providers can return a generic response to all requests, so
       it is necessary to check that metadata header is present also. */
    size_t i;
    for (i = 0; i < detector->response.hdr_count; i++) {
      grpc_http_header* header = &detector->response.hdrs[i];
      if (strcmp(header->key, "Metadata-Flavor") == 0 &&
          strcmp(header->value, "Google") == 0) {
        detector->success = 1;
        break;
      }
    }
  }
  gpr_mu_lock(g_polling_mu);
  detector->is_done = 1;
  GRPC_LOG_IF_ERROR(
      "Pollset kick",
      grpc_pollset_kick(grpc_polling_entity_pollset(&detector->pollent),
                        nullptr));
  gpr_mu_unlock(g_polling_mu);
}

static void destroy_pollset(void* p, grpc_error* e) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(p));
}

static int is_stack_running_on_compute_engine() {
  compute_engine_detector detector;
  grpc_httpcli_request request;
  grpc_httpcli_context context;
  grpc_closure destroy_closure;

  /* The http call is local. If it takes more than one sec, it is for sure not
     on compute engine. */
  grpc_millis max_detection_delay = GPR_MS_PER_SEC;

  grpc_pollset* pollset =
      static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(pollset, &g_polling_mu);
  detector.pollent = grpc_polling_entity_create_from_pollset(pollset);
  detector.is_done = 0;
  detector.success = 0;

  memset(&detector.response, 0, sizeof(detector.response));
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = (char*)GRPC_COMPUTE_ENGINE_DETECTION_HOST;
  request.http.path = (char*)"/";

  grpc_httpcli_context_init(&context);

  grpc_resource_quota* resource_quota =
      grpc_resource_quota_create("google_default_credentials");
  grpc_httpcli_get(
      &context, &detector.pollent, resource_quota, &request,
      grpc_core::ExecCtx::Get()->Now() + max_detection_delay,
      GRPC_CLOSURE_CREATE(on_compute_engine_detection_http_response, &detector,
                          grpc_schedule_on_exec_ctx),
      &detector.response);
  grpc_resource_quota_unref_internal(resource_quota);

  grpc_core::ExecCtx::Get()->Flush();

  /* Block until we get the response. This is not ideal but this should only be
     called once for the lifetime of the process by the default credentials. */
  gpr_mu_lock(g_polling_mu);
  while (!detector.is_done) {
    grpc_pollset_worker* worker = nullptr;
    if (!GRPC_LOG_IF_ERROR(
            "pollset_work",
            grpc_pollset_work(grpc_polling_entity_pollset(&detector.pollent),
                              &worker, GRPC_MILLIS_INF_FUTURE))) {
      detector.is_done = 1;
      detector.success = 0;
    }
  }
  gpr_mu_unlock(g_polling_mu);

  grpc_httpcli_context_destroy(&context);
  GRPC_CLOSURE_INIT(&destroy_closure, destroy_pollset,
                    grpc_polling_entity_pollset(&detector.pollent),
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(grpc_polling_entity_pollset(&detector.pollent),
                        &destroy_closure);
  g_polling_mu = nullptr;
  grpc_core::ExecCtx::Get()->Flush();

  gpr_free(grpc_polling_entity_pollset(&detector.pollent));
  grpc_http_response_destroy(&detector.response);

  return detector.success;
}

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
    int need_compute_engine_creds = is_stack_running_on_compute_engine();
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
