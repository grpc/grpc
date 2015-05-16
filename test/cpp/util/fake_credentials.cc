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

#include <grpc/grpc_security.h>
#include <grpc++/channel_arguments.h>
#include <grpc++/credentials.h>
#include <grpc++/server_credentials.h>
#include "src/cpp/client/channel.h"
#include "src/cpp/server/secure_server_credentials.h"

namespace grpc {
namespace testing {

namespace {
class FakeCredentialsImpl GRPC_FINAL : public Credentials {
 public:
  FakeCredentialsImpl()
      : c_creds_(grpc_fake_transport_security_credentials_create()) {}
  ~FakeCredentialsImpl() { grpc_credentials_release(c_creds_); }
  SecureCredentials* AsSecureCredentials() GRPC_OVERRIDE { return nullptr; }
  std::shared_ptr<ChannelInterface> CreateChannel(
      const grpc::string& target, const ChannelArguments& args) GRPC_OVERRIDE {
    grpc_channel_args channel_args;
    args.SetChannelArgs(&channel_args);
    return std::shared_ptr<ChannelInterface>(new Channel(
        target,
        grpc_secure_channel_create(c_creds_, target.c_str(), &channel_args)));
  }
  bool ApplyToCall(grpc_call* call) GRPC_OVERRIDE { return false; }

 private:
  grpc_credentials* const c_creds_;
};

}  // namespace

std::shared_ptr<Credentials> FakeCredentials() {
  return std::shared_ptr<Credentials>(new FakeCredentialsImpl());
}

std::shared_ptr<ServerCredentials> FakeServerCredentials() {
  grpc_server_credentials* c_creds =
      grpc_fake_transport_security_server_credentials_create();
  return std::shared_ptr<ServerCredentials>(
      new SecureServerCredentials(c_creds));
}

}  // namespace testing
}  // namespace grpc
