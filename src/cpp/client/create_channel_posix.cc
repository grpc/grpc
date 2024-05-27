//
//
// Copyright 2016 gRPC authors.
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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/grpc_security.h>
#include <grpcpp/channel.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/client_interceptor.h>

#include "src/cpp/client/create_channel_internal.h"

namespace grpc {

class ChannelArguments;

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD

std::shared_ptr<Channel> CreateInsecureChannelFromFd(const std::string& target,
                                                     int fd) {
  internal::GrpcLibrary init_lib;
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  auto channel = CreateChannelInternal(
      "", grpc_channel_create_from_fd(target.c_str(), fd, creds, nullptr),
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());
  grpc_channel_credentials_release(creds);
  return channel;
}

std::shared_ptr<Channel> CreateCustomInsecureChannelFromFd(
    const std::string& target, int fd, const grpc::ChannelArguments& args) {
  internal::GrpcLibrary init_lib;
  grpc_channel_args channel_args;
  args.SetChannelArgs(&channel_args);
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  auto channel = CreateChannelInternal(
      "", grpc_channel_create_from_fd(target.c_str(), fd, creds, &channel_args),
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());
  grpc_channel_credentials_release(creds);
  // Channel also initializes gRPC, so we can decrement the init ref count here.
  return channel;
}

namespace experimental {

std::shared_ptr<Channel> CreateCustomInsecureChannelWithInterceptorsFromFd(
    const std::string& target, int fd, const ChannelArguments& args,
    std::vector<
        std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>
        interceptor_creators) {
  internal::GrpcLibrary init_lib;
  grpc_channel_args channel_args;
  args.SetChannelArgs(&channel_args);
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  auto channel = CreateChannelInternal(
      "", grpc_channel_create_from_fd(target.c_str(), fd, creds, &channel_args),
      std::move(interceptor_creators));
  grpc_channel_credentials_release(creds);
  return channel;
}

}  // namespace experimental

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD

}  // namespace grpc
