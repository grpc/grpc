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

#include "src/cpp/server/secure_server_credentials.h"

namespace grpc {
namespace experimental {

std::shared_ptr<ServerCredentials> XdsServerCredentials(
    const std::shared_ptr<ServerCredentials>& fallback_credentials) {
  GPR_ASSERT(fallback_credentials != nullptr);
  if (fallback_credentials->IsInsecure()) {
    grpc_server_credentials* insecure_creds =
        grpc_insecure_server_credentials_create();
    auto xds_creds = std::make_shared<SecureServerCredentials>(
        grpc_xds_server_credentials_create(insecure_creds));
    grpc_server_credentials_release(insecure_creds);
    return xds_creds;
  }
  return std::make_shared<SecureServerCredentials>(
      grpc_xds_server_credentials_create(
          fallback_credentials->AsSecureServerCredentials()->c_creds()));
}

}  // namespace experimental
}  // namespace grpc
