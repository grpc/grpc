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

#ifndef GRPCXX_CREATE_CHANNEL_POSIX_H
#define GRPCXX_CREATE_CHANNEL_POSIX_H

#include <memory>

#include <grpc++/channel.h>
#include <grpc++/support/channel_arguments.h>
#include <grpc/support/port_platform.h>

namespace grpc {

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD

/// Create a new \a Channel communicating over the given file descriptor.
///
/// \param target The name of the target.
/// \param fd The file descriptor representing a socket.
std::shared_ptr<Channel> CreateInsecureChannelFromFd(const grpc::string& target,
                                                     int fd);

/// Create a new \a Channel communicating over given file descriptor with custom
/// channel arguments.
///
/// \param target The name of the target.
/// \param fd The file descriptor representing a socket.
/// \param args Options for channel creation.
std::shared_ptr<Channel> CreateCustomInsecureChannelFromFd(
    const grpc::string& target, int fd, const ChannelArguments& args);

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD

}  // namespace grpc

#endif  // GRPCXX_CREATE_CHANNEL_POSIX_H
