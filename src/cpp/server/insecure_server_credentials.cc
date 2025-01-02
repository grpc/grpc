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

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpcpp/security/auth_metadata_processor.h>
#include <grpcpp/security/server_credentials.h>

#include <memory>

namespace grpc {
namespace {
class InsecureServerCredentialsImpl final : public ServerCredentials {
 public:
  InsecureServerCredentialsImpl()
      : ServerCredentials(grpc_insecure_server_credentials_create()) {}
};
}  // namespace

std::shared_ptr<ServerCredentials> InsecureServerCredentials() {
  return std::make_shared<InsecureServerCredentialsImpl>();
}

}  // namespace grpc
