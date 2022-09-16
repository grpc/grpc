/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpcpp/channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/client_interceptor.h>
#include <grpcpp/support/config.h>

#include "src/cpp/client/create_channel_internal.h"

namespace grpc {

namespace {
class InsecureChannelCredentialsImpl final : public ChannelCredentials {
 public:
  std::shared_ptr<Channel> CreateChannelImpl(
      const std::string& target, const ChannelArguments& args) override {
    return CreateChannelWithInterceptors(
        target, args,
        std::vector<std::unique_ptr<
            grpc::experimental::ClientInterceptorFactoryInterface>>());
  }

  std::shared_ptr<Channel> CreateChannelWithInterceptors(
      const std::string& target, const ChannelArguments& args,
      std::vector<std::unique_ptr<
          grpc::experimental::ClientInterceptorFactoryInterface>>
          interceptor_creators) override {
    grpc_channel_args channel_args;
    args.SetChannelArgs(&channel_args);
    grpc_channel_credentials* creds = grpc_insecure_credentials_create();
    std::shared_ptr<Channel> channel = grpc::CreateChannelInternal(
        "", grpc_channel_create(target.c_str(), creds, &channel_args),
        std::move(interceptor_creators));
    grpc_channel_credentials_release(creds);
    return channel;
  }

  SecureChannelCredentials* AsSecureCredentials() override { return nullptr; }

 private:
  bool IsInsecure() const override { return true; }
};
}  // namespace

std::shared_ptr<ChannelCredentials> InsecureChannelCredentials() {
  return std::shared_ptr<ChannelCredentials>(
      new InsecureChannelCredentialsImpl());
}

}  // namespace grpc
