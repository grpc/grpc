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

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/jwt/jwt_credentials.h"

#include <gflags/gflags.h>

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

DEFINE_string(json_key, "", "File path of the json key.");
DEFINE_string(scope, "",
              "OPTIONAL Space delimited permissions. Mutually "
              "exclusive with service_url");
DEFINE_string(service_url, "",
              "OPTIONAL service URL. Mutually exclusive with scope.");

void create_jwt(const char* json_key_file_path, const char* service_url,
                const char* scope) {
  grpc_auth_json_key key;
  char* jwt;
  grpc_slice json_key_data;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file", grpc_load_file(json_key_file_path, 1, &json_key_data)));
  key = grpc_auth_json_key_create_from_string(
      (const char*)GRPC_SLICE_START_PTR(json_key_data));
  grpc_slice_unref(json_key_data);
  if (!grpc_auth_json_key_is_valid(&key)) {
    fprintf(stderr, "Could not parse json key.\n");
    exit(1);
  }
  jwt = grpc_jwt_encode_and_sign(
      &key, service_url == nullptr ? GRPC_JWT_OAUTH2_AUDIENCE : service_url,
      grpc_max_auth_token_lifetime(), scope);
  grpc_auth_json_key_destruct(&key);
  if (jwt == nullptr) {
    fprintf(stderr, "Could not create JWT.\n");
    exit(1);
  }
  fprintf(stdout, "%s\n", jwt);
  gpr_free(jwt);
}

int main(int argc, char** argv) {
  grpc_init();
  ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_json_key.empty()) {
    fprintf(stderr, "Missing --json_key option.\n");
    exit(1);
  }
  if (!FLAGS_scope.empty()) {
    if (!FLAGS_service_url.empty()) {
      fprintf(stderr,
              "Options --scope and --service_url are mutually exclusive.\n");
      exit(1);
    }
  } else if (FLAGS_service_url.empty()) {
    fprintf(stderr, "Need one of --service_url or --scope options.\n");
    exit(1);
  }

  create_jwt(FLAGS_json_key.c_str(), FLAGS_service_url.c_str(),
             FLAGS_scope.c_str());

  grpc_shutdown();
  return 0;
}
