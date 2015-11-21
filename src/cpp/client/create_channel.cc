/*
 *
 * Copyright 2015, Google Inc.
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

#include <memory>
#include <sstream>

#include <grpc++/channel.h>
#include <grpc++/create_channel.h>
#include <grpc++/support/channel_arguments.h>

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
  GrpcLibrary init_lib;  // We need to call init in case of a bad creds.
  ChannelArguments cp_args = args;
  std::ostringstream user_agent_prefix;
  user_agent_prefix << "grpc-c++/" << grpc_version_string();
  cp_args.SetString(GRPC_ARG_PRIMARY_USER_AGENT_STRING,
                    user_agent_prefix.str());
  return creds
             ? creds->CreateChannel(target, cp_args)
             : CreateChannelInternal("", grpc_lame_client_channel_create(
                                             NULL, GRPC_STATUS_INVALID_ARGUMENT,
                                             "Invalid credentials."));
}

}  // namespace grpc
