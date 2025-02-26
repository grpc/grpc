//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>
#include <grpcpp/security/credentials.h>
#include <stdio.h>
#include <string.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/credentials/call/json_util.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/crash.h"
#include "src/cpp/client/secure_credentials.h"
#include "test/core/credentials/call/oauth2/oauth2_utils.h"
#include "test/core/test_util/cmdline.h"
#include "test/core/test_util/tls_utils.h"

static grpc_call_credentials* create_sts_creds(const char* json_file_path) {
  grpc::experimental::StsCredentialsOptions options;
  if (strlen(json_file_path) == 0) {
    auto status = grpc::experimental::StsCredentialsOptionsFromEnv(&options);
    if (!status.ok()) {
      LOG(ERROR) << status.error_message();
      return nullptr;
    }
  } else {
    std::string sts_options =
        grpc_core::testing::GetFileContents(json_file_path);
    auto status = grpc::experimental::StsCredentialsOptionsFromJson(sts_options,
                                                                    &options);
    if (!status.ok()) {
      LOG(ERROR) << status.error_message();
      return nullptr;
    }
  }
  grpc_sts_credentials_options opts =
      grpc::experimental::StsCredentialsCppToCoreOptions(options);
  grpc_call_credentials* result = grpc_sts_credentials_create(&opts, nullptr);
  return result;
}

static grpc_call_credentials* create_refresh_token_creds(
    const char* json_refresh_token_file_path) {
  std::string refresh_token =
      grpc_core::testing::GetFileContents(json_refresh_token_file_path);
  return grpc_google_refresh_token_credentials_create(refresh_token.c_str(),
                                                      nullptr);
}

int main(int argc, char** argv) {
  grpc_call_credentials* creds = nullptr;
  const char* json_sts_options_file_path = nullptr;
  const char* json_refresh_token_file_path = nullptr;
  char* token = nullptr;
  int use_gce = 0;
  gpr_cmdline* cl = gpr_cmdline_create("fetch_oauth2");
  gpr_cmdline_add_string(cl, "json_refresh_token",
                         "File path of the json refresh token.",
                         &json_refresh_token_file_path);
  gpr_cmdline_add_string(
      cl, "json_sts_options",
      "File path of the json sts options. If the path is empty, the program "
      "will attempt to use the $STS_CREDENTIALS environment variable to access "
      "a file containing the options.",
      &json_sts_options_file_path);
  gpr_cmdline_add_flag(
      cl, "gce",
      "Get a token from the GCE metadata server (only works in GCE).",
      &use_gce);
  gpr_cmdline_parse(cl, argc, argv);

  grpc_init();

  if (json_sts_options_file_path != nullptr &&
      json_refresh_token_file_path != nullptr) {
    LOG(ERROR) << "--json_sts_options and --json_refresh_token are mutually "
                  "exclusive.";
    exit(1);
  }

  if (use_gce) {
    if (json_sts_options_file_path != nullptr ||
        json_refresh_token_file_path != nullptr) {
      LOG(INFO)
          << "Ignoring json refresh token or sts options to get a token from "
             "the GCE metadata server.";
    }
    creds = grpc_google_compute_engine_credentials_create(nullptr);
    if (creds == nullptr) {
      LOG(ERROR) << "Could not create gce credentials.";
      exit(1);
    }
  } else if (json_refresh_token_file_path != nullptr) {
    creds = create_refresh_token_creds(json_refresh_token_file_path);
    if (creds == nullptr) {
      LOG(ERROR) << "Could not create refresh token creds. "
                 << json_refresh_token_file_path
                 << " does probably not contain a valid json refresh token.";
      exit(1);
    }
  } else if (json_sts_options_file_path != nullptr) {
    creds = create_sts_creds(json_sts_options_file_path);
    if (creds == nullptr) {
      LOG(ERROR) << "Could not create sts creds. " << json_sts_options_file_path
                 << " does probably not contain a valid json for sts options.";
      exit(1);
    }
  } else {
    LOG(ERROR)
        << "Missing --gce, --json_sts_options, or --json_refresh_token option.";
    exit(1);
  }
  CHECK_NE(creds, nullptr);

  token = grpc_test_fetch_oauth2_token_with_credentials(creds);
  if (token != nullptr) {
    printf("Got token: %s.\n", token);
    gpr_free(token);
  }
  grpc_call_credentials_release(creds);
  gpr_cmdline_destroy(cl);
  grpc_shutdown();
  return 0;
}
