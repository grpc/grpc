/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_REGISTRY_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_REGISTRY_H

#include "src/core/ext/filters/client_channel/resolver_factory.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/support/memory.h"
#include "src/core/lib/support/orphanable.h"
#include "src/core/lib/support/vector.h"

namespace grpc_core {

class ResolverRegistry {
 public:
  /// Returns the global resolver registry.
  static ResolverRegistry* Global();

  /// Sets the default URI prefix to \a default_prefix.
  void SetDefaultPrefix(const char* default_prefix);

  /// Registers a resolver factory.  The factory will be used to create a
  /// resolver for any URI whose scheme matches that of the factory.
  void RegisterResolverFactory(UniquePtr<ResolverFactory> factory);

  /// Creates a resolver given \a target.
  /// First tries to parse \a target as a URI. If this succeeds, tries
  /// to locate a registered resolver factory based on the URI scheme.
  /// If parsing or location fails, prefixes default_prefix
  /// to target and tries again.
  /// If a resolver factory was found, uses it to instantiate a resolver and
  /// returns it; otherwise, returns nullptr.
  OrphanablePtr<Resolver> CreateResolver(const char* target,
                                         const grpc_channel_args* args,
                                         grpc_pollset_set* pollset_set,
                                         grpc_combiner* combiner);

  /// Returns the default authority to pass from a client for \a target.
  UniquePtr<char> GetDefaultAuthority(const char* target);

  /// Returns \a target with the default prefix prepended, if needed.
  UniquePtr<char> AddDefaultPrefixIfNeeded(const char* target);

  /// Returns the resolver factory for \a scheme.
  /// Caller does NOT own the return value.
  ResolverFactory* LookupResolverFactory(const char* scheme);

  /// Global initialization and shutdown hooks.
  static void Init();
  static void Shutdown();

  // DO NOT USE THESE.
  // Instead, use the singleton instance returned by Global().
  // The only reason these are not private is that they need to be
  // accessed by the gRPC-specific New<> and Delete<>.
  ResolverRegistry();
  ~ResolverRegistry();

 private:
  ResolverFactory* FindResolverFactory(const char* target, grpc_uri** uri,
                                       char** canonical_target);

  InlinedVector<UniquePtr<ResolverFactory>, 10> factories_;
  UniquePtr<char> default_prefix_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_REGISTRY_H */
