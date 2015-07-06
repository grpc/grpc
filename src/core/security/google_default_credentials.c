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

#include "src/core/security/credentials.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/httpcli/httpcli.h"
#include "src/core/support/env.h"
#include "src/core/support/file.h"

/* -- Constants. -- */

#define GRPC_COMPUTE_ENGINE_DETECTION_HOST "metadata.google.internal"
#define GRPC_GOOGLE_CREDENTIALS_ENV_VAR "GOOGLE_APPLICATION_CREDENTIALS"

/* -- Default credentials. -- */

static grpc_credentials *default_credentials = NULL;
static int compute_engine_detection_done = 0;
static gpr_mu g_mu;
static gpr_once g_once = GPR_ONCE_INIT;

static void init_default_credentials(void) { gpr_mu_init(&g_mu); }

typedef struct {
  grpc_pollset pollset;
  int is_done;
  int success;
} compute_engine_detector;

static void on_compute_engine_detection_http_response(
    void *user_data, const grpc_httpcli_response *response) {
  compute_engine_detector *detector = (compute_engine_detector *)user_data;
  if (response != NULL && response->status == 200 && response->hdr_count > 0) {
    /* Internet providers can return a generic response to all requests, so
       it is necessary to check that metadata header is present also. */
    size_t i;
    for (i = 0; i < response->hdr_count; i++) {
      grpc_httpcli_header *header = &response->hdrs[i];
      if (strcmp(header->key, "Metadata-Flavor") == 0 &&
          strcmp(header->value, "Google") == 0) {
        detector->success = 1;
        break;
      }
    }
  }
  gpr_mu_lock(GRPC_POLLSET_MU(&detector->pollset));
  detector->is_done = 1;
  grpc_pollset_kick(&detector->pollset);
  gpr_mu_unlock(GRPC_POLLSET_MU(&detector->pollset));
}

static int is_stack_running_on_compute_engine(void) {
  compute_engine_detector detector;
  grpc_httpcli_request request;
  grpc_httpcli_context context;

  /* The http call is local. If it takes more than one sec, it is for sure not
     on compute engine. */
  gpr_timespec max_detection_delay = {1, 0};

  grpc_pollset_init(&detector.pollset);
  detector.is_done = 0;
  detector.success = 0;

  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = GRPC_COMPUTE_ENGINE_DETECTION_HOST;
  request.path = "/";

  grpc_httpcli_context_init(&context);

  grpc_httpcli_get(
      &context, &detector.pollset, &request,
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), max_detection_delay),
      on_compute_engine_detection_http_response, &detector);

  /* Block until we get the response. This is not ideal but this should only be
     called once for the lifetime of the process by the default credentials. */
  gpr_mu_lock(GRPC_POLLSET_MU(&detector.pollset));
  while (!detector.is_done) {
    grpc_pollset_work(&detector.pollset, gpr_inf_future);
  }
  gpr_mu_unlock(GRPC_POLLSET_MU(&detector.pollset));

  grpc_httpcli_context_destroy(&context);
  grpc_pollset_destroy(&detector.pollset);

  return detector.success;
}

/* Takes ownership of creds_path if not NULL. */
static grpc_credentials *create_jwt_creds_from_path(char *creds_path) {
  grpc_credentials *result = NULL;
  gpr_slice creds_data;
  int file_ok = 0;
  if (creds_path == NULL) return NULL;
  creds_data = gpr_load_file(creds_path, 1, &file_ok);
  gpr_free(creds_path);
  if (file_ok) {
    result = grpc_jwt_credentials_create(
        (const char *)GPR_SLICE_START_PTR(creds_data),
        grpc_max_auth_token_lifetime);
    gpr_slice_unref(creds_data);
  }
  return result;
}

/* Takes ownership of creds_path if not NULL. */
static grpc_credentials *create_refresh_token_creds_from_path(
    char *creds_path) {
  grpc_credentials *result = NULL;
  gpr_slice creds_data;
  int file_ok = 0;
  if (creds_path == NULL) return NULL;
  creds_data = gpr_load_file(creds_path, 1, &file_ok);
  gpr_free(creds_path);
  if (file_ok) {
    result = grpc_refresh_token_credentials_create(
        (const char *)GPR_SLICE_START_PTR(creds_data));
    gpr_slice_unref(creds_data);
  }
  return result;
}

grpc_credentials *grpc_google_default_credentials_create(void) {
  grpc_credentials *result = NULL;
  int serving_cached_credentials = 0;
  gpr_once_init(&g_once, init_default_credentials);

  gpr_mu_lock(&g_mu);

  if (default_credentials != NULL) {
    result = grpc_credentials_ref(default_credentials);
    serving_cached_credentials = 1;
    goto end;
  }

  /* First, try the environment variable. */
  result =
      create_jwt_creds_from_path(gpr_getenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR));
  if (result != NULL) goto end;

  /* Then the well-known file. */
  result = create_refresh_token_creds_from_path(
      grpc_get_well_known_google_credentials_file_path());
  if (result != NULL) goto end;

  /* At last try to see if we're on compute engine (do the detection only once
     since it requires a network test). */
  if (!compute_engine_detection_done) {
    int need_compute_engine_creds = is_stack_running_on_compute_engine();
    compute_engine_detection_done = 1;
    if (need_compute_engine_creds) {
      result = grpc_compute_engine_credentials_create();
    }
  }

end:
  if (!serving_cached_credentials && result != NULL) {
    /* Blend with default ssl credentials and add a global reference so that it
       can be cached and re-served. */
    result = grpc_composite_credentials_create(
        grpc_ssl_credentials_create(NULL, NULL), result);
    GPR_ASSERT(result != NULL);
    default_credentials = grpc_credentials_ref(result);
  }
  gpr_mu_unlock(&g_mu);
  return result;
}
