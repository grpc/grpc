// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_LIB_CONFIG_CORE_CONFIGURATION_H
#define GRPC_CORE_LIB_CONFIG_CORE_CONFIGURATION_H

#include <grpc/impl/codegen/port_platform.h>

#include <atomic>

#include "src/core/lib/channel/handshaker_registry.h"

namespace grpc_core {

// Global singleton that stores library configuration - factories, etc...
// that plugins might choose to extend.
class CoreConfiguration {
 public:
  CoreConfiguration(const CoreConfiguration&) = delete;
  CoreConfiguration& operator=(const CoreConfiguration&) = delete;

  // Builder is passed to plugins, etc... at initialization time to collect
  // their configuration and assemble the published CoreConfiguration.
  class Builder {
   public:
    HandshakerRegistry::Builder* handshaker_registry() {
      return &handshaker_registry_;
    }

   private:
    friend class CoreConfiguration;

    HandshakerRegistry::Builder handshaker_registry_;

    Builder();
    CoreConfiguration* Build();
  };

  // Lifetime methods

  // Get the core configuration; if it does not exist, create it.
  static const CoreConfiguration& Get() {
    CoreConfiguration* p = config_.load(std::memory_order_acquire);
    if (p != nullptr) {
      return *p;
    }
    return BuildNewAndMaybeSet();
  }

  // Build a special core configuration.
  // Requires no concurrent Get() be called.
  // Doesn't call the regular BuildCoreConfiguration function, instead calls
  // `build`.
  // BuildFunc is a callable that takes a Builder* and returns void.
  // We use a template instead of std::function<void(Builder*)> to avoid
  // including std::function in this widely used header, and to ensure no code
  // is generated in programs that do not use this function.
  // This is sometimes useful for testing.
  template <typename BuildFunc>
  static void BuildSpecialConfiguration(BuildFunc build) {
    // Build bespoke configuration
    Builder builder;
    build(&builder);
    CoreConfiguration* p = builder.Build();
    // Swap in final configuration, deleting anything that was already present.
    delete config_.exchange(p, std::memory_order_release);
  }

  // Drop the core configuration. Users must ensure no other threads are
  // accessing the configuration.
  // Clears any dynamically registered builders.
  static void Reset();

  // Accessors

  const HandshakerRegistry& handshaker_registry() const {
    return handshaker_registry_;
  }

 private:
  explicit CoreConfiguration(Builder* builder);

  // Create a new CoreConfiguration, and either set it or throw it away.
  // We allow multiple CoreConfiguration's to be created in parallel.
  static const CoreConfiguration& BuildNewAndMaybeSet();

  // The configuration
  static std::atomic<CoreConfiguration*> config_;

  HandshakerRegistry handshaker_registry_;
};

extern void BuildCoreConfiguration(CoreConfiguration::Builder* builder);

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CONFIG_CORE_CONFIGURATION_H */
