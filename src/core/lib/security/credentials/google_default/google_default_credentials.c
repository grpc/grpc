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

#include "src/core/lib/security/credentials/credentials.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/security/credentials/jwt/jwt_credentials.h"
#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/load_file.h"
#include "src/core/lib/surface/api_trace.h"

/* -- Constants. -- */

#define GRPC_COMPUTE_ENGINE_DETECTION_HOST "metadata.google.internal"

/* -- Default credentials. -- */

static grpc_channel_credentials *default_credentials = NULL;
static int compute_engine_detection_done = 0;
static gpr_mu g_state_mu;
static gpr_mu *g_polling_mu;
static gpr_once g_once = GPR_ONCE_INIT;

static void init_default_credentials(void) { gpr_mu_init(&g_state_mu); }

typedef struct {
  grpc_polling_entity pollent;
  int is_done;
  int success;
} compute_engine_detector;

static void on_compute_engine_detection_http_response(
    grpc_exec_ctx *exec_ctx, void *user_data,
    const grpc_http_response *response) {
  compute_engine_detector *detector = (compute_engine_detector *)user_data;
  if (response != NULL && response->status == 200 && response->hdr_count > 0) {
    /* Internet providers can return a generic response to all requests, so
       it is necessary to check that metadata header is present also. */
    size_t i;
    for (i = 0; i < response->hdr_count; i++) {
      grpc_http_header *header = &response->hdrs[i];
      if (strcmp(header->key, "Metadata-Flavor") == 0 &&
          strcmp(header->value, "Google") == 0) {
        detector->success = 1;
        break;
      }
    }
  }
  gpr_mu_lock(g_polling_mu);
  detector->is_done = 1;
  grpc_pollset_kick(grpc_polling_entity_pollset(&detector->pollent), NULL);
  gpr_mu_unlock(g_polling_mu);
}

static void destroy_pollset(grpc_exec_ctx *exec_ctx, void *p, bool s) {
  grpc_pollset_destroy(p);
}

static int is_stack_running_on_compute_engine(void) {
  compute_engine_detector detector;
  grpc_httpcli_request request;
  grpc_httpcli_context context;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_closure destroy_closure;

  /* The http call is local. If it takes more than one sec, it is for sure not
     on compute engine. */
  gpr_timespec max_detection_delay = gpr_time_from_seconds(1, GPR_TIMESPAN);

  grpc_pollset *pollset = gpr_malloc(grpc_pollset_size());
  grpc_pollset_init(pollset, &g_polling_mu);
  detector.pollent = grpc_polling_entity_create_from_pollset(pollset);
  detector.is_done = 0;
  detector.success = 0;

  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = GRPC_COMPUTE_ENGINE_DETECTION_HOST;
  request.http.path = "/";

  grpc_httpcli_context_init(&context);

  grpc_httpcli_get(
      &exec_ctx, &context, &detector.pollent, &request,
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), max_detection_delay),
      on_compute_engine_detection_http_response, &detector);

  grpc_exec_ctx_finish(&exec_ctx);

  /* Block until we get the response. This is not ideal but this should only be
     called once for the lifetime of the process by the default credentials. */
  gpr_mu_lock(g_polling_mu);
  while (!detector.is_done) {
    grpc_pollset_worker *worker = NULL;
    grpc_pollset_work(&exec_ctx, grpc_polling_entity_pollset(&detector.pollent),
                      &worker, gpr_now(GPR_CLOCK_MONOTONIC),
                      gpr_inf_future(GPR_CLOCK_MONOTONIC));
  }
  gpr_mu_unlock(g_polling_mu);

  grpc_httpcli_context_destroy(&context);
  grpc_closure_init(&destroy_closure, destroy_pollset,
                    grpc_polling_entity_pollset(&detector.pollent));
  grpc_pollset_shutdown(&exec_ctx,
                        grpc_polling_entity_pollset(&detector.pollent),
                        &destroy_closure);
  grpc_exec_ctx_finish(&exec_ctx);
  g_polling_mu = NULL;

  gpr_free(grpc_polling_entity_pollset(&detector.pollent));

  return detector.success;
}

/* Takes ownership of creds_path if not NULL. */
static grpc_call_credentials *create_default_creds_from_path(char *creds_path) {
  grpc_json *json = NULL;
  grpc_auth_json_key key;
  grpc_auth_refresh_token token;
  grpc_call_credentials *result = NULL;
  gpr_slice creds_data = gpr_empty_slice();
  int file_ok = 0;
  if (creds_path == NULL) goto end;
  creds_data = gpr_load_file(creds_path, 0, &file_ok);
  if (!file_ok) goto end;
  json = grpc_json_parse_string_with_len(
      (char *)GPR_SLICE_START_PTR(creds_data), GPR_SLICE_LENGTH(creds_data));
  if (json == NULL) goto end;

  /* First, try an auth json key. */
  key = grpc_auth_json_key_create_from_json(json);
  if (grpc_auth_json_key_is_valid(&key)) {
    result =
        grpc_service_account_jwt_access_credentials_create_from_auth_json_key(
            key, grpc_max_auth_token_lifetime());
    goto end;
  }

  /* Then try a refresh token if the auth json key was invalid. */
  token = grpc_auth_refresh_token_create_from_json(json);
  if (grpc_auth_refresh_token_is_valid(&token)) {
    result =
        grpc_refresh_token_credentials_create_from_auth_refresh_token(token);
    goto end;
  }

end:
  if (creds_path != NULL) gpr_free(creds_path);
  gpr_slice_unref(creds_data);
  if (json != NULL) grpc_json_destroy(json);
  return result;
}

grpc_channel_credentials *grpc_google_default_credentials_create(void) {
  grpc_channel_credentials *result = NULL;
  grpc_call_credentials *call_creds = NULL;

  GRPC_API_TRACE("grpc_google_default_credentials_create(void)", 0, ());

  gpr_once_init(&g_once, init_default_credentials);

  gpr_mu_lock(&g_state_mu);

  if (default_credentials != NULL) {
    result = grpc_channel_credentials_ref(default_credentials);
    goto end;
  }

  /* First, try the environment variable. */
  call_creds = create_default_creds_from_path(
      gpr_getenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR));
  if (call_creds != NULL) goto end;

  /* Then the well-known file. */
  call_creds = create_default_creds_from_path(
      grpc_get_well_known_google_credentials_file_path());
  if (call_creds != NULL) goto end;

  /* At last try to see if we're on compute engine (do the detection only once
     since it requires a network test). */
  if (!compute_engine_detection_done) {
    int need_compute_engine_creds = is_stack_running_on_compute_engine();
    compute_engine_detection_done = 1;
    if (need_compute_engine_creds) {
      call_creds = grpc_google_compute_engine_credentials_create(NULL);
    }
  }

end:
  if (result == NULL) {
    if (call_creds != NULL) {
      /* Blend with default ssl credentials and add a global reference so that
         it
         can be cached and re-served. */
      grpc_channel_credentials *ssl_creds =
          grpc_ssl_credentials_create(NULL, NULL, NULL);
      default_credentials = grpc_channel_credentials_ref(
          grpc_composite_channel_credentials_create(ssl_creds, call_creds,
                                                    NULL));
      GPR_ASSERT(default_credentials != NULL);
      grpc_channel_credentials_unref(ssl_creds);
      grpc_call_credentials_unref(call_creds);
      result = default_credentials;
    } else {
      gpr_log(GPR_ERROR, "Could not create google default credentials.");
    }
  }
  gpr_mu_unlock(&g_state_mu);
  return result;
}

void grpc_flush_cached_google_default_credentials(void) {
  gpr_once_init(&g_once, init_default_credentials);
  gpr_mu_lock(&g_state_mu);
  if (default_credentials != NULL) {
    grpc_channel_credentials_unref(default_credentials);
    default_credentials = NULL;
  }
  compute_engine_detection_done = 0;
  gpr_mu_unlock(&g_state_mu);
}

/* -- Well known credentials path. -- */

static grpc_well_known_credentials_path_getter creds_path_getter = NULL;

char *grpc_get_well_known_google_credentials_file_path(void) {
  if (creds_path_getter != NULL) return creds_path_getter();
  return grpc_get_well_known_google_credentials_file_path_impl();
}

void grpc_override_well_known_credentials_path_getter(
    grpc_well_known_credentials_path_getter getter) {
  creds_path_getter = getter;
}
