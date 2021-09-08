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

#include <grpcpp/security/server_credentials.h>

#include "src/core/ext/transport/binder/server/binder_server.h"
#include "src/core/ext/transport/binder/wire_format/binder_android.h"

namespace grpc {
namespace experimental {

namespace {

class BinderServerCredentialsImpl final : public ServerCredentials {
 public:
#ifdef GPR_ANDROID
  int AddPortToServer(const std::string& addr, grpc_server* server) override {
    return grpc_core::AddBinderPort(
        std::string(addr), server,
        [](grpc_binder::TransactionReceiver::OnTransactCb transact_cb) {
          return absl::make_unique<grpc_binder::TransactionReceiverAndroid>(
              nullptr, std::move(transact_cb));
        });
  }
#else
  int AddPortToServer(const std::string& /*addr*/,
                      grpc_server* /*server*/) override {
    return 0;
  }
#endif  // GPR_ANDROID

  void SetAuthMetadataProcessor(
      const std::shared_ptr<AuthMetadataProcessor>& /*processor*/) override {
    GPR_ASSERT(false);
  }

 private:
  bool IsInsecure() const override { return true; }
};

}  // namespace

std::shared_ptr<ServerCredentials> BinderServerCredentials() {
  return std::shared_ptr<ServerCredentials>(new BinderServerCredentialsImpl());
}

}  // namespace experimental
}  // namespace grpc
