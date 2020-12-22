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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/http_proxy.h"

#include <stdbool.h>
#include <string.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/slice/b64.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {
namespace {

/**
 * Parses the 'https_proxy' env var (fallback on 'http_proxy') and returns the
 * proxy hostname to resolve or nullptr on error. Also sets 'user_cred' to user
 * credentials if present in the 'http_proxy' env var, otherwise leaves it
 * unchanged. It is caller's responsibility to gpr_free user_cred.
 */
// TODO(hork): change this to return std::string
char* GetHttpProxyServer(const grpc_channel_args* args, char** user_cred) {
  GPR_ASSERT(user_cred != nullptr);
  absl::StatusOr<URI> uri;
  char* proxy_name = nullptr;
  char** authority_strs = nullptr;
  size_t authority_nstrs;
  /* We check the following places to determine the HTTP proxy to use, stopping
   * at the first one that is set:
   * 1. GRPC_ARG_HTTP_PROXY channel arg
   * 2. grpc_proxy environment variable
   * 3. https_proxy environment variable
   * 4. http_proxy environment variable
   * If none of the above are set, then no HTTP proxy will be used.
   */
  char* uri_str =
      gpr_strdup(grpc_channel_args_find_string(args, GRPC_ARG_HTTP_PROXY));
  if (uri_str == nullptr) uri_str = gpr_getenv("grpc_proxy");
  if (uri_str == nullptr) uri_str = gpr_getenv("https_proxy");
  if (uri_str == nullptr) uri_str = gpr_getenv("http_proxy");
  if (uri_str == nullptr) return nullptr;
  // an emtpy value means "don't use proxy"
  if (uri_str[0] == '\0') goto done;
  uri = URI::Parse(uri_str);
  if (!uri.ok() || uri->authority().empty()) {
    gpr_log(GPR_ERROR, "cannot parse value of 'http_proxy' env var. Error: %s",
            uri.status().ToString().c_str());
    goto done;
  }
  if (uri->scheme() != "http") {
    gpr_log(GPR_ERROR, "'%s' scheme not supported in proxy URI",
            uri->scheme().c_str());
    goto done;
  }
  /* Split on '@' to separate user credentials from host */
  gpr_string_split(uri->authority().c_str(), "@", &authority_strs,
                   &authority_nstrs);
  GPR_ASSERT(authority_nstrs != 0); /* should have at least 1 string */
  if (authority_nstrs == 1) {
    /* User cred not present in authority */
    proxy_name = authority_strs[0];
  } else if (authority_nstrs == 2) {
    /* User cred found */
    *user_cred = authority_strs[0];
    proxy_name = authority_strs[1];
    gpr_log(GPR_DEBUG, "userinfo found in proxy URI");
  } else {
    /* Bad authority */
    for (size_t i = 0; i < authority_nstrs; i++) {
      gpr_free(authority_strs[i]);
    }
    proxy_name = nullptr;
  }
  gpr_free(authority_strs);
done:
  gpr_free(uri_str);
  return proxy_name;
}

class HttpProxyMapper : public ProxyMapperInterface {
 public:
  bool MapName(const char* server_uri, const grpc_channel_args* args,
               char** name_to_resolve, grpc_channel_args** new_args) override {
    if (!grpc_channel_args_find_bool(args, GRPC_ARG_ENABLE_HTTP_PROXY, true)) {
      return false;
    }
    char* user_cred = nullptr;
    *name_to_resolve = GetHttpProxyServer(args, &user_cred);
    if (*name_to_resolve == nullptr) return false;
    char* no_proxy_str = nullptr;
    absl::StatusOr<URI> uri = URI::Parse(server_uri);
    if (!uri.ok() || uri->path().empty()) {
      gpr_log(GPR_ERROR,
              "'http_proxy' environment variable set, but cannot "
              "parse server URI '%s' -- not using proxy. Error: %s",
              server_uri, uri.status().ToString().c_str());
      goto no_use_proxy;
    }
    if (uri->scheme() == "unix") {
      gpr_log(GPR_INFO, "not using proxy for Unix domain socket '%s'",
              server_uri);
      goto no_use_proxy;
    }
    /* Prefer using 'no_grpc_proxy'. Fallback on 'no_proxy' if it is not set. */
    no_proxy_str = gpr_getenv("no_grpc_proxy");
    if (no_proxy_str == nullptr) no_proxy_str = gpr_getenv("no_proxy");
    if (no_proxy_str != nullptr) {
      static const char* NO_PROXY_SEPARATOR = ",";
      bool use_proxy = true;
      std::string server_host;
      std::string server_port;
      if (!SplitHostPort(absl::StripPrefix(uri->path(), "/"), &server_host,
                         &server_port)) {
        gpr_log(GPR_INFO,
                "unable to split host and port, not checking no_proxy list for "
                "host '%s'",
                server_uri);
        gpr_free(no_proxy_str);
      } else {
        size_t uri_len = server_host.size();
        char** no_proxy_hosts;
        size_t num_no_proxy_hosts;
        gpr_string_split(no_proxy_str, NO_PROXY_SEPARATOR, &no_proxy_hosts,
                         &num_no_proxy_hosts);
        for (size_t i = 0; i < num_no_proxy_hosts; i++) {
          char* no_proxy_entry = no_proxy_hosts[i];
          size_t no_proxy_len = strlen(no_proxy_entry);
          if (no_proxy_len <= uri_len &&
              gpr_stricmp(no_proxy_entry,
                          &(server_host.c_str()[uri_len - no_proxy_len])) ==
                  0) {
            gpr_log(GPR_INFO, "not using proxy for host in no_proxy list '%s'",
                    server_uri);
            use_proxy = false;
            break;
          }
        }
        for (size_t i = 0; i < num_no_proxy_hosts; i++) {
          gpr_free(no_proxy_hosts[i]);
        }
        gpr_free(no_proxy_hosts);
        gpr_free(no_proxy_str);
        if (!use_proxy) goto no_use_proxy;
      }
    }
    grpc_arg args_to_add[2];
    args_to_add[0] = grpc_channel_arg_string_create(
        const_cast<char*>(GRPC_ARG_HTTP_CONNECT_SERVER),
        const_cast<char*>(absl::StripPrefix(uri->path(), "/").data()));
    if (user_cred != nullptr) {
      /* Use base64 encoding for user credentials as stated in RFC 7617 */
      char* encoded_user_cred =
          grpc_base64_encode(user_cred, strlen(user_cred), 0, 0);
      std::string header =
          absl::StrCat("Proxy-Authorization:Basic ", encoded_user_cred);
      gpr_free(encoded_user_cred);
      args_to_add[1] = grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_ARG_HTTP_CONNECT_HEADERS),
          const_cast<char*>(header.c_str()));
      *new_args = grpc_channel_args_copy_and_add(args, args_to_add, 2);
    } else {
      *new_args = grpc_channel_args_copy_and_add(args, args_to_add, 1);
    }
    gpr_free(user_cred);
    return true;
  no_use_proxy:
    gpr_free(*name_to_resolve);
    *name_to_resolve = nullptr;
    gpr_free(user_cred);
    return false;
  }

  bool MapAddress(const grpc_resolved_address& /*address*/,
                  const grpc_channel_args* /*args*/,
                  grpc_resolved_address** /*new_address*/,
                  grpc_channel_args** /*new_args*/) override {
    return false;
  }
};

}  // namespace

void RegisterHttpProxyMapper() {
  ProxyMapperRegistry::Register(
      true /* at_start */,
      std::unique_ptr<ProxyMapperInterface>(new HttpProxyMapper()));
}

}  // namespace grpc_core
