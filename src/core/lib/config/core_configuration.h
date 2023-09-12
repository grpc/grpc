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

#ifndef GRPC_SRC_CORE_LIB_CONFIG_CORE_CONFIGURATION_H
#define GRPC_SRC_CORE_LIB_CONFIG_CORE_CONFIGURATION_H

#include <grpc/support/port_platform.h>

#include <atomic>

#include "absl/functional/any_invocable.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/handshaker/proxy_mapper_registry.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/security/certificate_provider/certificate_provider_registry.h"
#include "src/core/lib/security/credentials/channel_creds_registry.h"
#include "src/core/lib/service_config/service_config_parser.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/handshaker_registry.h"

namespace grpc_core {

// Global singleton that stores library configuration - factories, etc...
// that plugins might choose to extend.
class GRPC_DLL CoreConfiguration {
 public:
  CoreConfiguration(const CoreConfiguration&) = delete;
  CoreConfiguration& operator=(const CoreConfiguration&) = delete;

  // Builder is passed to plugins, etc... at initialization time to collect
  // their configuration and assemble the published CoreConfiguration.
  class Builder {
   public:
    ChannelArgsPreconditioning::Builder* channel_args_preconditioning() {
      return &channel_args_preconditioning_;
    }

    ChannelInit::Builder* channel_init() { return &channel_init_; }

    HandshakerRegistry::Builder* handshaker_registry() {
      return &handshaker_registry_;
    }

    ChannelCredsRegistry<>::Builder* channel_creds_registry() {
      return &channel_creds_registry_;
    }

    ServiceConfigParser::Builder* service_config_parser() {
      return &service_config_parser_;
    }

    ResolverRegistry::Builder* resolver_registry() {
      return &resolver_registry_;
    }

    LoadBalancingPolicyRegistry::Builder* lb_policy_registry() {
      return &lb_policy_registry_;
    }

    ProxyMapperRegistry::Builder* proxy_mapper_registry() {
      return &proxy_mapper_registry_;
    }

    CertificateProviderRegistry::Builder* certificate_provider_registry() {
      return &certificate_provider_registry_;
    }

   private:
    friend class CoreConfiguration;

    ChannelArgsPreconditioning::Builder channel_args_preconditioning_;
    ChannelInit::Builder channel_init_;
    HandshakerRegistry::Builder handshaker_registry_;
    ChannelCredsRegistry<>::Builder channel_creds_registry_;
    ServiceConfigParser::Builder service_config_parser_;
    ResolverRegistry::Builder resolver_registry_;
    LoadBalancingPolicyRegistry::Builder lb_policy_registry_;
    ProxyMapperRegistry::Builder proxy_mapper_registry_;
    CertificateProviderRegistry::Builder certificate_provider_registry_;

    Builder();
    CoreConfiguration* Build();
  };

  // Stores a builder for RegisterBuilder
  struct RegisteredBuilder {
    absl::AnyInvocable<void(Builder*)> builder;
    RegisteredBuilder* next;
  };

  // Temporarily replaces core configuration with what is built from the
  // provided BuildFunc that takes (Builder*) and returns void.
  // Requires no concurrent Get() be called. Restores current core
  // configuration when this object is destroyed. The default builder
  // is not backed up or restored.
  //
  // Useful for running multiple tests back to back in the same process
  // without side effects from previous tests.
  class WithSubstituteBuilder {
   public:
    template <typename BuildFunc>
    explicit WithSubstituteBuilder(BuildFunc build) {
      // Build core configuration to replace.
      Builder builder;
      build(&builder);
      CoreConfiguration* p = builder.Build();

      // Backup current core configuration and replace/reset.
      config_restore_ =
          CoreConfiguration::config_.exchange(p, std::memory_order_acquire);
      builders_restore_ = CoreConfiguration::builders_.exchange(
          nullptr, std::memory_order_acquire);
    }

    ~WithSubstituteBuilder() {
      // Reset and restore.
      Reset();
      GPR_ASSERT(CoreConfiguration::config_.exchange(
                     config_restore_, std::memory_order_acquire) == nullptr);
      GPR_ASSERT(CoreConfiguration::builders_.exchange(
                     builders_restore_, std::memory_order_acquire) == nullptr);
    }

   private:
    CoreConfiguration* config_restore_;
    RegisteredBuilder* builders_restore_;
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

  // Attach a registration function globally.
  // Each registration function is called *in addition to*
  // BuildCoreConfiguration for the default core configuration.
  static void RegisterBuilder(absl::AnyInvocable<void(Builder*)> builder);

  // Drop the core configuration. Users must ensure no other threads are
  // accessing the configuration.
  // Clears any dynamically registered builders.
  static void Reset();

  // Helper for tests: Reset the configuration, build a special one, run some
  // code, and then reset the configuration again.
  // Templatized to be sure no codegen in normal builds.
  template <typename BuildFunc, typename RunFunc>
  static void RunWithSpecialConfiguration(BuildFunc build_configuration,
                                          RunFunc code_to_run) {
    WithSubstituteBuilder builder(build_configuration);
    code_to_run();
  }

  // Accessors

  const ChannelArgsPreconditioning& channel_args_preconditioning() const {
    return channel_args_preconditioning_;
  }

  const ChannelInit& channel_init() const { return channel_init_; }

  const HandshakerRegistry& handshaker_registry() const {
    return handshaker_registry_;
  }

  const ChannelCredsRegistry<>& channel_creds_registry() const {
    return channel_creds_registry_;
  }

  const ServiceConfigParser& service_config_parser() const {
    return service_config_parser_;
  }

  const ResolverRegistry& resolver_registry() const {
    return resolver_registry_;
  }

  const LoadBalancingPolicyRegistry& lb_policy_registry() const {
    return lb_policy_registry_;
  }

  const ProxyMapperRegistry& proxy_mapper_registry() const {
    return proxy_mapper_registry_;
  }

  const CertificateProviderRegistry& certificate_provider_registry() const {
    return certificate_provider_registry_;
  }

  static void SetDefaultBuilder(void (*builder)(CoreConfiguration::Builder*)) {
    default_builder_ = builder;
  }

 private:
  explicit CoreConfiguration(Builder* builder);

  // Create a new CoreConfiguration, and either set it or throw it away.
  // We allow multiple CoreConfiguration's to be created in parallel.
  static const CoreConfiguration& BuildNewAndMaybeSet();

  // The configuration
  static std::atomic<CoreConfiguration*> config_;
  // Extra registered builders
  static std::atomic<RegisteredBuilder*> builders_;
  // Default builder
  static void (*default_builder_)(CoreConfiguration::Builder*);

  ChannelArgsPreconditioning channel_args_preconditioning_;
  ChannelInit channel_init_;
  HandshakerRegistry handshaker_registry_;
  ChannelCredsRegistry<> channel_creds_registry_;
  ServiceConfigParser service_config_parser_;
  ResolverRegistry resolver_registry_;
  LoadBalancingPolicyRegistry lb_policy_registry_;
  ProxyMapperRegistry proxy_mapper_registry_;
  CertificateProviderRegistry certificate_provider_registry_;
};

extern void BuildCoreConfiguration(CoreConfiguration::Builder* builder);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_CONFIG_CORE_CONFIGURATION_H
