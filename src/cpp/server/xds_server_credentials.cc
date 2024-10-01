//
//
// Copyright 2020 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpcpp/security/server_credentials.h>

#include <memory>

#include "absl/log/check.h"

namespace grpc {

std::shared_ptr<ServerCredentials> XdsServerCredentials(
    const std::shared_ptr<ServerCredentials>& fallback_credentials) {
  CHECK_NE(fallback_credentials, nullptr);
  CHECK_NE(fallback_credentials->c_creds_, nullptr);
  return std::shared_ptr<ServerCredentials>(new ServerCredentials(
      grpc_xds_server_credentials_create(fallback_credentials->c_creds_)));
}

namespace experimental {

std::shared_ptr<ServerCredentials> XdsServerCredentials(
    const std::shared_ptr<ServerCredentials>& fallback_credentials) {
  return grpc::XdsServerCredentials(fallback_credentials);
}

}  // namespace experimental
}  // namespace grpc
