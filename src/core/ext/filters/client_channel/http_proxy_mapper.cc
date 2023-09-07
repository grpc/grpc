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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/http_proxy_mapper.h"

#include <stdint.h>
#include <string.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/slice/b64.h"
#include "src/core/lib/transport/http_connect_handshaker.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {
namespace {

bool ServerInCIDRRange(absl::string_view server_host,
                       absl::string_view no_proxy_entry) {
  auto server_address = StringToSockaddr(server_host, 0);
  if (!server_address.ok()) {
    return false;
  }
  std::pair<absl::string_view, absl::string_view> possible_cidr =
      absl::StrSplit(no_proxy_entry, absl::MaxSplits('/', 2),
                     absl::SkipEmpty());
  if (possible_cidr.first.empty() || possible_cidr.second.empty()) {
    return false;
  }
  auto proxy_address = StringToSockaddr(possible_cidr.first, 0);
  if (!proxy_address.ok()) {
    return false;
  }
  uint32_t mask_bits = 0;
  if (absl::SimpleAtoi(possible_cidr.second, &mask_bits)) {
    grpc_sockaddr_mask_bits(&*proxy_address, mask_bits);
    return grpc_sockaddr_match_subnet(&*server_address, &*proxy_address,
                                      mask_bits);
  }
  return false;
}

///
/// Parses the 'https_proxy' env var (fallback on 'http_proxy') and returns the
/// proxy hostname to resolve or nullopt on error. Also sets 'user_cred' to user
/// credentials if present in the 'http_proxy' env var, otherwise leaves it
/// unchanged.
///
absl::optional<std::string> GetHttpProxyServer(
    const ChannelArgs& args, absl::optional<std::string>* user_cred) {
  GPR_ASSERT(user_cred != nullptr);
  absl::StatusOr<URI> uri;
  // We check the following places to determine the HTTP proxy to use, stopping
  // at the first one that is set:
  // 1. GRPC_ARG_HTTP_PROXY channel arg
  // 2. grpc_proxy environment variable
  // 3. https_proxy environment variable
  // 4. http_proxy environment variable
  // If none of the above are set, then no HTTP proxy will be used.
  //
  absl::optional<std::string> uri_str =
      args.GetOwnedString(GRPC_ARG_HTTP_PROXY);
  if (!uri_str.has_value()) uri_str = GetEnv("grpc_proxy");
  if (!uri_str.has_value()) uri_str = GetEnv("https_proxy");
  if (!uri_str.has_value()) uri_str = GetEnv("http_proxy");
  if (!uri_str.has_value()) return absl::nullopt;
  // an empty value means "don't use proxy"
  if (uri_str->empty()) return absl::nullopt;
  uri = URI::Parse(*uri_str);
  if (!uri.ok() || uri->authority().empty()) {
    gpr_log(GPR_ERROR, "cannot parse value of 'http_proxy' env var. Error: %s",
            uri.status().ToString().c_str());
    return absl::nullopt;
  }
  if (uri->scheme() != "http") {
    gpr_log(GPR_ERROR, "'%s' scheme not supported in proxy URI",
            uri->scheme().c_str());
    return absl::nullopt;
  }
  // Split on '@' to separate user credentials from host
  char** authority_strs = nullptr;
  size_t authority_nstrs;
  gpr_string_split(uri->authority().c_str(), "@", &authority_strs,
                   &authority_nstrs);
  GPR_ASSERT(authority_nstrs != 0);  // should have at least 1 string
  absl::optional<std::string> proxy_name;
  if (authority_nstrs == 1) {
    // User cred not present in authority
    proxy_name = authority_strs[0];
  } else if (authority_nstrs == 2) {
    // User cred found
    *user_cred = authority_strs[0];
    proxy_name = authority_strs[1];
    gpr_log(GPR_DEBUG, "userinfo found in proxy URI");
  } else {
    // Bad authority
    proxy_name = absl::nullopt;
  }
  for (size_t i = 0; i < authority_nstrs; i++) {
    gpr_free(authority_strs[i]);
  }
  gpr_free(authority_strs);
  return proxy_name;
}

// Adds the default port if target does not contain a port.
std::string MaybeAddDefaultPort(absl::string_view target) {
  absl::string_view host;
  absl::string_view port;
  SplitHostPort(target, &host, &port);
  if (port.empty()) {
    return JoinHostPort(host, kDefaultSecurePortInt);
  }
  return std::string(target);
}

}  // namespace

absl::optional<std::string> HttpProxyMapper::MapName(
    absl::string_view server_uri, ChannelArgs* args) {
  if (!args->GetBool(GRPC_ARG_ENABLE_HTTP_PROXY).value_or(true)) {
    return absl::nullopt;
  }
  absl::optional<std::string> user_cred;
  auto name_to_resolve = GetHttpProxyServer(*args, &user_cred);
  if (!name_to_resolve.has_value()) return name_to_resolve;
  absl::StatusOr<URI> uri = URI::Parse(server_uri);
  if (!uri.ok() || uri->path().empty()) {
    gpr_log(GPR_ERROR,
            "'http_proxy' environment variable set, but cannot "
            "parse server URI '%s' -- not using proxy. Error: %s",
            std::string(server_uri).c_str(), uri.status().ToString().c_str());
    return absl::nullopt;
  }
  if (uri->scheme() == "unix") {
    gpr_log(GPR_INFO, "not using proxy for Unix domain socket '%s'",
            std::string(server_uri).c_str());
    return absl::nullopt;
  }
  if (uri->scheme() == "vsock") {
    gpr_log(GPR_INFO, "not using proxy for VSock '%s'",
            std::string(server_uri).c_str());
    return absl::nullopt;
  }
  // Prefer using 'no_grpc_proxy'. Fallback on 'no_proxy' if it is not set.
  auto no_proxy_str = GetEnv("no_grpc_proxy");
  if (!no_proxy_str.has_value()) {
    no_proxy_str = GetEnv("no_proxy");
  }
  if (no_proxy_str.has_value()) {
    bool use_proxy = true;
    std::string server_host;
    std::string server_port;
    if (!SplitHostPort(absl::StripPrefix(uri->path(), "/"), &server_host,
                       &server_port)) {
      gpr_log(GPR_INFO,
              "unable to split host and port, not checking no_proxy list for "
              "host '%s'",
              std::string(server_uri).c_str());
    } else {
      std::vector<absl::string_view> no_proxy_hosts =
          absl::StrSplit(*no_proxy_str, ',', absl::SkipEmpty());
      for (const auto& no_proxy_entry : no_proxy_hosts) {
        auto entry = absl::StripAsciiWhitespace(no_proxy_entry);
        if (absl::EndsWithIgnoreCase(server_host, entry) ||
            ServerInCIDRRange(server_host, entry)) {
          gpr_log(GPR_INFO, "not using proxy for host in no_proxy list '%s'",
                  std::string(server_uri).c_str());
          use_proxy = false;
          break;
        }
      }
      if (!use_proxy) return absl::nullopt;
    }
  }
  *args = args->Set(GRPC_ARG_HTTP_CONNECT_SERVER,
                    MaybeAddDefaultPort(absl::StripPrefix(uri->path(), "/")));
  if (user_cred.has_value()) {
    // Use base64 encoding for user credentials as stated in RFC 7617
    auto encoded_user_cred = UniquePtr<char>(
        grpc_base64_encode(user_cred->data(), user_cred->length(), 0, 0));
    *args = args->Set(
        GRPC_ARG_HTTP_CONNECT_HEADERS,
        absl::StrCat("Proxy-Authorization:Basic ", encoded_user_cred.get()));
  }
  return name_to_resolve;
}

void RegisterHttpProxyMapper(CoreConfiguration::Builder* builder) {
  builder->proxy_mapper_registry()->Register(
      true /* at_start */,
      std::unique_ptr<ProxyMapperInterface>(new HttpProxyMapper()));
}

}  // namespace grpc_core
