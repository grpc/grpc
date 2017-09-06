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

namespace grpc {
namespace testing {

std::shared_ptr<grpc::ChannelCredentials> CliCredentials::GetCredentials()
    const {
  if (!FLAGS_enable_ssl) {
    return grpc::InsecureChannelCredentials();
  } else {
    if (FLAGS_use_auth) {
      return grpc::GoogleDefaultCredentials();
    } else {
      return grpc::SslCredentials(grpc::SslCredentialsOptions());
    }
  }
}

const grpc::string CliCredentials::GetCredentialUsage() const {
  return "    --enable_ssl             ; Set whether to use tls\n"
         "    --use_auth               ; Set whether to create default google"
         " credentials\n";
}
}  // namespace testing
}  // namespace grpc
