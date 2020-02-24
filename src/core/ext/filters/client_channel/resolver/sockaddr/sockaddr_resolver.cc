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

#include <grpc/support/port_platform.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

namespace grpc_core {

namespace {

class SockaddrResolver : public Resolver {
 public:
  SockaddrResolver(ServerAddressList addresses, ResolverArgs args);
  ~SockaddrResolver() override;

  void StartLocked() override;

  void ShutdownLocked() override {}

 private:
  ServerAddressList addresses_;
  const grpc_channel_args* channel_args_ = nullptr;
};

SockaddrResolver::SockaddrResolver(ServerAddressList addresses,
                                   ResolverArgs args)
    : Resolver(args.combiner, std::move(args.result_handler)),
      addresses_(std::move(addresses)),
      channel_args_(grpc_channel_args_copy(args.args)) {}

SockaddrResolver::~SockaddrResolver() {
  grpc_channel_args_destroy(channel_args_);
}

void SockaddrResolver::StartLocked() {
  Result result;
  result.addresses = std::move(addresses_);
  // TODO(roth): Use std::move() once channel args is converted to C++.
  result.args = channel_args_;
  channel_args_ = nullptr;
  result_handler()->ReturnResult(std::move(result));
}

//
// Factory
//

void DoNothing(void* /*ignored*/) {}

bool ParseUri(const grpc_uri* uri,
              bool parse(const grpc_uri* uri, grpc_resolved_address* dst),
              ServerAddressList* addresses) {
  if (0 != strcmp(uri->authority, "")) {
    gpr_log(GPR_ERROR, "authority-based URIs not supported by the %s scheme",
            uri->scheme);
    return false;
  }
  // Construct addresses.
  grpc_slice path_slice =
      grpc_slice_new(uri->path, strlen(uri->path), DoNothing);
  grpc_slice_buffer path_parts;
  grpc_slice_buffer_init(&path_parts);
  grpc_slice_split(path_slice, ",", &path_parts);
  bool errors_found = false;
  for (size_t i = 0; i < path_parts.count; i++) {
    grpc_uri ith_uri = *uri;
    grpc_core::UniquePtr<char> part_str(
        grpc_slice_to_c_string(path_parts.slices[i]));
    ith_uri.path = part_str.get();
    grpc_resolved_address addr;
    if (!parse(&ith_uri, &addr)) {
      errors_found = true;
      break;
    }
    if (addresses != nullptr) {
      addresses->emplace_back(addr, nullptr /* args */);
    }
  }
  grpc_slice_buffer_destroy_internal(&path_parts);
  grpc_slice_unref_internal(path_slice);
  return !errors_found;
}

OrphanablePtr<Resolver> CreateSockaddrResolver(
    ResolverArgs args,
    bool parse(const grpc_uri* uri, grpc_resolved_address* dst)) {
  ServerAddressList addresses;
  if (!ParseUri(args.uri, parse, &addresses)) return nullptr;
  // Instantiate resolver.
  return MakeOrphanable<SockaddrResolver>(std::move(addresses),
                                          std::move(args));
}

class IPv4ResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const grpc_uri* uri) const override {
    return ParseUri(uri, grpc_parse_ipv4, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return CreateSockaddrResolver(std::move(args), grpc_parse_ipv4);
  }

  const char* scheme() const override { return "ipv4"; }
};

class IPv6ResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const grpc_uri* uri) const override {
    return ParseUri(uri, grpc_parse_ipv6, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return CreateSockaddrResolver(std::move(args), grpc_parse_ipv6);
  }

  const char* scheme() const override { return "ipv6"; }
};

#ifdef GRPC_HAVE_UNIX_SOCKET
class UnixResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const grpc_uri* uri) const override {
    return ParseUri(uri, grpc_parse_unix, nullptr);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return CreateSockaddrResolver(std::move(args), grpc_parse_unix);
  }

  grpc_core::UniquePtr<char> GetDefaultAuthority(
      grpc_uri* /*uri*/) const override {
    return grpc_core::UniquePtr<char>(gpr_strdup("localhost"));
  }

  const char* scheme() const override { return "unix"; }
};
#endif  // GRPC_HAVE_UNIX_SOCKET

}  // namespace

}  // namespace grpc_core

void grpc_resolver_sockaddr_init() {
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      absl::make_unique<grpc_core::IPv4ResolverFactory>());
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      absl::make_unique<grpc_core::IPv6ResolverFactory>());
#ifdef GRPC_HAVE_UNIX_SOCKET
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      absl::make_unique<grpc_core::UnixResolverFactory>());
#endif
}

void grpc_resolver_sockaddr_shutdown() {}
