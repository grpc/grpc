//
//
// Copyright 2018 gRPC authors.
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
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <string.h>

#include <optional>
#include <string>

#include "absl/log/check.h"
#include "fuzztest/fuzztest.h"
#include "src/core/credentials/transport/alts/alts_credentials.h"
#include "src/core/credentials/transport/alts/check_gcp_environment.h"
#include "src/core/credentials/transport/alts/grpc_alts_credentials_options.h"
#include "src/core/util/crash.h"
#include "src/core/util/env.h"
#include "test/core/test_util/test_config.h"

const char* StrPtr(const std::optional<std::string>& str) {
  return str.has_value() ? str->c_str() : nullptr;
}

void ChannelCredentialsTest(bool enable_untrusted_alts,
                            std::optional<std::string> handshaker_service_url,
                            std::vector<std::string> target_service_accounts) {
  grpc_init();
  bool is_on_gcp = grpc_alts_is_running_on_gcp();
  grpc_alts_credentials_options* options =
      grpc_alts_credentials_client_options_create();
  for (const auto& service_account : target_service_accounts) {
    grpc_alts_credentials_client_options_add_target_service_account(
        options, service_account.c_str());
  }
  // Added to improve code coverage.
  grpc_alts_credentials_client_options_add_target_service_account(options,
                                                                  nullptr);
  grpc_alts_credentials_client_options_add_target_service_account(
      nullptr, "this is service account");
  grpc_channel_credentials* cred = grpc_alts_credentials_create_customized(
      options, StrPtr(handshaker_service_url), enable_untrusted_alts);
  if (!enable_untrusted_alts && !is_on_gcp) {
    CHECK_EQ(cred, nullptr);
  } else {
    CHECK_NE(cred, nullptr);
  }
  grpc_channel_credentials_release(cred);
  grpc_alts_credentials_options_destroy(options);
  grpc_shutdown();
}
FUZZ_TEST(AltsCredentials, ChannelCredentialsTest);

void ServerCredentialsTest(bool enable_untrusted_alts,
                           std::optional<std::string> handshaker_service_url) {
  grpc_init();
  bool is_on_gcp = grpc_alts_is_running_on_gcp();
  grpc_alts_credentials_options* options =
      grpc_alts_credentials_server_options_create();
  grpc_server_credentials* cred =
      grpc_alts_server_credentials_create_customized(
          options, StrPtr(handshaker_service_url), enable_untrusted_alts);
  if (!enable_untrusted_alts && !is_on_gcp) {
    CHECK_EQ(cred, nullptr);
  } else {
    CHECK_NE(cred, nullptr);
  }
  grpc_server_credentials_release(cred);
  grpc_alts_credentials_options_destroy(options);
  grpc_shutdown();
}
FUZZ_TEST(AltsCredentials, ServerCredentialsTest);
