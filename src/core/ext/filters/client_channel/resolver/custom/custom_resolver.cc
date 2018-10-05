//
// Copyright 2016 gRPC authors.
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
//

// Shim resolver for exposing public API for custom application resolvers.

#include <grpc/support/port_platform.h>

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/custom/custom_resolver.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/mutex_lock.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/service_config.h"

struct grpc_addresses {};

namespace grpc_core {

namespace {

class PluginResolverFactory;

grpc_error* grpc_error_from_status(grpc_status_code status,
                                   const char* error_details) {
  if (status == GRPC_STATUS_OK) {
    return GRPC_ERROR_NONE;
  }
  return grpc_error_set_int(GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_details),
                            GRPC_ERROR_INT_GRPC_STATUS, status);
}

struct OnCreationArg {
  OnCreationArg(RefCountedPtr<Resolver> resolver, grpc_resolver resolver_plugin,
                grpc_error* error, grpc_closure_scheduler* scheduler);

  RefCountedPtr<Resolver> resolver;
  grpc_resolver resolver_plugin;
  grpc_error* error;
  grpc_closure closure;
};

class PluginResolver : public Resolver {
 public:
  PluginResolver(const grpc_resolver_factory& factory,
                 const ResolverArgs& args);

  void NextLocked(grpc_channel_args** result,
                  grpc_closure* on_complete) override;
  void RequestReresolutionLocked() override;

 private:
  friend struct OnCreationArg;
  virtual ~PluginResolver();

  void InitLocked(const grpc_resolver& resolver, grpc_error* error);
  static void OnCreation(void* user_data, grpc_resolver resolver,
                         grpc_status_code status, const char* error_details);
  static void OnCreationLocked(void* user_data, grpc_error* error);
  static void OnNextResult(void* user_data, const grpc_resolver_result* result,
                           grpc_status_code status, const char* error_details);
  void StartNextLocked();
  void ShutdownLocked() override;

  // Resolver implmentation.
  bool resolver_initialized_ = false;
  bool next_requested_ = false;
  bool shutdown_requested_ = false;
  grpc_resolver resolver_;
  grpc_error* resolver_error_ = nullptr;

  // Base channel arguments.
  grpc_channel_args* channel_args_;

  // Pending next completion, or NULL.
  grpc_closure* next_completion_ = nullptr;
  grpc_channel_args** target_result_ = nullptr;
};

PluginResolver::PluginResolver(const grpc_resolver_factory& factory,
                               const ResolverArgs& args)
    : Resolver(args.combiner),
      channel_args_(grpc_channel_args_copy(args.args)) {
  memset(&resolver_, 0, sizeof(resolver_));
  grpc_resolver_args api_args = {args.target};
  grpc_resolver resolver;
  memset(&resolver, 0, sizeof(resolver));
  grpc_status_code status = GRPC_STATUS_OK;
  const char* error_details = nullptr;
  if (!factory.create_resolver(factory.factory_state, &api_args, OnCreation,
                               this, &resolver, &status, &error_details)) {
    Ref(DEBUG_LOCATION, "grpc_resolver_plugin_create_resolver").release();
    return;
  }
  InitLocked(resolver, grpc_error_from_status(status, error_details));
  gpr_free(const_cast<char*>(error_details));
}

PluginResolver::~PluginResolver() {
  grpc_channel_args_destroy(channel_args_);
  if (resolver_error_ == GRPC_ERROR_NONE) {
    resolver_.destroy(resolver_.resolver_state);
  }
  GRPC_ERROR_UNREF(resolver_error_);
}

void PluginResolver::OnCreation(void* user_data, grpc_resolver resolver,
                                grpc_status_code status,
                                const char* error_details) {
  auto* plugin_resolver = static_cast<PluginResolver*>(user_data);
  New<OnCreationArg>(plugin_resolver->Ref(), resolver,
                     grpc_error_from_status(status, error_details),
                     grpc_combiner_scheduler(plugin_resolver->combiner()));
  plugin_resolver->Unref(DEBUG_LOCATION, "grpc_resolver_creation_cb");
}

void PluginResolver::OnCreationLocked(void* user_data, grpc_error* error) {
  auto* arg = static_cast<OnCreationArg*>(user_data);
  auto* resolver = static_cast<PluginResolver*>(arg->resolver.get());
  resolver->InitLocked(arg->resolver_plugin, arg->error);
  Delete(arg);
}

void PluginResolver::InitLocked(const grpc_resolver& resolver,
                                grpc_error* error) {
  resolver_initialized_ = true;
  resolver_ = resolver;
  resolver_error_ = error;
  if (next_requested_) StartNextLocked();
  if (shutdown_requested_ && resolver_error_ == GRPC_ERROR_NONE) {
    resolver_.shutdown(resolver_.resolver_state);
  }
}

void PluginResolver::NextLocked(grpc_channel_args** target_result,
                                grpc_closure* on_complete) {
  GPR_ASSERT(next_completion_ == nullptr);
  next_requested_ = true;
  next_completion_ = on_complete;
  target_result_ = target_result;
  if (resolver_initialized_) StartNextLocked();
}

void PluginResolver::StartNextLocked() {
  if (resolver_error_ == GRPC_ERROR_NONE) {
    auto* user_data = Ref(DEBUG_LOCATION, "grpc_resolver_next").release();
    resolver_.next(resolver_.resolver_state, OnNextResult, user_data);
  } else {
    auto* next_completion = next_completion_;
    next_completion_ = nullptr;
    GRPC_CLOSURE_SCHED(next_completion, GRPC_ERROR_REF(resolver_error_));
  }
}

void PluginResolver::OnNextResult(void* user_data,
                                  const grpc_resolver_result* result,
                                  grpc_status_code status,
                                  const char* error_details) {
  auto* resolver = static_cast<PluginResolver*>(user_data);
  grpc_error* error = GRPC_ERROR_NONE;
  if (status == GRPC_STATUS_OK) {
    *resolver->target_result_ =
        add_resolver_result_to_channel_args(resolver->channel_args_, result);
  } else if (resolver->shutdown_requested_ && status == GRPC_STATUS_CANCELLED) {
    error = GRPC_ERROR_CANCELLED;
  } else {
    error =
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_details),
                           GRPC_ERROR_INT_GRPC_STATUS, status);
  }
  auto* next_completion = resolver->next_completion_;
  resolver->next_completion_ = nullptr;
  GRPC_CLOSURE_SCHED(next_completion, error);
  resolver->Unref(DEBUG_LOCATION, "grpc_resolver_next_cb");
}

void PluginResolver::RequestReresolutionLocked() {
  if (resolver_initialized_ && resolver_error_ == GRPC_ERROR_NONE) {
    resolver_.request_reresolution(resolver_.resolver_state);
  }
}

void PluginResolver::ShutdownLocked() {
  shutdown_requested_ = true;
  if (resolver_initialized_ && resolver_error_ == GRPC_ERROR_NONE) {
    resolver_.shutdown(resolver_.resolver_state);
  }
}

OnCreationArg::OnCreationArg(RefCountedPtr<Resolver> resolver,
                             grpc_resolver resolver_plugin, grpc_error* error,
                             grpc_closure_scheduler* scheduler)
    : resolver(std::move(resolver)),
      resolver_plugin(resolver_plugin),
      error(error) {
  GRPC_CLOSURE_INIT(&closure, PluginResolver::OnCreationLocked, this,
                    scheduler);
  GRPC_CLOSURE_SCHED(&closure, GRPC_ERROR_NONE);
}

//
// Factory
//

class PluginResolverFactory : public ResolverFactory,
                              public grpc_resolver_factory {
 public:
  PluginResolverFactory(const char* scheme,
                        const grpc_resolver_factory& factory)
      : scheme_(gpr_strdup(scheme)), factory_(factory) {}

  ~PluginResolverFactory() { factory_.destroy(factory_.factory_state); }

  OrphanablePtr<Resolver> CreateResolver(
      const ResolverArgs& args) const override {
    return OrphanablePtr<Resolver>(New<PluginResolver>(factory_, args));
  }

  const char* scheme() const override { return scheme_.get(); }

 private:
  static void OnResolverCreation(void* user_data, const grpc_resolver* resolver,
                                 grpc_status_code status,
                                 const char* error_details) {}

  UniquePtr<char> scheme_;
  grpc_resolver_factory factory_;
};

grpc_lb_addresses* grpc_lb_addresses_create_from_resolver_result(
    const grpc_resolver_result* result) {
  grpc_lb_addresses* addresses =
      grpc_lb_addresses_create(result->num_addresses, nullptr);
  size_t index = 0;
  for (size_t i = 0; i < result->num_addresses; ++i) {
    const grpc_address* address = &result->addresses[i];
    grpc_uri* uri = grpc_uri_parse(address->target, false);
    if (!uri) continue;
    bool result = grpc_lb_addresses_set_address_from_uri(
        addresses, index, uri, address->is_balancer, address->balancer_name,
        nullptr);
    if (result) index++;
    grpc_uri_destroy(uri);
  }
  addresses->num_addresses = index;
  return addresses;
}

}  // namespace

// TODO(elessar): Cover by tests.
grpc_channel_args* add_resolver_result_to_channel_args(
    grpc_channel_args* base_args, const grpc_resolver_result* result) {
  InlinedVector<const char*, 2> args_to_remove;
  InlinedVector<grpc_arg, 3> new_args;
  grpc_core::UniquePtr<grpc_core::ServiceConfig> service_config;
  grpc_lb_addresses* addresses =
      grpc_lb_addresses_create_from_resolver_result(result);
  new_args.emplace_back(grpc_lb_addresses_create_channel_arg(addresses));
  if (result->json_service_config != nullptr) {
    args_to_remove.emplace_back(GRPC_ARG_SERVICE_CONFIG);
    new_args.emplace_back(grpc_channel_arg_string_create(
        const_cast<char*>(GRPC_ARG_SERVICE_CONFIG),
        const_cast<char*>(result->json_service_config)));
    service_config =
        grpc_core::ServiceConfig::Create(result->json_service_config);
    if (service_config != nullptr) {
      const char* lb_policy_name = service_config->GetLoadBalancingPolicyName();
      if (lb_policy_name != nullptr) {
        args_to_remove.emplace_back(GRPC_ARG_LB_POLICY_NAME);
        new_args.emplace_back(grpc_channel_arg_string_create(
            const_cast<char*>(GRPC_ARG_LB_POLICY_NAME),
            const_cast<char*>(lb_policy_name)));
      }
    }
  }
  auto* channel_args = grpc_channel_args_copy_and_add_and_remove(
      base_args, args_to_remove.data(), args_to_remove.size(), new_args.data(),
      new_args.size());
  grpc_lb_addresses_destroy(addresses);
  return channel_args;
}

}  // namespace grpc_core

void grpc_resolver_factory_register(const char* scheme,
                                    grpc_resolver_factory factory) {
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      grpc_core::UniquePtr<grpc_core::ResolverFactory>(
          grpc_core::New<grpc_core::PluginResolverFactory>(scheme, factory)));
}
