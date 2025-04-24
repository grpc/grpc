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

#include "src/core/config/core_configuration.h"

#include <grpc/support/port_platform.h>

#include <atomic>
#include <utility>
#include <vector>

#include "absl/log/check.h"

namespace grpc_core {

std::atomic<CoreConfiguration*> CoreConfiguration::config_{nullptr};
std::atomic<CoreConfiguration::RegisteredBuilder*>
    CoreConfiguration::builders_[static_cast<size_t>(BuilderScope::kCount)]{
        nullptr, nullptr};
std::atomic<bool> CoreConfiguration::has_config_ever_been_produced_{false};
void (*CoreConfiguration::default_builder_)(CoreConfiguration::Builder*);

CoreConfiguration::Builder::Builder() = default;

CoreConfiguration* CoreConfiguration::Builder::Build() {
  return new CoreConfiguration(this);
}

CoreConfiguration::CoreConfiguration(Builder* builder)
    : channel_args_preconditioning_(
          builder->channel_args_preconditioning_.Build()),
      channel_init_(builder->channel_init_.Build()),
      handshaker_registry_(builder->handshaker_registry_.Build()),
      channel_creds_registry_(builder->channel_creds_registry_.Build()),
      service_config_parser_(builder->service_config_parser_.Build()),
      resolver_registry_(builder->resolver_registry_.Build()),
      lb_policy_registry_(builder->lb_policy_registry_.Build()),
      proxy_mapper_registry_(builder->proxy_mapper_registry_.Build()),
      certificate_provider_registry_(
          builder->certificate_provider_registry_.Build()),
      endpoint_transport_registry_(
          builder->endpoint_transport_registry_.Build()) {}

void CoreConfiguration::RegisterBuilder(
    BuilderScope scope, absl::AnyInvocable<void(Builder*)> builder,
    SourceLocation whence) {
  CHECK(config_.load(std::memory_order_relaxed) == nullptr)
      << "CoreConfiguration was already instantiated before builder "
         "registration was completed";
  if (scope == BuilderScope::kPersistent) {
    CHECK(!has_config_ever_been_produced_.load(std::memory_order_relaxed))
        << "Persistent builders cannot be registered after the first "
           "CoreConfiguration has been produced";
  }
  CHECK_NE(scope, BuilderScope::kCount);
  auto& head = builders_[static_cast<size_t>(scope)];
  RegisteredBuilder* n = new RegisteredBuilder();
  VLOG(4) << "Registering " << scope << " builder from " << whence.file() << ":"
          << whence.line();
  n->builder = std::move(builder);
  n->whence = whence;
  n->next = head.load(std::memory_order_relaxed);
  while (!head.compare_exchange_weak(n->next, n, std::memory_order_acq_rel,
                                     std::memory_order_relaxed)) {
  }
  CHECK(config_.load(std::memory_order_relaxed) == nullptr)
      << "CoreConfiguration was already instantiated before builder "
         "registration was completed";
}

const CoreConfiguration& CoreConfiguration::BuildNewAndMaybeSet() {
  has_config_ever_been_produced_.store(true, std::memory_order_relaxed);
  // Construct builder, pass it up to code that knows about build configuration
  Builder builder;
  // The linked list of builders stores things in reverse registration order.
  // To get things registered as systems relying on this expect however, we
  // actually need to run things in forward registration order, so we iterate
  // once over the linked list to build a vector of builders, and then iterate
  // over said vector in reverse to actually run the builders.
  // Note that we also iterate scopes in reverse order here too, so that when
  // we run the builders in the reverse generated order we'll actually run
  // persistent builders before ephemeral ones.
  std::vector<RegisteredBuilder*> registered_builders;
  for (auto scope : {BuilderScope::kEphemeral, BuilderScope::kPersistent}) {
    for (RegisteredBuilder* b = builders_[static_cast<size_t>(scope)].load(
             std::memory_order_acquire);
         b != nullptr; b = b->next) {
      registered_builders.push_back(b);
    }
  }
  for (auto it = registered_builders.rbegin(); it != registered_builders.rend();
       ++it) {
    VLOG(4) << "Running builder from " << (*it)->whence.file() << ":"
            << (*it)->whence.line();
    (*it)->builder(&builder);
  }
  // Finally, call the built in configuration builder.
  if (default_builder_ != nullptr) (*default_builder_)(&builder);
  // Use builder to construct a configuration
  CoreConfiguration* p = builder.Build();
  // Try to set configuration global - it's possible another thread raced us
  // here, in which case we drop the work we did and use the one that got set
  // first
  CoreConfiguration* expected = nullptr;
  if (!config_.compare_exchange_strong(expected, p, std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
    delete p;
    return *expected;
  }
  return *p;
}

void CoreConfiguration::Reset() {
  delete config_.exchange(nullptr, std::memory_order_acquire);
  RegisteredBuilder* builder =
      builders_[static_cast<size_t>(BuilderScope::kEphemeral)].exchange(
          nullptr, std::memory_order_acquire);
  while (builder != nullptr) {
    RegisteredBuilder* next = builder->next;
    delete builder;
    builder = next;
  }
}

void CoreConfiguration::
    ResetEverythingIncludingPersistentBuildersAbsolutelyNotRecommended() {
  has_config_ever_been_produced_.store(false, std::memory_order_relaxed);
  RegisteredBuilder* builder =
      builders_[static_cast<size_t>(BuilderScope::kPersistent)].exchange(
          nullptr, std::memory_order_acquire);
  while (builder != nullptr) {
    RegisteredBuilder* next = builder->next;
    delete builder;
    builder = next;
  }
  Reset();
}

}  // namespace grpc_core
