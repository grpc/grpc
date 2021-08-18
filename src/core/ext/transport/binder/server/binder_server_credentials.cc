// Copyright 2021 gRPC authors.
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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/ext/transport/binder/server/binder_server_credentials.h"

#include <grpcpp/security/server_credentials.h>

#ifdef GPR_ANDROID

namespace grpc {
namespace experimental {

std::shared_ptr<ServerCredentials> BinderServerCredentials() {
  return std::shared_ptr<ServerCredentials>(
      new grpc::internal::BinderServerCredentialsImpl<
          grpc_binder::TransactionReceiverAndroid>());
}

}  // namespace experimental
}  // namespace grpc

#endif  // GPR_ANDROID
