//
//
// Copyright 2025 gRPC authors.
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
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/secure_posix.h>
#include <grpcpp/channel.h>
#include <grpcpp/security/credentials.h>

#include <memory>
namespace grpc::experimental {

std::shared_ptr<Channel> CreateChannelFromEndpoint(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint,
    const std::shared_ptr<ChannelCredentials>& creds,
    const ChannelArguments& args) {
  grpc_channel_args channel_args = args.c_channel_args();
  grpc_channel_credentials* creds_ = creds->c_creds();
  auto channel = CreateChannelInternal(
      "",
      grpc_core::experimental::CreateChannelFromEndpoint(std::move(endpoint),
                                                         creds_, &channel_args),
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());
  grpc_channel_credentials_release(creds_);
  return channel;
}

std::shared_ptr<Channel> CreateChannelFromFd(
    int fd, const std::shared_ptr<ChannelCredentials>& creds,
    const ChannelArguments& args) {
  grpc_channel_args channel_args = args.c_channel_args();
  grpc_channel_credentials* creds_ = creds->c_creds();
  auto channel = CreateChannelInternal(
      "",
      grpc_core::experimental::CreateChannelFromFd(fd, creds_, &channel_args),
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());
  grpc_channel_credentials_release(creds_);
  return channel;
}

}  // namespace grpc::experimental
