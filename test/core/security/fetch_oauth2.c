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
