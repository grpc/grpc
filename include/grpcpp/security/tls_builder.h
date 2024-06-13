//
//
// Copyright 2024 gRPC authors.
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

#ifndef GRPCPP_SECURITY_TLS_BUILDER_H
#define GRPCPP_SECURITY_TLS_BUILDER_H

#include "credentials.h"
#include "server_credentials.h"

#include <grpc/grpc_tls.h>

namespace grpc {
class TlsChannelCredentialsBuilder final
    : public grpc_core::TlsChannelCredentialsBuilder {
  std::shared_ptr<ChannelCredentials> BuildTlsChannelCredentials();
};

class TlsServerCredentialsBuilder final
    : public grpc_core::TlsServerCredentialsBuilder {
  std::shared_ptr<ServerCredentials> BuildTlsServerCredentials();
};
}  // namespace grpc

#endif  // GRPCPP_SECURITY_TLS_BUILDER_H
