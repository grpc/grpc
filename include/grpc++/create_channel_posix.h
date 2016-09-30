/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

/// Create a new \a Channel communicating over given file descriptor
///
/// \param target The name of the target.
/// \param fd The file descriptor representing a socket.
std::shared_ptr<Channel> CreateInsecureChannelFromFd(const grpc::string& target,
                                                     int fd);

/// Create a new \a Channel communicating over given file descriptor with custom
/// channel arguments
///
/// \param target The name of the target.
/// \param fd The file descriptor representing a socket.
/// \param args Options for channel creation.
std::shared_ptr<Channel> CreateCustomInsecureChannelFromFd(
    const grpc::string& target, int fd, const ChannelArguments& args);

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD

}  // namespace grpc

#endif  // GRPCXX_CREATE_CHANNEL_POSIX_H
