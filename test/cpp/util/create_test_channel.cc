/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include "test/cpp/util/create_test_channel.h"

#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>
#include <grpc/support/log.h>

#include "test/cpp/util/test_credentials_provider.h"

namespace grpc {

namespace {

const char kProdTlsCredentialsType[] = "prod_ssl";

class SslCredentialProvider : public testing::CredentialTypeProvider {
 public:
  std::shared_ptr<ChannelCredentials> GetChannelCredentials(
      grpc::ChannelArguments* args) override {
    return SslCredentials(SslCredentialsOptions());
  }
  std::shared_ptr<ServerCredentials> GetServerCredentials() override {
    return nullptr;
  }
};

gpr_once g_once_init_add_prod_ssl_provider = GPR_ONCE_INIT;
// Register ssl with non-test roots type to the credentials provider.
void AddProdSslType() {
  testing::GetCredentialsProvider()->AddSecureType(
      kProdTlsCredentialsType, std::unique_ptr<testing::CredentialTypeProvider>(
                                   new SslCredentialProvider));
}

}  // namespace

// When ssl is enabled, if server is empty, override_hostname is used to
// create channel. Otherwise, connect to server and override hostname if
// override_hostname is provided.
// When ssl is not enabled, override_hostname is ignored.
// Set use_prod_root to true to use the SSL root for connecting to google.
// In this case, path to the roots pem file must be set via environment variable
// GRPC_DEFAULT_SSL_ROOTS_FILE_PATH.
// Otherwise, root for test SSL cert will be used.
// creds will be used to create a channel when enable_ssl is true.
// Use examples:
//   CreateTestChannel(
//       "1.1.1.1:12345", "override.hostname.com", true, false, creds);
//   CreateTestChannel("test.google.com:443", "", true, true, creds);
//   same as above
//   CreateTestChannel("", "test.google.com:443", true, true, creds);
std::shared_ptr<Channel> CreateTestChannel(
    const grpc::string& server, const grpc::string& override_hostname,
    bool enable_ssl, bool use_prod_roots,
    const std::shared_ptr<CallCredentials>& creds,
    const ChannelArguments& args) {
  ChannelArguments channel_args(args);
  std::shared_ptr<ChannelCredentials> channel_creds;
  if (enable_ssl) {
    if (use_prod_roots) {
      gpr_once_init(&g_once_init_add_prod_ssl_provider, &AddProdSslType);
      channel_creds = testing::GetCredentialsProvider()->GetChannelCredentials(
          kProdTlsCredentialsType, &channel_args);
      if (!server.empty() && !override_hostname.empty()) {
        channel_args.SetSslTargetNameOverride(override_hostname);
      }
    } else {
      // override_hostname is discarded as the provider handles it.
      channel_creds = testing::GetCredentialsProvider()->GetChannelCredentials(
          testing::kTlsCredentialsType, &channel_args);
    }
    GPR_ASSERT(channel_creds != nullptr);

    const grpc::string& connect_to =
        server.empty() ? override_hostname : server;
    if (creds.get()) {
      channel_creds = CompositeChannelCredentials(channel_creds, creds);
    }
    return CreateCustomChannel(connect_to, channel_creds, channel_args);
  } else {
    return CreateChannel(server, InsecureChannelCredentials());
  }
}

std::shared_ptr<Channel> CreateTestChannel(
    const grpc::string& server, const grpc::string& override_hostname,
    bool enable_ssl, bool use_prod_roots,
    const std::shared_ptr<CallCredentials>& creds) {
  return CreateTestChannel(server, override_hostname, enable_ssl,
                           use_prod_roots, creds, ChannelArguments());
}

std::shared_ptr<Channel> CreateTestChannel(
    const grpc::string& server, const grpc::string& override_hostname,
    bool enable_ssl, bool use_prod_roots) {
  return CreateTestChannel(server, override_hostname, enable_ssl,
                           use_prod_roots, std::shared_ptr<CallCredentials>());
}

// Shortcut for end2end and interop tests.
std::shared_ptr<Channel> CreateTestChannel(const grpc::string& server,
                                           bool enable_ssl) {
  return CreateTestChannel(server, "foo.test.google.fr", enable_ssl, false);
}

std::shared_ptr<Channel> CreateTestChannel(
    const grpc::string& server, const grpc::string& credential_type,
    const std::shared_ptr<CallCredentials>& creds) {
  ChannelArguments channel_args;
  std::shared_ptr<ChannelCredentials> channel_creds =
      testing::GetCredentialsProvider()->GetChannelCredentials(credential_type,
                                                               &channel_args);
  GPR_ASSERT(channel_creds != nullptr);
  if (creds.get()) {
    channel_creds = CompositeChannelCredentials(channel_creds, creds);
  }
  return CreateCustomChannel(server, channel_creds, channel_args);
}

}  // namespace grpc
