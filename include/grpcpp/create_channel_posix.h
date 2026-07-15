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

#ifndef GRPCPP_CREATE_CHANNEL_POSIX_H
#define GRPCPP_CREATE_CHANNEL_POSIX_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>
#include <grpcpp/channel.h>
#include <grpcpp/support/channel_arguments.h>

#include <memory>

namespace grpc {

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD

/// Create a new \a Channel communicating over the given file descriptor.
///
/// \param target The name of the target.
/// \param fd The file descriptor representing a socket.
std::shared_ptr<grpc::Channel> CreateInsecureChannelFromFd(
    const std::string& target, int fd);

/// Create a new \a Channel communicating over given file descriptor with custom
/// channel arguments.
///
/// \param target The name of the target.
/// \param fd The file descriptor representing a socket.
/// \param args Options for channel creation.
std::shared_ptr<grpc::Channel> CreateCustomInsecureChannelFromFd(
    const std::string& target, int fd, const grpc::ChannelArguments& args);

namespace experimental {

/// Create a new \a Channel communicating over given file descriptor with custom
/// channel arguments.
///
/// \param target The name of the target.
/// \param fd The file descriptor representing a socket.
/// \param args Options for channel creation.
/// \param interceptor_creators Vector of interceptor factory objects.
std::shared_ptr<grpc::Channel>
CreateCustomInsecureChannelWithInterceptorsFromFd(
    const std::string& target, int fd, const grpc::ChannelArguments& args,
    std::vector<
        std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>
        interceptor_creators);

/// Creates a new \a Channel from a file descriptor.
/// The channel target will be hard-coded to something like "ipv4:127.0.0.1:80".
/// The default authority will be "unknown", but the application can override it
/// using the GRPC_ARG_DEFAULT_AUTHORITY channel argument.
/// This API supports both secure and insecure channel credentials.
///
/// \param fd The file descriptor representing the connection.
/// \param creds The channel credentials used to secure the connection.
/// \param args Channel arguments used to configure the channel behavior.
std::shared_ptr<grpc::Channel> CreateChannelFromFd(
    int fd, const std::shared_ptr<ChannelCredentials>& creds,
    const ChannelArguments& args);

}  // namespace experimental

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD

namespace experimental {

/// Creates a new \a Channel from an EventEngine endpoint.
/// The channel target will be hard-coded to something like "ipv4:127.0.0.1:80".
/// The default authority will be set to the endpoint's peer address, but the
/// application can override it using the GRPC_ARG_DEFAULT_AUTHORITY channel
/// argument. This API supports both secure and insecure channel credentials.
///
/// \param endpoint A unique pointer to an EventEngine endpoint representing
///        an established connection.
/// \param creds The channel credentials used to secure the connection.
/// \param args Channel arguments used to configure the channel behavior.
std::shared_ptr<grpc::Channel> CreateChannelFromEndpoint(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint,
    const std::shared_ptr<ChannelCredentials>& creds,
    const ChannelArguments& args);

}  // namespace experimental

}  // namespace grpc

#endif  // GRPCPP_CREATE_CHANNEL_POSIX_H
