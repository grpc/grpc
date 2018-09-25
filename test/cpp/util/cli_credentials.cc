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
#include <grpc/slice.h>
#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/slice.h>

#include "src/core/lib/iomgr/load_file.h"

DEFINE_bool(
    enable_ssl, false,
    "Whether to use ssl/tls. Deprecated. Use --channel_creds_type=ssl.");
DEFINE_bool(use_auth, false,
            "Whether to create default google credentials. Deprecated. Use "
            "--channel_creds_type=gdc.");
DEFINE_string(
    access_token, "",
    "The access token that will be sent to the server to authenticate RPCs. "
    "Deprecated. Use --call_creds=access_token=<token>.");
DEFINE_string(
    ssl_target, "",
    "If not empty, treat the server host name as this for ssl/tls certificate "
    "validation.");
DEFINE_string(
    ssl_client_cert, "",
    "If not empty, load this PEM formated client certificate file. Requires "
    "use of --ssl_client_key.");
DEFINE_string(
    ssl_client_key, "",
    "If not empty, load this PEM formated private key. Requires use of "
    "--ssl_client_cert");
DEFINE_string(
    channel_creds_type, "",
    "The channel creds type: insecure, ssl, gdc (Google Default Credentials) "
    "or alts.");
DEFINE_string(
    call_creds, "",
    "Call credentials to use: none (default), or access_token=<token>. If "
    "provided, the call creds are composited on top of channel creds.");

namespace grpc {
namespace testing {

namespace {

const char ACCESS_TOKEN_PREFIX[] = "access_token=";
constexpr int ACCESS_TOKEN_PREFIX_LEN =
    sizeof(ACCESS_TOKEN_PREFIX) / sizeof(*ACCESS_TOKEN_PREFIX) - 1;

bool IsAccessToken(const grpc::string& auth) {
  return auth.length() > ACCESS_TOKEN_PREFIX_LEN &&
         auth.compare(0, ACCESS_TOKEN_PREFIX_LEN, ACCESS_TOKEN_PREFIX) == 0;
}

grpc::string AccessToken(const grpc::string& auth) {
  if (!IsAccessToken(auth)) {
    return "";
  }
  return grpc::string(auth, ACCESS_TOKEN_PREFIX_LEN);
}

}  // namespace

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

grpc::string CliCredentials::GetDefaultCallCreds() const {
  if (!FLAGS_access_token.empty()) {
    fprintf(stderr,
            "warning: --access_token is deprecated. Use "
            "--call_creds=access_token=<token>.\n");
    return grpc::string("access_token=") + FLAGS_access_token;
  }
  return "none";
}

std::shared_ptr<grpc::ChannelCredentials>
CliCredentials::GetChannelCredentials() const {
  if (FLAGS_channel_creds_type.compare("insecure") == 0) {
    return grpc::InsecureChannelCredentials();
  } else if (FLAGS_channel_creds_type.compare("ssl") == 0) {
    grpc::SslCredentialsOptions ssl_creds_options;
    // TODO(@Capstan): This won't affect Google Default Credentials using SSL.
    if (!FLAGS_ssl_client_cert.empty()) {
      grpc_slice cert_slice = grpc_empty_slice();
      GRPC_LOG_IF_ERROR(
          "load_file",
          grpc_load_file(FLAGS_ssl_client_cert.c_str(), 1, &cert_slice));
      ssl_creds_options.pem_cert_chain =
          grpc::StringFromCopiedSlice(cert_slice);
      grpc_slice_unref(cert_slice);
    }
    if (!FLAGS_ssl_client_key.empty()) {
      grpc_slice key_slice = grpc_empty_slice();
      GRPC_LOG_IF_ERROR(
          "load_file",
          grpc_load_file(FLAGS_ssl_client_key.c_str(), 1, &key_slice));
      ssl_creds_options.pem_private_key =
          grpc::StringFromCopiedSlice(key_slice);
      grpc_slice_unref(key_slice);
    }
    return grpc::SslCredentials(ssl_creds_options);
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
  if (IsAccessToken(FLAGS_call_creds)) {
    return grpc::AccessTokenCredentials(AccessToken(FLAGS_call_creds));
  }
  if (FLAGS_call_creds.compare("none") != 0) {
    // Nothing to do; creds, if any, are baked into the channel.
    return std::shared_ptr<grpc::CallCredentials>();
  }
  fprintf(stderr,
          "--call_creds=%s invalid; must be none "
          "or access_token=<token>.\n",
          FLAGS_call_creds.c_str());
  return std::shared_ptr<grpc::CallCredentials>();
}

std::shared_ptr<grpc::ChannelCredentials> CliCredentials::GetCredentials()
    const {
  if (FLAGS_call_creds.empty()) {
    FLAGS_call_creds = GetDefaultCallCreds();
  } else if (!FLAGS_access_token.empty() && !IsAccessToken(FLAGS_call_creds)) {
    fprintf(stderr,
            "warning: ignoring --access_token because --call_creds "
            "already set to %s.\n",
            FLAGS_call_creds.c_str());
  }
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
  if (IsAccessToken(FLAGS_call_creds) &&
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
         "                             ; (deprecated)\n"
         "    --access_token           ; Set the access token in metadata,"
         " overrides --use_auth\n"
         "                             ; (deprecated)\n"
         "    --ssl_target             ; Set server host for ssl validation\n"
         "    --ssl_client_cert        ; Client cert for ssl\n"
         "    --ssl_client_key         ; Client private key for ssl\n"
         "    --channel_creds_type     ; Set to insecure, ssl, gdc, or alts\n"
         "    --call_creds             ; Set to none, or"
         " access_token=<token>\n";
}

const grpc::string CliCredentials::GetSslTargetNameOverride() const {
  bool use_ssl = FLAGS_channel_creds_type.compare("ssl") == 0 ||
                 FLAGS_channel_creds_type.compare("gdc") == 0;
  return use_ssl ? FLAGS_ssl_target : "";
}

}  // namespace testing
}  // namespace grpc
