/*
 *
 * Copyright 2015-2016 gRPC authors.
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

// When cred_type is 'ssl', if server is empty, override_hostname is used to
// create channel. Otherwise, connect to server and override hostname if
// override_hostname is provided.
// When cred_type is not 'ssl', override_hostname is ignored.
// Set use_prod_root to true to use the SSL root for connecting to google.
// In this case, path to the roots pem file must be set via environment variable
// GRPC_DEFAULT_SSL_ROOTS_FILE_PATH.
// Otherwise, root for test SSL cert will be used.
// creds will be used to create a channel when cred_type is 'ssl'.
// Use examples:
//   CreateTestChannel(
//       "1.1.1.1:12345", "ssl", "override.hostname.com", false, creds);
//   CreateTestChannel("test.google.com:443", "ssl", "", true, creds);
//   same as above
//   CreateTestChannel("", "ssl", "test.google.com:443", true, creds);
std::shared_ptr<Channel> CreateTestChannel(
    const grpc::string& server, const grpc::string& cred_type,
    const grpc::string& override_hostname, bool use_prod_roots,
    const std::shared_ptr<CallCredentials>& creds,
    const ChannelArguments& args) {
  ChannelArguments channel_args(args);
  std::shared_ptr<ChannelCredentials> channel_creds;
  if (cred_type.empty()) {
    return CreateCustomChannel(server, InsecureChannelCredentials(), args);
  } else if (cred_type == testing::kTlsCredentialsType) {  // cred_type == "ssl"
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
    channel_creds = testing::GetCredentialsProvider()->GetChannelCredentials(
        cred_type, &channel_args);
    GPR_ASSERT(channel_creds != nullptr);

    return CreateCustomChannel(server, channel_creds, args);
  }
}

std::shared_ptr<Channel> CreateTestChannel(
    const grpc::string& server, const grpc::string& override_hostname,
    bool enable_ssl, bool use_prod_roots,
    const std::shared_ptr<CallCredentials>& creds,
    const ChannelArguments& args) {
  grpc::string type;
  if (enable_ssl) {
    type = testing::kTlsCredentialsType;
  }

  return CreateTestChannel(server, type, override_hostname, use_prod_roots,
                           creds, args);
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
