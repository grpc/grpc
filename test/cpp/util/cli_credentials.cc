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

#include "test/cpp/util/cli_credentials.h"

#include <gflags/gflags.h>

DEFINE_bool(
    enable_ssl, false,
    "Whether to use ssl/tls. Deprecated. Use --channel_creds_type=ssl.");
DEFINE_bool(use_auth, false,
            "Whether to create default google credentials. Deprecated. Use "
            "--channel_creds_type=gdc.");
DEFINE_string(
    access_token, "",
    "The access token that will be sent to the server to authenticate RPCs.");
DEFINE_string(
    ssl_target, "",
    "If not empty, treat the server host name as this for ssl/tls certificate "
    "validation.");
DEFINE_string(
    channel_creds_type, "",
    "The channel creds type: insecure, ssl, gdc (Google Default Credentials) "
    "or alts.");

namespace grpc {
namespace testing {

grpc::string CliCredentials::GetDefaultChannelCredsType() const {
  // Compatibility logic for --enable_ssl.
  if (FLAGS_enable_ssl) {
    fprintf(stderr,
            "warning: --enable_ssl is deprecated. Use "
            "--channel_creds_type=ssl.\n");
    return "ssl";
  }
  // Compatibility logic for --use_auth.
  if (FLAGS_access_token.empty() && FLAGS_use_auth) {
    fprintf(stderr,
            "warning: --use_auth is deprecated. Use "
            "--channel_creds_type=gdc.\n");
    return "gdc";
  }
  return "insecure";
}

std::shared_ptr<grpc::ChannelCredentials>
CliCredentials::GetChannelCredentials() const {
  if (FLAGS_channel_creds_type.compare("insecure") == 0) {
    return grpc::InsecureChannelCredentials();
  } else if (FLAGS_channel_creds_type.compare("ssl") == 0) {
    return grpc::SslCredentials(grpc::SslCredentialsOptions());
  } else if (FLAGS_channel_creds_type.compare("gdc") == 0) {
    return grpc::GoogleDefaultCredentials();
  } else if (FLAGS_channel_creds_type.compare("alts") == 0) {
    return grpc::experimental::AltsCredentials(
        grpc::experimental::AltsCredentialsOptions());
  }
  fprintf(stderr,
          "--channel_creds_type=%s invalid; must be insecure, ssl, gdc or "
          "alts.\n",
          FLAGS_channel_creds_type.c_str());
  return std::shared_ptr<grpc::ChannelCredentials>();
}

std::shared_ptr<grpc::CallCredentials> CliCredentials::GetCallCredentials()
    const {
  if (!FLAGS_access_token.empty()) {
    if (FLAGS_use_auth) {
      fprintf(stderr,
              "warning: use_auth is ignored when access_token is provided.");
    }
    return grpc::AccessTokenCredentials(FLAGS_access_token);
  }
  return std::shared_ptr<grpc::CallCredentials>();
}

std::shared_ptr<grpc::ChannelCredentials> CliCredentials::GetCredentials()
    const {
  if (FLAGS_channel_creds_type.empty()) {
    FLAGS_channel_creds_type = GetDefaultChannelCredsType();
  } else if (FLAGS_enable_ssl && FLAGS_channel_creds_type.compare("ssl") != 0) {
    fprintf(stderr,
            "warning: ignoring --enable_ssl because "
            "--channel_creds_type already set to %s.\n",
            FLAGS_channel_creds_type.c_str());
  } else if (FLAGS_use_auth && FLAGS_channel_creds_type.compare("gdc") != 0) {
    fprintf(stderr,
            "warning: ignoring --use_auth because "
            "--channel_creds_type already set to %s.\n",
            FLAGS_channel_creds_type.c_str());
  }
  // Legacy transport upgrade logic for insecure requests.
  if (!FLAGS_access_token.empty() &&
      FLAGS_channel_creds_type.compare("insecure") == 0) {
    fprintf(stderr,
            "warning: --channel_creds_type=insecure upgraded to ssl because "
            "an access token was provided.\n");
    FLAGS_channel_creds_type = "ssl";
  }
  std::shared_ptr<grpc::ChannelCredentials> channel_creds =
      GetChannelCredentials();
  // Composite any call-type credentials on top of the base channel.
  std::shared_ptr<grpc::CallCredentials> call_creds = GetCallCredentials();
  return (channel_creds == nullptr || call_creds == nullptr)
             ? channel_creds
             : grpc::CompositeChannelCredentials(channel_creds, call_creds);
}

const grpc::string CliCredentials::GetCredentialUsage() const {
  return "    --enable_ssl             ; Set whether to use ssl (deprecated)\n"
         "    --use_auth               ; Set whether to create default google"
         " credentials\n"
         "    --access_token           ; Set the access token in metadata,"
         " overrides --use_auth\n"
         "    --ssl_target             ; Set server host for ssl validation\n"
         "    --channel_creds_type     ; Set to insecure, ssl, gdc, or alts\n";
}

const grpc::string CliCredentials::GetSslTargetNameOverride() const {
  bool use_ssl = FLAGS_channel_creds_type.compare("ssl") == 0 ||
                 FLAGS_channel_creds_type.compare("gdc") == 0;
  return use_ssl ? FLAGS_ssl_target : "";
}

}  // namespace testing
}  // namespace grpc
