//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "test/cpp/util/cli_credentials.h"

#include <grpc/slice.h>
#include <grpcpp/support/slice.h>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "src/core/util/crash.h"
#include "src/core/util/load_file.h"

ABSL_RETIRED_FLAG(bool, enable_ssl, false,
                  "Replaced by --channel_creds_type=ssl.");
ABSL_RETIRED_FLAG(bool, use_auth, false,
                  "Replaced by --channel_creds_type=gdc.");
ABSL_RETIRED_FLAG(std::string, access_token, "",
                  "Replaced by --call_creds=access_token=<token>.");
ABSL_FLAG(
    std::string, ssl_target, "",
    "If not empty, treat the server host name as this for ssl/tls certificate "
    "validation.");
ABSL_FLAG(
    std::string, ssl_client_cert, "",
    "If not empty, load this PEM formatted client certificate file. Requires "
    "use of --ssl_client_key.");
ABSL_FLAG(std::string, ssl_client_key, "",
          "If not empty, load this PEM formatted private key. Requires use of "
          "--ssl_client_cert");
ABSL_FLAG(
    std::string, local_connect_type, "local_tcp",
    "The type of local connections for which local channel credentials will "
    "be applied. Should be local_tcp or uds.");
ABSL_FLAG(
    std::string, channel_creds_type, "",
    "The channel creds type: insecure, ssl, gdc (Google Default Credentials), "
    "alts, or local.");
ABSL_FLAG(
    std::string, call_creds, "",
    "Call credentials to use: none (default), or access_token=<token>. If "
    "provided, the call creds are composited on top of channel creds.");

namespace grpc {
namespace testing {

namespace {

const char ACCESS_TOKEN_PREFIX[] = "access_token=";
constexpr int ACCESS_TOKEN_PREFIX_LEN =
    (sizeof(ACCESS_TOKEN_PREFIX) / sizeof(*ACCESS_TOKEN_PREFIX)) - 1;

bool IsAccessToken(const std::string& auth) {
  return auth.length() > ACCESS_TOKEN_PREFIX_LEN &&
         auth.compare(0, ACCESS_TOKEN_PREFIX_LEN, ACCESS_TOKEN_PREFIX) == 0;
}

std::string AccessToken(const std::string& auth) {
  if (!IsAccessToken(auth)) {
    return "";
  }
  return std::string(auth, ACCESS_TOKEN_PREFIX_LEN);
}

}  // namespace

std::string CliCredentials::GetDefaultChannelCredsType() const {
  return "insecure";
}

std::string CliCredentials::GetDefaultCallCreds() const { return "none"; }

std::shared_ptr<grpc::ChannelCredentials>
CliCredentials::GetChannelCredentials() const {
  if (absl::GetFlag(FLAGS_channel_creds_type) == "insecure") {
    return grpc::InsecureChannelCredentials();
  } else if (absl::GetFlag(FLAGS_channel_creds_type) == "ssl") {
    grpc::SslCredentialsOptions ssl_creds_options;
    // TODO(@Capstan): This won't affect Google Default Credentials using SSL.
    if (!absl::GetFlag(FLAGS_ssl_client_cert).empty()) {
      auto cert = grpc_core::LoadFile(absl::GetFlag(FLAGS_ssl_client_cert),
                                      /*add_null_terminator=*/false);
      if (!cert.ok()) {
        LOG(ERROR) << "error loading file "
                   << absl::GetFlag(FLAGS_ssl_client_cert) << ": "
                   << cert.status();
      } else {
        ssl_creds_options.pem_cert_chain = std::string(cert->as_string_view());
      }
    }
    if (!absl::GetFlag(FLAGS_ssl_client_key).empty()) {
      auto key = grpc_core::LoadFile(absl::GetFlag(FLAGS_ssl_client_key),
                                     /*add_null_terminator=*/false);
      if (!key.ok()) {
        LOG(ERROR) << "error loading file "
                   << absl::GetFlag(FLAGS_ssl_client_key) << ": "
                   << key.status();
      } else {
        ssl_creds_options.pem_private_key = std::string(key->as_string_view());
      }
    }
    return grpc::SslCredentials(ssl_creds_options);
  } else if (absl::GetFlag(FLAGS_channel_creds_type) == "gdc") {
    return grpc::GoogleDefaultCredentials();
  } else if (absl::GetFlag(FLAGS_channel_creds_type) == "alts") {
    return grpc::experimental::AltsCredentials(
        grpc::experimental::AltsCredentialsOptions());
  } else if (absl::GetFlag(FLAGS_channel_creds_type) == "local") {
    if (absl::GetFlag(FLAGS_local_connect_type) == "local_tcp") {
      return grpc::experimental::LocalCredentials(LOCAL_TCP);
    } else if (absl::GetFlag(FLAGS_local_connect_type) == "uds") {
      return grpc::experimental::LocalCredentials(UDS);
    } else {
      fprintf(stderr,
              "--local_connect_type=%s invalid; must be local_tcp or uds.\n",
              absl::GetFlag(FLAGS_local_connect_type).c_str());
    }
  }
  fprintf(stderr,
          "--channel_creds_type=%s invalid; must be insecure, ssl, gdc, "
          "alts, or local.\n",
          absl::GetFlag(FLAGS_channel_creds_type).c_str());
  return std::shared_ptr<grpc::ChannelCredentials>();
}

std::shared_ptr<grpc::CallCredentials> CliCredentials::GetCallCredentials()
    const {
  if (IsAccessToken(absl::GetFlag(FLAGS_call_creds))) {
    return grpc::AccessTokenCredentials(
        AccessToken(absl::GetFlag(FLAGS_call_creds)));
  }
  if (absl::GetFlag(FLAGS_call_creds) == "none") {
    // Nothing to do; creds, if any, are baked into the channel.
    return std::shared_ptr<grpc::CallCredentials>();
  }
  fprintf(stderr,
          "--call_creds=%s invalid; must be none "
          "or access_token=<token>.\n",
          absl::GetFlag(FLAGS_call_creds).c_str());
  return std::shared_ptr<grpc::CallCredentials>();
}

std::shared_ptr<grpc::ChannelCredentials> CliCredentials::GetCredentials()
    const {
  if (absl::GetFlag(FLAGS_call_creds).empty()) {
    absl::SetFlag(&FLAGS_call_creds, GetDefaultCallCreds());
  }
  if (absl::GetFlag(FLAGS_channel_creds_type).empty()) {
    absl::SetFlag(&FLAGS_channel_creds_type, GetDefaultChannelCredsType());
  }
  std::shared_ptr<grpc::ChannelCredentials> channel_creds =
      GetChannelCredentials();
  // Composite any call-type credentials on top of the base channel.
  std::shared_ptr<grpc::CallCredentials> call_creds = GetCallCredentials();
  return (channel_creds == nullptr || call_creds == nullptr)
             ? channel_creds
             : grpc::CompositeChannelCredentials(channel_creds, call_creds);
}

std::string CliCredentials::GetCredentialUsage() const {
  return "    --ssl_target             ; Set server host for ssl validation\n"
         "    --ssl_client_cert        ; Client cert for ssl\n"
         "    --ssl_client_key         ; Client private key for ssl\n"
         "    --local_connect_type     ; Set to local_tcp or uds\n"
         "    --channel_creds_type     ; Set to insecure, ssl, gdc, alts, or "
         "local\n"
         "    --call_creds             ; Set to none, or"
         " access_token=<token>\n";
}

std::string CliCredentials::GetSslTargetNameOverride() const {
  bool use_ssl = absl::GetFlag(FLAGS_channel_creds_type) == "ssl" ||
                 absl::GetFlag(FLAGS_channel_creds_type) == "gdc";
  return use_ssl ? absl::GetFlag(FLAGS_ssl_target) : "";
}

}  // namespace testing
}  // namespace grpc
