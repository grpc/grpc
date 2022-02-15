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

#ifndef GRPC_CORE_LIB_RESOLVER_RESOLVER_REGISTRY_H
#define GRPC_CORE_LIB_RESOLVER_RESOLVER_REGISTRY_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/resolver/resolver_factory.h"

namespace grpc_core {

class ResolverRegistry {
  class State;

 public:
  /// Methods used to create and populate the ResolverRegistry.
  /// NOT THREAD SAFE -- to be used only during global gRPC
  /// initialization and shutdown.
  class Builder {
   public:
    Builder();
    ~Builder();

    /// Sets the default URI prefix to \a default_prefix.
    void SetDefaultPrefix(const char* default_prefix);

    /// Registers a resolver factory.  The factory will be used to create a
    /// resolver for any URI whose scheme matches that of the factory.
    void RegisterResolverFactory(std::unique_ptr<ResolverFactory> factory);

    /// Returns true iff scheme already has a registered factory.
    bool HasResolverFactory(const char* scheme) const;

    /// Wipe everything in the registry and reset to empty.
    void Reset();

    ResolverRegistry Build();

   private:
    std::unique_ptr<State> state_;
  };

  ~ResolverRegistry();

  ResolverRegistry(const ResolverRegistry&) = delete;
  ResolverRegistry& operator=(const ResolverRegistry&) = delete;
  ResolverRegistry(ResolverRegistry&&);
  ResolverRegistry& operator=(ResolverRegistry&&);

  /// Checks whether the user input \a target is valid to create a resolver.
  bool IsValidTarget(absl::string_view target) const;

  /// Creates a resolver given \a target.
  /// First tries to parse \a target as a URI. If this succeeds, tries
  /// to locate a registered resolver factory based on the URI scheme.
  /// If parsing fails or there is no factory for the URI's scheme,
  /// prepends default_prefix to target and tries again.
  /// If a resolver factory is found, uses it to instantiate a resolver and
  /// returns it; otherwise, returns nullptr.
  /// \a args, \a pollset_set, and \a work_serializer are passed to the
  /// factory's \a CreateResolver() method. \a args are the channel args to be
  /// included in resolver results. \a pollset_set is used to drive I/O in the
  /// name resolution process. \a work_serializer is the work_serializer under
  /// which all resolver calls will be run. \a result_handler is used to return
  /// results from the resolver.
  OrphanablePtr<Resolver> CreateResolver(
      const char* target, const grpc_channel_args* args,
      grpc_pollset_set* pollset_set,
      std::shared_ptr<WorkSerializer> work_serializer,
      std::unique_ptr<Resolver::ResultHandler> result_handler) const;

  /// Returns the default authority to pass from a client for \a target.
  std::string GetDefaultAuthority(absl::string_view target) const;

  /// Returns \a target with the default prefix prepended, if needed.
  UniquePtr<char> AddDefaultPrefixIfNeeded(const char* target) const;

  /// Returns the resolver factory for \a scheme.
  /// Caller does NOT own the return value.
  ResolverFactory* LookupResolverFactory(const char* scheme) const;

 private:
  ResolverRegistry(std::unique_ptr<State> state);

  std::unique_ptr<State> state_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_RESOLVER_RESOLVER_REGISTRY_H */
