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

#include <stdio.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/cmdline.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>
#include <grpc/support/sync.h>

#include "src/core/security/credentials.h"
#include "src/core/support/file.h"

typedef struct {
  grpc_pollset pollset;
  int is_done;
} synchronizer;

static void on_oauth2_response(void *user_data,
                               grpc_credentials_md *md_elems,
                               size_t num_md, grpc_credentials_status status) {
  synchronizer *sync = user_data;
  char *token;
  gpr_slice token_slice;
  if (status == GRPC_CREDENTIALS_ERROR) {
    gpr_log(GPR_ERROR, "Fetching token failed.");
  } else {
    GPR_ASSERT(num_md == 1);
    token_slice = md_elems[0].value;
    token = gpr_malloc(GPR_SLICE_LENGTH(token_slice) + 1);
    memcpy(token, GPR_SLICE_START_PTR(token_slice),
           GPR_SLICE_LENGTH(token_slice));
    token[GPR_SLICE_LENGTH(token_slice)] = '\0';
    printf("Got token: %s.\n", token);
    gpr_free(token);
  }
  gpr_mu_lock(GRPC_POLLSET_MU(&sync->pollset));
  sync->is_done = 1;
  grpc_pollset_kick(&sync->pollset);
  gpr_mu_unlock(GRPC_POLLSET_MU(&sync->pollset));
}

static grpc_credentials *create_service_account_creds(
    const char *json_key_file_path, const char *scope) {
  int success;
  gpr_slice json_key = gpr_load_file(json_key_file_path, 1, &success);
  if (!success) {
    gpr_log(GPR_ERROR, "Could not read file %s.", json_key_file_path);
    exit(1);
  }
  return grpc_service_account_credentials_create(
      (const char *)GPR_SLICE_START_PTR(json_key), scope,
      grpc_max_auth_token_lifetime);
}

static grpc_credentials *create_refresh_token_creds(
    const char *json_refresh_token_file_path) {
  int success;
  gpr_slice refresh_token =
      gpr_load_file(json_refresh_token_file_path, 1, &success);
  if (!success) {
    gpr_log(GPR_ERROR, "Could not read file %s.", json_refresh_token_file_path);
    exit(1);
  }
  return grpc_refresh_token_credentials_create(
      (const char *)GPR_SLICE_START_PTR(refresh_token));
}

int main(int argc, char **argv) {
  synchronizer sync;
  grpc_credentials *creds = NULL;
  char *json_key_file_path = NULL;
  char *json_refresh_token_file_path = NULL;
  int use_gce = 0;
  char *scope = NULL;
  gpr_cmdline *cl = gpr_cmdline_create("fetch_oauth2");
  gpr_cmdline_add_string(cl, "json_key",
                         "File path of the json key. Mutually exclusive with "
                         "--json_refresh_token.",
                         &json_key_file_path);
  gpr_cmdline_add_string(cl, "json_refresh_token",
                         "File path of the json refresh token. Mutually "
                         "exclusive with --json_key.",
                         &json_refresh_token_file_path);
  gpr_cmdline_add_string(cl, "scope",
                         "Space delimited permissions. Only used for "
                         "--json_key, ignored otherwise.",
                         &scope);
  gpr_cmdline_add_flag(
      cl, "gce",
      "Get a token from the GCE metadata server (only works in GCE).",
      &use_gce);
  gpr_cmdline_parse(cl, argc, argv);

  grpc_init();

  if (json_key_file_path != NULL && json_refresh_token_file_path != NULL) {
    gpr_log(GPR_ERROR,
            "--json_key and --json_refresh_token are mutually exclusive.");
    exit(1);
  }

  if (use_gce) {
    if (json_key_file_path != NULL || scope != NULL) {
      gpr_log(GPR_INFO,
              "Ignoring json key and scope to get a token from the GCE "
              "metadata server.");
    }
    creds = grpc_compute_engine_credentials_create();
    if (creds == NULL) {
      gpr_log(GPR_ERROR, "Could not create gce credentials.");
      exit(1);
    }
  } else if (json_refresh_token_file_path != NULL) {
    creds = create_refresh_token_creds(json_refresh_token_file_path);
    if (creds == NULL) {
      gpr_log(GPR_ERROR,
              "Could not create refresh token creds. %s does probably not "
              "contain a valid json refresh token.",
              json_refresh_token_file_path);
      exit(1);
    }
  } else {
    if (json_key_file_path == NULL) {
      gpr_log(GPR_ERROR, "Missing --json_key option.");
      exit(1);
    }
    if (scope == NULL) {
      gpr_log(GPR_ERROR, "Missing --scope option.");
      exit(1);
    }

    creds = create_service_account_creds(json_key_file_path, scope);
    if (creds == NULL) {
      gpr_log(GPR_ERROR,
              "Could not create service account creds. %s does probably not "
              "contain a valid json key.",
              json_key_file_path);
      exit(1);
    }
  }
  GPR_ASSERT(creds != NULL);

  grpc_pollset_init(&sync.pollset);
  sync.is_done = 0;

  grpc_credentials_get_request_metadata(creds, &sync.pollset, "", on_oauth2_response, &sync);

  gpr_mu_lock(GRPC_POLLSET_MU(&sync.pollset));
  while (!sync.is_done)
    grpc_pollset_work(&sync.pollset, gpr_inf_future(GPR_CLOCK_REALTIME));
  gpr_mu_unlock(GRPC_POLLSET_MU(&sync.pollset));

  grpc_pollset_destroy(&sync.pollset);
  grpc_credentials_release(creds);
  gpr_cmdline_destroy(cl);
  grpc_shutdown();
  return 0;
}
