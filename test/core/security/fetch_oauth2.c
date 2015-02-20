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

#include "src/core/security/credentials.h"
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/cmdline.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>
#include <grpc/support/sync.h>

typedef struct {
  gpr_cv cv;
  gpr_mu mu;
  int is_done;
} synchronizer;

static void on_oauth2_response(void *user_data, grpc_mdelem **md_elems,
                               size_t num_md, grpc_credentials_status status) {
  synchronizer *sync = user_data;
  char *token;
  gpr_slice token_slice;
  if (status == GRPC_CREDENTIALS_ERROR) {
    gpr_log(GPR_ERROR, "Fetching token failed.");
  } else {
    GPR_ASSERT(num_md == 1);
    token_slice = md_elems[0]->value->slice;
    token = gpr_malloc(GPR_SLICE_LENGTH(token_slice) + 1);
    memcpy(token, GPR_SLICE_START_PTR(token_slice),
           GPR_SLICE_LENGTH(token_slice));
    token[GPR_SLICE_LENGTH(token_slice)] = '\0';
    printf("Got token: %s.\n", token);
    gpr_free(token);
  }
  gpr_mu_lock(&sync->mu);
  sync->is_done = 1;
  gpr_mu_unlock(&sync->mu);
  gpr_cv_signal(&sync->cv);
}

static grpc_credentials *create_service_account_creds(
    const char *json_key_file_path, const char *scope) {
  char json_key[8192]; /* Should be plenty. */
  char *current = json_key;
  FILE *json_key_file = fopen(json_key_file_path, "r");
  if (json_key_file == NULL) {
    gpr_log(GPR_ERROR, "Invalid path for json key file: %s.",
            json_key_file_path);
    exit(1);
  }

  do {
    size_t bytes_read = fread(
        current, 1, sizeof(json_key) - (current - json_key), json_key_file);
    if (bytes_read == 0) {
      if (!feof(json_key_file)) {
        gpr_log(GPR_ERROR, "Error occured while reading %s.",
                json_key_file_path);
        exit(1);
      }
      break;
    }
    current += bytes_read;
  } while (sizeof(json_key) > (size_t)(current - json_key));

  if ((current - json_key) == sizeof(json_key)) {
    gpr_log(GPR_ERROR, "Json key file %s exceeds size limit (%d bytes).",
            json_key_file_path, (int)sizeof(json_key));
    exit(1);
  }
  fclose(json_key_file);

  return grpc_service_account_credentials_create(json_key, scope,
                                                 grpc_max_auth_token_lifetime);
}

int main(int argc, char **argv) {
  synchronizer sync;
  grpc_credentials *creds = NULL;
  char *json_key_file_path = NULL;
  int use_gce = 0;
  char *scope = NULL;
  gpr_cmdline *cl = gpr_cmdline_create("fetch_oauth2");
  gpr_cmdline_add_string(cl, "json_key", "File path of the json key.",
                         &json_key_file_path);
  gpr_cmdline_add_string(cl, "scope", "Space delimited permissions.", &scope);
  gpr_cmdline_add_flag(
      cl, "gce",
      "Get a token from the GCE metadata server (only works in GCE).",
      &use_gce);
  gpr_cmdline_parse(cl, argc, argv);

  grpc_init();

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

  gpr_mu_init(&sync.mu);
  gpr_cv_init(&sync.cv);
  sync.is_done = 0;

  grpc_credentials_get_request_metadata(creds, "", on_oauth2_response, &sync);

  gpr_mu_lock(&sync.mu);
  while (!sync.is_done) gpr_cv_wait(&sync.cv, &sync.mu, gpr_inf_future);
  gpr_mu_unlock(&sync.mu);

  gpr_mu_destroy(&sync.mu);
  gpr_cv_destroy(&sync.cv);
  grpc_credentials_release(creds);
  gpr_cmdline_destroy(cl);
  grpc_shutdown();
  return 0;
}
