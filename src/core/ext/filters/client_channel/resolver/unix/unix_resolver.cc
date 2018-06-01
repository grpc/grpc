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

#ifdef GRPC_HAVE_UNIX_SOCKET

namespace grpc_core {
namespace {

class UnixResolver : public Resolver {
 public:
  /// Takes ownership of \a addresses.
  UnixResolver(const ResolverArgs& args, grpc_lb_addresses* addresses);

  void NextLocked(grpc_channel_args** result,
                  grpc_closure* on_complete) override;

  void RequestReresolutionLocked() override;

  void ShutdownLocked() override;

 private:
  virtual ~UnixResolver();

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

UnixResolver::UnixResolver(const ResolverArgs& args,
                           grpc_lb_addresses* addresses)
    : Resolver(args.combiner),
      addresses_(addresses),
      channel_args_(grpc_channel_args_copy(args.args)) {}

UnixResolver::~UnixResolver() {
  grpc_lb_addresses_destroy(addresses_);
  grpc_channel_args_destroy(channel_args_);
}

void UnixResolver::NextLocked(grpc_channel_args** target_result,
                              grpc_closure* on_complete) {
  GPR_ASSERT(!next_completion_);
  next_completion_ = on_complete;
  target_result_ = target_result;
  MaybeFinishNextLocked();
}

void UnixResolver::RequestReresolutionLocked() {
  published_ = false;
  MaybeFinishNextLocked();
}

void UnixResolver::ShutdownLocked() {
  if (next_completion_ != nullptr) {
    *target_result_ = nullptr;
    GRPC_CLOSURE_SCHED(next_completion_, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                             "Resolver Shutdown"));
    next_completion_ = nullptr;
  }
}

void UnixResolver::MaybeFinishNextLocked() {
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

class UnixResolverFactory : public ResolverFactory {
 public:
  OrphanablePtr<Resolver> CreateResolver(
      const ResolverArgs& args) const override {
    if (0 != strcmp(args.uri->authority, "")) {
      gpr_log(GPR_ERROR, "authority-based URIs not supported by the %s scheme",
              args.uri->scheme);
      return OrphanablePtr<Resolver>(nullptr);
    }
    // Construct addresses.
    grpc_lb_addresses* addresses =
        grpc_lb_addresses_create(1, nullptr /* user_data_vtable */);
    if (!grpc_parse_unix(args.uri, &addresses->addresses[0].address)) {
      grpc_lb_addresses_destroy(addresses);
      return OrphanablePtr<Resolver>(nullptr);
    }
    // Instantiate resolver.
    return OrphanablePtr<Resolver>(New<UnixResolver>(args, addresses));
  }

  UniquePtr<char> GetDefaultAuthority(grpc_uri* uri) const override {
    return UniquePtr<char>(gpr_strdup("localhost"));
  }

  const char* scheme() const override { return "unix"; }
};

}  // namespace
}  // namespace grpc_core

#endif  // GRPC_HAVE_UNIX_SOCKET

void grpc_resolver_unix_init() {
#ifdef GRPC_HAVE_UNIX_SOCKET
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      grpc_core::UniquePtr<grpc_core::ResolverFactory>(
          grpc_core::New<grpc_core::UnixResolverFactory>()));
#endif  // GRPC_HAVE_UNIX_SOCKET
}

void grpc_resolver_unix_shutdown() {}
