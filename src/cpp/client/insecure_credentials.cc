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

#include <string>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include <grpc++/channel_arguments.h>
#include <grpc++/config.h>
#include <grpc++/credentials.h>
#include "src/cpp/client/channel.h"

namespace grpc {

namespace {
class InsecureCredentialsImpl GRPC_FINAL : public Credentials {
 public:
  std::shared_ptr<grpc::ChannelInterface> CreateChannel(
      const string& target, const grpc::ChannelArguments& args) GRPC_OVERRIDE {
    grpc_channel_args channel_args;
    args.SetChannelArgs(&channel_args);
    return std::shared_ptr<ChannelInterface>(new Channel(
        target, grpc_channel_create(target.c_str(), &channel_args)));
  }

  SecureCredentials* AsSecureCredentials() { return nullptr; }
};
}  // namespace

std::unique_ptr<Credentials> InsecureCredentials() {
  return std::unique_ptr<Credentials>(new InsecureCredentialsImpl());
}

}  // namespace grpc
