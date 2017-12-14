/*
 *
 * Copyright 2016 gRPC authors.
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

#include "test/cpp/util/cli_credentials.h"

#include <gflags/gflags.h>

DEFINE_bool(enable_ssl, false, "Whether to use ssl/tls.");
DEFINE_bool(use_auth, false, "Whether to create default google credentials.");
DEFINE_string(
    access_token, "",
    "The access token that will be sent to the server to authenticate RPCs.");

namespace grpc {
namespace testing {

std::shared_ptr<grpc::ChannelCredentials> CliCredentials::GetCredentials()
    const {
  if (!FLAGS_access_token.empty()) {
    if (FLAGS_use_auth) {
      fprintf(stderr,
              "warning: use_auth is ignored when access_token is provided.");
    }

    return grpc::CompositeChannelCredentials(
        grpc::SslCredentials(grpc::SslCredentialsOptions()),
        grpc::AccessTokenCredentials(FLAGS_access_token));
  }

  if (FLAGS_use_auth) {
    return grpc::GoogleDefaultCredentials();
  }

  if (FLAGS_enable_ssl) {
    return grpc::SslCredentials(grpc::SslCredentialsOptions());
  }

  return grpc::InsecureChannelCredentials();
}

const grpc::string CliCredentials::GetCredentialUsage() const {
  return "    --enable_ssl             ; Set whether to use tls\n"
         "    --use_auth               ; Set whether to create default google"
         " credentials\n"
         "    --access_token           ; Set the access token in metadata,"
         " overrides --use_auth\n";
}
}  // namespace testing
}  // namespace grpc
