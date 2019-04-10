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

#include <grpcpp/security/credentials.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpcpp/channel.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/config.h>
#include "src/cpp/client/create_channel_internal.h"

namespace grpc {

namespace {
class InsecureChannelCredentialsImpl final : public ChannelCredentials {
 public:
  std::shared_ptr<grpc::Channel> CreateChannel(
      const string& target, const grpc::ChannelArguments& args) override {
    return CreateChannelWithInterceptors(
        target, args,
        std::vector<std::unique_ptr<
            experimental::ClientInterceptorFactoryInterface>>());
  }

  std::shared_ptr<grpc::Channel> CreateChannelWithInterceptors(
      const string& target, const grpc::ChannelArguments& args,
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
          interceptor_creators) override {
    grpc_channel_args channel_args;
    args.SetChannelArgs(&channel_args);
    return CreateChannelInternal(
        "",
        grpc_insecure_channel_create(target.c_str(), &channel_args, nullptr),
        std::move(interceptor_creators));
  }

  SecureChannelCredentials* AsSecureCredentials() override { return nullptr; }
};
}  // namespace

std::shared_ptr<ChannelCredentials> InsecureChannelCredentials() {
  return std::shared_ptr<ChannelCredentials>(
      new InsecureChannelCredentialsImpl());
}

}  // namespace grpc
