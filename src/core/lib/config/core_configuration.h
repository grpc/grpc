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
   private:
    friend class CoreConfiguration;

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

  // Drop the core configuration. Users must ensure no other threads are
  // accessing the configuration.
  static void Reset();

  // Accessors

 private:
  CoreConfiguration();

  // Create a new CoreConfiguration, and either set it or throw it away.
  // We allow multiple CoreConfiguration's to be created in parallel.
  static const CoreConfiguration& BuildNewAndMaybeSet();

  // The configuration
  static std::atomic<CoreConfiguration*> config_;
};

extern void BuildCoreConfiguration(CoreConfiguration::Builder* builder);

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CONFIG_CORE_CONFIGURATION_H */
