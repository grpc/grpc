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

#include "src/cpp/ext/chaotic_good.h"

#include <memory>

#include <grpc/grpc.h>

#include "src/core/ext/transport/chaotic_good/client/chaotic_good_connector.h"
#include "src/core/ext/transport/chaotic_good/server/chaotic_good_server.h"
#include "src/core/lib/gprpp/crash.h"

namespace grpc {

namespace {

class ChaoticGoodInsecureChannelCredentialsImpl final
    : public ChannelCredentials {
 public:
  std::shared_ptr<Channel> CreateChannelImpl(
      const grpc::string& target, const grpc::ChannelArguments& args) override {
    return CreateChannelWithInterceptors(target, args, {});
  }

  std::shared_ptr<Channel> CreateChannelWithInterceptors(
      const grpc::string& target, const grpc::ChannelArguments& args,
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
          interceptor_creators) override {
    grpc_channel_args channel_args;
    args.SetChannelArgs(&channel_args);
    auto channel = grpc::CreateChannelInternal(
        "", grpc_chaotic_good_channel_create(target.c_str(), &channel_args),
        std::move(interceptor_creators));
    grpc_channel_args_destroy(&channel_args);
    return channel;
  }

  SecureChannelCredentials* AsSecureCredentials() override { return nullptr; }

 private:
  bool IsInsecure() const override { return true; }
};

class ChaoticGoodInsecureServerCredentialsImpl final
    : public ServerCredentials {
 public:
  int AddPortToServer(const std::string& addr, grpc_server* server) override {
    return grpc_server_add_chaotic_good_port(server, addr.c_str());
  }

  void SetAuthMetadataProcessor(
      const std::shared_ptr<AuthMetadataProcessor>&) override {
    grpc_core::Crash("Not supported on insecure server credentials");
  }
};

}  // namespace

std::shared_ptr<ChannelCredentials> ChaoticGoodInsecureChannelCredentials() {
  return std::make_shared<ChaoticGoodInsecureChannelCredentialsImpl>();
}

std::shared_ptr<ServerCredentials> ChaoticGoodInsecureServerCredentials() {
  return std::make_shared<ChaoticGoodInsecureServerCredentialsImpl>();
}

}  // namespace grpc
