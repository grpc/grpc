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

#include <grpc++/channel.h>
#include <grpc++/create_channel.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc++/support/channel_arguments.h>
#include <grpc/support/log.h>

#include "src/cpp/client/create_channel_internal.h"

namespace grpc {
class ChannelArguments;

namespace {
class DefaultGlobalCallbacks final : public CreateChannelGlobalCallbacks {
 public:
  ~DefaultGlobalCallbacks() override {}
  void UpdateArguments(ChannelArguments* args) override {}
};
}  // namespace

static DefaultGlobalCallbacks g_default_create_channel_callbacks;
static CreateChannelGlobalCallbacks* g_create_channel_callbacks =
    &g_default_create_channel_callbacks;

std::shared_ptr<Channel> CreateChannel(
    const grpc::string& target,
    const std::shared_ptr<ChannelCredentials>& creds) {
  return CreateCustomChannel(target, creds, ChannelArguments());
}

std::shared_ptr<Channel> CreateCustomChannel(
    const grpc::string& target,
    const std::shared_ptr<ChannelCredentials>& creds,
    const ChannelArguments& args) {
  internal::GrpcLibrary
      init_lib;  // We need to call init in case of a bad creds.
  ChannelArguments new_args(args);
  g_create_channel_callbacks->UpdateArguments(&new_args);
  return creds
             ? creds->CreateChannel(target, new_args)
             : CreateChannelInternal("", grpc_lame_client_channel_create(
                                             NULL, GRPC_STATUS_INVALID_ARGUMENT,
                                             "Invalid credentials."));
}

void SetCreateChannelGlobalCallbacks(
    CreateChannelGlobalCallbacks* create_channel_callbacks) {
  GPR_ASSERT(g_create_channel_callbacks == &g_default_create_channel_callbacks);
  GPR_ASSERT(create_channel_callbacks != NULL);
  GPR_ASSERT(create_channel_callbacks != &g_default_create_channel_callbacks);
  g_create_channel_callbacks = create_channel_callbacks;
}

}  // namespace grpc
