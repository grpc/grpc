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
#include <grpcpp/security/credentials.h>

#include <memory>

#include "absl/log/check.h"

namespace grpc {
class XdsChannelCredentialsImpl final : public ChannelCredentials {
 public:
  explicit XdsChannelCredentialsImpl(
      const std::shared_ptr<ChannelCredentials>& fallback_creds)
      : ChannelCredentials(
            grpc_xds_credentials_create(fallback_creds->c_creds_)) {
    CHECK_NE(fallback_creds->c_creds_, nullptr);
  }
};

std::shared_ptr<ChannelCredentials> XdsCredentials(
    const std::shared_ptr<ChannelCredentials>& fallback_creds) {
  CHECK_NE(fallback_creds, nullptr);
  return std::make_shared<XdsChannelCredentialsImpl>(fallback_creds);
}

namespace experimental {

std::shared_ptr<ChannelCredentials> XdsCredentials(
    const std::shared_ptr<ChannelCredentials>& fallback_creds) {
  return grpc::XdsCredentials(fallback_creds);
}

}  // namespace experimental
}  // namespace grpc
