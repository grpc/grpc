/*
 *
 * Copyright 2016 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpcpp/channel.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/support/channel_arguments.h>

#include "src/cpp/client/create_channel_internal.h"

namespace grpc {

class ChannelArguments;

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD

std::shared_ptr<Channel> CreateInsecureChannelFromFd(const std::string& target,
                                                     int fd) {
  grpc::internal::GrpcLibrary init_lib;
  init_lib.init();
  return CreateChannelInternal(
      "", grpc_insecure_channel_create_from_fd(target.c_str(), fd, nullptr),
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());
}

std::shared_ptr<Channel> CreateCustomInsecureChannelFromFd(
    const std::string& target, int fd, const grpc::ChannelArguments& args) {
  internal::GrpcLibrary init_lib;
  init_lib.init();
  grpc_channel_args channel_args;
  args.SetChannelArgs(&channel_args);
  return CreateChannelInternal(
      "",
      grpc_insecure_channel_create_from_fd(target.c_str(), fd, &channel_args),
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());
}

namespace experimental {

std::shared_ptr<Channel> CreateCustomInsecureChannelWithInterceptorsFromFd(
    const std::string& target, int fd, const ChannelArguments& args,
    std::vector<
        std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>
        interceptor_creators) {
  grpc::internal::GrpcLibrary init_lib;
  init_lib.init();
  grpc_channel_args channel_args;
  args.SetChannelArgs(&channel_args);
  return CreateChannelInternal(
      "",
      grpc_insecure_channel_create_from_fd(target.c_str(), fd, &channel_args),
      std::move(interceptor_creators));
}

}  // namespace experimental

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD

}  // namespace grpc
