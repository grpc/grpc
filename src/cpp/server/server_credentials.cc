// Copyright 2024 The gRPC Authors
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
#include <grpc/support/port_platform.h>
#include <grpcpp/security/server_credentials.h>

#include "src/core/util/crash.h"

namespace grpc {

ServerCredentials::ServerCredentials(grpc_server_credentials* creds)
    : c_creds_(creds) {}

ServerCredentials::~ServerCredentials() {
  grpc_server_credentials_release(c_creds_);
}

void ServerCredentials::SetAuthMetadataProcessor(
    const std::shared_ptr<grpc::AuthMetadataProcessor>& /* processor */) {
  grpc_core::Crash("Not Supported");
}

int ServerCredentials::AddPortToServer(const std::string& addr,
                                       grpc_server* server) {
  return grpc_server_add_http2_port(server, addr.c_str(), c_creds_);
}

}  // namespace grpc
