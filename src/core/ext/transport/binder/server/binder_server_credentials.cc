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

#include "absl/log/check.h"

#include <grpc/support/port_platform.h>

#ifndef GRPC_NO_BINDER

#include <grpcpp/security/binder_security_policy.h>
#include <grpcpp/security/server_credentials.h>

#include "src/core/ext/transport/binder/server/binder_server.h"
#include "src/core/ext/transport/binder/wire_format/binder_android.h"

namespace grpc {
namespace experimental {

namespace {

class BinderServerCredentialsImpl final : public ServerCredentials {
 public:
  explicit BinderServerCredentialsImpl(
      std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
          security_policy)
      : ServerCredentials(nullptr), security_policy_(security_policy) {}
#ifdef GPR_SUPPORT_BINDER_TRANSPORT
  int AddPortToServer(const std::string& addr, grpc_server* server) override {
    return grpc_core::AddBinderPort(
        std::string(addr), server,
        [](grpc_binder::TransactionReceiver::OnTransactCb transact_cb) {
          return std::make_unique<grpc_binder::TransactionReceiverAndroid>(
              nullptr, std::move(transact_cb));
        },
        security_policy_);
  }
#else
  int AddPortToServer(const std::string& /*addr*/,
                      grpc_server* /*server*/) override {
    return 0;
  }
#endif  // GPR_SUPPORT_BINDER_TRANSPORT

 private:
  std::shared_ptr<grpc::experimental::binder::SecurityPolicy> security_policy_;
};

}  // namespace

std::shared_ptr<ServerCredentials> BinderServerCredentials(
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy) {
  CHECK_NE(security_policy, nullptr);
  return std::shared_ptr<ServerCredentials>(
      new BinderServerCredentialsImpl(security_policy));
}

}  // namespace experimental
}  // namespace grpc
#endif
