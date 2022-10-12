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

#include <memory>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>
#include <grpcpp/security/credentials.h>

#include "src/cpp/client/secure_credentials.h"

namespace grpc {

std::shared_ptr<ChannelCredentials> XdsCredentials(
    const std::shared_ptr<ChannelCredentials>& fallback_creds) {
  GPR_ASSERT(fallback_creds != nullptr);
  if (fallback_creds->IsInsecure()) {
    grpc_channel_credentials* insecure_creds =
        grpc_insecure_credentials_create();
    auto xds_creds = internal::WrapChannelCredentials(
        grpc_xds_credentials_create(insecure_creds));
    grpc_channel_credentials_release(insecure_creds);
    return xds_creds;
  } else {
    return internal::WrapChannelCredentials(grpc_xds_credentials_create(
        fallback_creds->AsSecureCredentials()->GetRawCreds()));
  }
}

namespace experimental {

std::shared_ptr<ChannelCredentials> XdsCredentials(
    const std::shared_ptr<ChannelCredentials>& fallback_creds) {
  return grpc::XdsCredentials(fallback_creds);
}

}  // namespace experimental
}  // namespace grpc
