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

#include <gflags/gflags.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/security/oauth2_utils.h"

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

DEFINE_string(json_refresh_token, "", "File path of the json refresh token.");
DEFINE_bool(gce, false,
            "Get a token from the GCE metadata server (only works in GCE).");

static grpc_call_credentials* create_refresh_token_creds(
    const char* json_refresh_token_file_path) {
  grpc_slice refresh_token;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file",
      grpc_load_file(json_refresh_token_file_path, 1, &refresh_token)));
  return grpc_google_refresh_token_credentials_create(
      (const char*)GRPC_SLICE_START_PTR(refresh_token), nullptr);
}

int main(int argc, char** argv) {
  grpc_call_credentials* creds = nullptr;
  char* json_key_file_path = nullptr;
  char* token = nullptr;
  char* scope = nullptr;
  ParseCommandLineFlags(&argc, &argv, true);

  grpc_init();

  if (json_key_file_path != nullptr && !FLAGS_json_refresh_token.empty()) {
    gpr_log(GPR_ERROR,
            "--json_key and --json_refresh_token are mutually exclusive.");
    exit(1);
  }

  if (FLAGS_gce) {
    if (json_key_file_path != nullptr || scope != nullptr) {
      gpr_log(GPR_INFO,
              "Ignoring json key and scope to get a token from the GCE "
              "metadata server.");
    }
    creds = grpc_google_compute_engine_credentials_create(nullptr);
    if (creds == nullptr) {
      gpr_log(GPR_ERROR, "Could not create gce credentials.");
      exit(1);
    }
  } else if (!FLAGS_json_refresh_token.empty()) {
    creds = create_refresh_token_creds(FLAGS_json_refresh_token.c_str());
    if (creds == nullptr) {
      gpr_log(GPR_ERROR,
              "Could not create refresh token creds. %s does probably not "
              "contain a valid json refresh token.",
              FLAGS_json_refresh_token.c_str());
      exit(1);
    }
  } else {
    gpr_log(GPR_ERROR, "Missing --gce or --json_refresh_token option.");
    exit(1);
  }
  GPR_ASSERT(creds != nullptr);

  token = grpc_test_fetch_oauth2_token_with_credentials(creds);
  if (token != nullptr) {
    printf("Got token: %s.\n", token);
    gpr_free(token);
  }
  grpc_call_credentials_release(creds);
  grpc_shutdown();
  return 0;
}
