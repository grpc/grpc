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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_SERVER_BINDER_SERVER_CREDENTIALS_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_SERVER_BINDER_SERVER_CREDENTIALS_H

#include <grpc/impl/codegen/port_platform.h>

#include <grpcpp/security/server_credentials.h>

#include <memory>

#include "src/core/ext/transport/binder/server/binder_server.h"

namespace grpc {
namespace experimental {

#ifdef GPR_ANDROID

// TODO(waynetu): Move this to <grpcpp/security/server_credentials.h>
std::shared_ptr<ServerCredentials> BinderServerCredentials();

#endif  // GPR_ANDROID

}  // namespace experimental

namespace internal {

template <typename T>
class BinderServerCredentialsImpl final : public ServerCredentials {
 public:
  int AddPortToServer(const std::string& addr, grpc_server* server) override {
    const std::string kBinderUriScheme = "binder:";
    if (addr.compare(0, kBinderUriScheme.size(), kBinderUriScheme) != 0) {
      return 0;
    }
    size_t pos = kBinderUriScheme.size();
    while (pos < addr.size() && addr[pos] == '/') pos++;
    grpc_core::Server* core_server = server->core_server.get();
    core_server->AddListener(
        grpc_core::OrphanablePtr<grpc_core::Server::ListenerInterface>(
            new grpc_core::BinderServerListener<T>(core_server,
                                                   addr.substr(pos))));
    return 1;
  }

  void SetAuthMetadataProcessor(
      const std::shared_ptr<AuthMetadataProcessor>& /*processor*/) override {
    GPR_ASSERT(false);
  }

 private:
  bool IsInsecure() const override { return true; }
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_SERVER_BINDER_SERVER_CREDENTIALS_H
