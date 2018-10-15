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

#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/support/channel_arguments.h>

#include "src/cpp/client/create_channel_internal.h"

namespace grpc {
class ChannelArguments;

std::shared_ptr<Channel> CreateChannel(
    const grpc::string& target,
    const std::shared_ptr<ChannelCredentials>& creds) {
  return CreateCustomChannel(target, creds, ChannelArguments());
}

std::shared_ptr<Channel> CreateCustomChannel(
    const grpc::string& target,
    const std::shared_ptr<ChannelCredentials>& creds,
    const ChannelArguments& args) {
  GrpcLibraryCodegen init_lib;  // We need to call init in case of a bad creds.
  return creds
             ? creds->CreateChannel(target, args)
             : CreateChannelInternal("",
                                     grpc_lame_client_channel_create(
                                         nullptr, GRPC_STATUS_INVALID_ARGUMENT,
                                         "Invalid credentials."),
                                     nullptr);
}

namespace experimental {
/// Create a new \em custom \a Channel pointing to \a target with \a
/// interceptors being invoked per call.
///
/// \warning For advanced use and testing ONLY. Override default channel
/// arguments only if necessary.
///
/// \param target The URI of the endpoint to connect to.
/// \param creds Credentials to use for the created channel. If it does not
/// hold an object or is invalid, a lame channel (one on which all operations
/// fail) is returned.
/// \param args Options for channel creation.
std::shared_ptr<Channel> CreateCustomChannelWithInterceptors(
    const grpc::string& target,
    const std::shared_ptr<ChannelCredentials>& creds,
    const ChannelArguments& args,
    std::unique_ptr<std::vector<
        std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>>
        interceptor_creators) {
  return creds
             ? creds->CreateChannelWithInterceptors(
                   target, args, std::move(interceptor_creators))
             : CreateChannelInternal("",
                                     grpc_lame_client_channel_create(
                                         nullptr, GRPC_STATUS_INVALID_ARGUMENT,
                                         "Invalid credentials."),
                                     nullptr);
}
}  // namespace experimental

}  // namespace grpc
