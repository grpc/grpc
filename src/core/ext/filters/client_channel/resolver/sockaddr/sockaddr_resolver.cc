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

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/host_port.h"
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
  /// Takes ownership of \a addresses.
  SockaddrResolver(const ResolverArgs& args, grpc_lb_addresses* addresses);

  void NextLocked(grpc_channel_args** result,
                  grpc_closure* on_complete) override;

  void ShutdownLocked() override;

 private:
  virtual ~SockaddrResolver();

  void MaybeFinishNextLocked();

  /// the addresses that we've "resolved"
  grpc_lb_addresses* addresses_ = nullptr;
  /// channel args
  grpc_channel_args* channel_args_ = nullptr;
  /// have we published?
  bool published_ = false;
  /// pending next completion, or NULL
  grpc_closure* next_completion_ = nullptr;
  /// target result address for next completion
  grpc_channel_args** target_result_ = nullptr;
};

SockaddrResolver::SockaddrResolver(const ResolverArgs& args,
                                   grpc_lb_addresses* addresses)
    : Resolver(args.combiner),
      addresses_(addresses),
      channel_args_(grpc_channel_args_copy(args.args)) {}

SockaddrResolver::~SockaddrResolver() {
  grpc_lb_addresses_destroy(addresses_);
  grpc_channel_args_destroy(channel_args_);
}

void SockaddrResolver::NextLocked(grpc_channel_args** target_result,
                                  grpc_closure* on_complete) {
  GPR_ASSERT(!next_completion_);
  next_completion_ = on_complete;
  target_result_ = target_result;
  MaybeFinishNextLocked();
}

void SockaddrResolver::ShutdownLocked() {
  if (next_completion_ != nullptr) {
    *target_result_ = nullptr;
    GRPC_CLOSURE_SCHED(next_completion_, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                             "Resolver Shutdown"));
    next_completion_ = nullptr;
  }
}

void SockaddrResolver::MaybeFinishNextLocked() {
  if (next_completion_ != nullptr && !published_) {
    published_ = true;
    grpc_arg arg = grpc_lb_addresses_create_channel_arg(addresses_);
    *target_result_ = grpc_channel_args_copy_and_add(channel_args_, &arg, 1);
    GRPC_CLOSURE_SCHED(next_completion_, GRPC_ERROR_NONE);
    next_completion_ = nullptr;
  }
}

//
// Factory
//

void DoNothing(void* ignored) {}

OrphanablePtr<Resolver> CreateSockaddrResolver(
    const ResolverArgs& args,
    bool parse(const grpc_uri* uri, grpc_resolved_address* dst)) {
  if (0 != strcmp(args.uri->authority, "")) {
    gpr_log(GPR_ERROR, "authority-based URIs not supported by the %s scheme",
            args.uri->scheme);
    return OrphanablePtr<Resolver>(nullptr);
  }
  // Construct addresses.
  grpc_slice path_slice =
      grpc_slice_new(args.uri->path, strlen(args.uri->path), DoNothing);
  grpc_slice_buffer path_parts;
  grpc_slice_buffer_init(&path_parts);
  grpc_slice_split(path_slice, ",", &path_parts);
  grpc_lb_addresses* addresses = grpc_lb_addresses_create(
      path_parts.count, nullptr /* user_data_vtable */);
  bool errors_found = false;
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    grpc_uri ith_uri = *args.uri;
    char* part_str = grpc_slice_to_c_string(path_parts.slices[i]);
    ith_uri.path = part_str;
    if (!parse(&ith_uri, &addresses->addresses[i].address)) {
      errors_found = true; /* GPR_TRUE */
    }
    gpr_free(part_str);
    if (errors_found) break;
  }
  grpc_slice_buffer_destroy_internal(&path_parts);
  grpc_slice_unref_internal(path_slice);
  if (errors_found) {
    grpc_lb_addresses_destroy(addresses);
    return OrphanablePtr<Resolver>(nullptr);
  }
  // Instantiate resolver.
  return OrphanablePtr<Resolver>(New<SockaddrResolver>(args, addresses));
}

class IPv4ResolverFactory : public ResolverFactory {
 public:
  OrphanablePtr<Resolver> CreateResolver(
      const ResolverArgs& args) const override {
    return CreateSockaddrResolver(args, grpc_parse_ipv4);
  }

  const char* scheme() const override { return "ipv4"; }
};

class IPv6ResolverFactory : public ResolverFactory {
 public:
  OrphanablePtr<Resolver> CreateResolver(
      const ResolverArgs& args) const override {
    return CreateSockaddrResolver(args, grpc_parse_ipv6);
  }

  const char* scheme() const override { return "ipv6"; }
};

#ifdef GRPC_HAVE_UNIX_SOCKET
class UnixResolverFactory : public ResolverFactory {
 public:
  OrphanablePtr<Resolver> CreateResolver(
      const ResolverArgs& args) const override {
    return CreateSockaddrResolver(args, grpc_parse_unix);
  }

  UniquePtr<char> GetDefaultAuthority(grpc_uri* uri) const override {
    return UniquePtr<char>(gpr_strdup("localhost"));
  }

  const char* scheme() const override { return "unix"; }
};
#endif  // GRPC_HAVE_UNIX_SOCKET

}  // namespace

}  // namespace grpc_core

void grpc_resolver_sockaddr_init() {
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      grpc_core::UniquePtr<grpc_core::ResolverFactory>(
          grpc_core::New<grpc_core::IPv4ResolverFactory>()));
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      grpc_core::UniquePtr<grpc_core::ResolverFactory>(
          grpc_core::New<grpc_core::IPv6ResolverFactory>()));
#ifdef GRPC_HAVE_UNIX_SOCKET
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      grpc_core::UniquePtr<grpc_core::ResolverFactory>(
          grpc_core::New<grpc_core::UnixResolverFactory>()));
#endif
}

void grpc_resolver_sockaddr_shutdown() {}
