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
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/cmdline.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/security/oauth2_utils.h"

static grpc_call_credentials *create_refresh_token_creds(
    const char *json_refresh_token_file_path) {
  grpc_slice refresh_token;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file",
      grpc_load_file(json_refresh_token_file_path, 1, &refresh_token)));
  return grpc_google_refresh_token_credentials_create(
      (const char *)GRPC_SLICE_START_PTR(refresh_token), NULL);
}

int main(int argc, char **argv) {
  grpc_call_credentials *creds = NULL;
  char *json_key_file_path = NULL;
  char *json_refresh_token_file_path = NULL;
  char *token = NULL;
  int use_gce = 0;
  char *scope = NULL;
  gpr_cmdline *cl = gpr_cmdline_create("fetch_oauth2");
  gpr_cmdline_add_string(cl, "json_refresh_token",
                         "File path of the json refresh token.",
                         &json_refresh_token_file_path);
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
    creds = grpc_google_compute_engine_credentials_create(NULL);
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
    gpr_log(GPR_ERROR, "Missing --gce or --json_refresh_token option.");
    exit(1);
  }
  GPR_ASSERT(creds != NULL);

  token = grpc_test_fetch_oauth2_token_with_credentials(creds);
  if (token != NULL) {
    printf("Got token: %s.\n", token);
    gpr_free(token);
  }
  grpc_call_credentials_release(creds);
  gpr_cmdline_destroy(cl);
  grpc_shutdown();
  return 0;
}
