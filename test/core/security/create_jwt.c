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

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/jwt/jwt_credentials.h"

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/cmdline.h>
#include <grpc/support/log.h>

void create_jwt(const char *json_key_file_path, const char *service_url,
                const char *scope) {
  grpc_auth_json_key key;
  char *jwt;
  grpc_slice json_key_data;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file", grpc_load_file(json_key_file_path, 1, &json_key_data)));
  key = grpc_auth_json_key_create_from_string(
      (const char *)GRPC_SLICE_START_PTR(json_key_data));
  grpc_slice_unref(json_key_data);
  if (!grpc_auth_json_key_is_valid(&key)) {
    fprintf(stderr, "Could not parse json key.\n");
    exit(1);
  }
  jwt = grpc_jwt_encode_and_sign(
      &key, service_url == NULL ? GRPC_JWT_OAUTH2_AUDIENCE : service_url,
      grpc_max_auth_token_lifetime(), scope);
  grpc_auth_json_key_destruct(&key);
  if (jwt == NULL) {
    fprintf(stderr, "Could not create JWT.\n");
    exit(1);
  }
  fprintf(stdout, "%s\n", jwt);
  gpr_free(jwt);
}

int main(int argc, char **argv) {
  char *scope = NULL;
  char *json_key_file_path = NULL;
  char *service_url = NULL;
  grpc_init();
  gpr_cmdline *cl = gpr_cmdline_create("create_jwt");
  gpr_cmdline_add_string(cl, "json_key", "File path of the json key.",
                         &json_key_file_path);
  gpr_cmdline_add_string(cl, "scope",
                         "OPTIONAL Space delimited permissions. Mutually "
                         "exclusive with service_url",
                         &scope);
  gpr_cmdline_add_string(cl, "service_url",
                         "OPTIONAL service URL. Mutually exclusive with scope.",
                         &service_url);
  gpr_cmdline_parse(cl, argc, argv);

  if (json_key_file_path == NULL) {
    fprintf(stderr, "Missing --json_key option.\n");
    exit(1);
  }
  if (scope != NULL) {
    if (service_url != NULL) {
      fprintf(stderr,
              "Options --scope and --service_url are mutually exclusive.\n");
      exit(1);
    }
  } else if (service_url == NULL) {
    fprintf(stderr, "Need one of --service_url or --scope options.\n");
    exit(1);
  }

  create_jwt(json_key_file_path, service_url, scope);

  gpr_cmdline_destroy(cl);
  grpc_shutdown();
  return 0;
}
