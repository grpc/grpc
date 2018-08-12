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
#include "src/core/ext/filters/client_channel/resolver/plugin/plugin_resolver.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/mutex_lock.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/service_config.h"

struct grpc_addresses {};
struct grpc_resolver_observer {};

namespace grpc_core {

namespace {

class PluginResolverFactory;

struct OnCreationArg {
  OnCreationArg(RefCountedPtr<Resolver> resolver,
                grpc_closure_scheduler* scheduler);

  RefCountedPtr<Resolver> resolver;
  grpc_resolver* resolver_plugin = nullptr;
  grpc_error* error = nullptr;
  grpc_closure closure;
};

class PluginResolver : public Resolver, public grpc_resolver_observer {
 public:
  PluginResolver(grpc_resolver_factory* factory, const ResolverArgs& args);

  void NextLocked(grpc_channel_args** result,
                  grpc_closure* on_complete) override;
  void RequestReresolutionLocked() override;
  static void OnCreationLocked(void* user_data, grpc_error* error);
  void SetNextResult(const grpc_resolver_result* result, grpc_error* error);
  static void SetNextResultLocked(void* raw_args, grpc_error* error);
  void RefAsObserver();
  void UnrefAsObserver();

 private:
  virtual ~PluginResolver();

  void InitLocked(grpc_resolver* resolver, grpc_error* error);
  static void OnCreation(void* user_data, grpc_resolver* resolver,
                         const char* error_details);
  void MaybeFinishNextLocked();
  void MaybeStartResolvingLocked();
  static void OnNextResolutionLocked(void* arg, grpc_error* error);
  void StartResolvingLocked();
  void ShutdownLocked() override;

  // Resolver implmentation.
  bool request_reresolution_requested_ = false;
  bool shutdown_requested_ = false;
  grpc_resolver* resolver_ = nullptr;
  grpc_error* initialization_error_ = nullptr;

  // Base channel arguments.
  grpc_channel_args* channel_args_;
  // Next resolved addresses.
  grpc_channel_args* resolved_channel_args_ = nullptr;
  grpc_error* resolved_error_ = nullptr;
  // Next resolution timer.
  bool have_next_resolution_timer_ = false;
  grpc_timer next_resolution_timer_;
  grpc_closure on_next_resolution_;
  // Min time between re-resolution requests.
  grpc_millis min_time_between_resolutions_;
  // Timestamp of last re-resolution request.
  grpc_millis last_resolution_timestamp_ = -1;
  // Pending next completion, or NULL.
  grpc_closure* next_completion_ = nullptr;
  grpc_channel_args** target_result_ = nullptr;
};

PluginResolver::PluginResolver(grpc_resolver_factory* factory,
                               const ResolverArgs& args)
    : Resolver(args.combiner),
      channel_args_(grpc_channel_args_copy(args.args)) {
  GRPC_CLOSURE_INIT(&on_next_resolution_,
                    PluginResolver::OnNextResolutionLocked, this,
                    grpc_combiner_scheduler(args.combiner));
  const grpc_arg* arg = grpc_channel_args_find(
      args.args, GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS);
  min_time_between_resolutions_ =
      grpc_channel_arg_get_integer(arg, {1000, 0, INT_MAX});
  grpc_resolver_args api_args = {args.target, this};
  auto on_creation_arg = grpc_core::MakeUnique<OnCreationArg>(
      Ref(), grpc_combiner_scheduler(combiner()));
  grpc_resolver* resolver = nullptr;
  const char* error_details = nullptr;
  if (!factory->create_resolver(factory, &api_args, OnCreation,
                                on_creation_arg.get(), &resolver,
                                &error_details)) {
    on_creation_arg.release();
    return;
  }
  grpc_error* error = nullptr;
  if (resolver == nullptr) {
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_details);
  }
  InitLocked(resolver, error);
  gpr_free(const_cast<char*>(error_details));
}

PluginResolver::~PluginResolver() {
  grpc_channel_args_destroy(channel_args_);
  grpc_channel_args_destroy(resolved_channel_args_);
  GRPC_ERROR_UNREF(initialization_error_);
  GRPC_ERROR_UNREF(resolved_error_);
}

void PluginResolver::OnCreation(void* user_data, grpc_resolver* resolver,
                                const char* error_details) {
  grpc_core::ExecCtx exec_ctx;
  auto* arg = static_cast<OnCreationArg*>(user_data);
  arg->resolver_plugin = resolver;
  if (resolver == nullptr) {
    arg->error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_details);
  }
  GRPC_CLOSURE_SCHED(&arg->closure, GRPC_ERROR_NONE);
}

void PluginResolver::OnCreationLocked(void* user_data, grpc_error* error) {
  auto* arg = static_cast<OnCreationArg*>(user_data);
  auto* resolver = static_cast<PluginResolver*>(arg->resolver.get());
  resolver->InitLocked(arg->resolver_plugin, arg->error);
  Delete(arg);
}

void PluginResolver::InitLocked(grpc_resolver* resolver, grpc_error* error) {
  resolver_ = resolver;
  initialization_error_ = error;
  if (shutdown_requested_ && resolver != nullptr) {
    resolver_->destroy(resolver_);
  } else {
    MaybeFinishNextLocked();
    if (request_reresolution_requested_) MaybeStartResolvingLocked();
  }
}

struct SetNextArgs {
  SetNextArgs(RefCountedPtr<Resolver> resolver,
              grpc_channel_args* resolved_channel_args, grpc_error* error,
              grpc_closure_scheduler* scheduler)
      : resolver(std::move(resolver)),
        resolved_channel_args(resolved_channel_args),
        error(error) {
    GRPC_CLOSURE_INIT(&closure, PluginResolver::SetNextResultLocked, this,
                      scheduler);
    GRPC_CLOSURE_SCHED(&closure, GRPC_ERROR_NONE);
  }

  RefCountedPtr<Resolver> resolver;
  grpc_channel_args* resolved_channel_args;
  grpc_error* error;
  grpc_closure closure;
};

void PluginResolver::SetNextResult(const grpc_resolver_result* result,
                                   grpc_error* error) {
  grpc_channel_args* resolved_channel_args = nullptr;
  if (result != nullptr) {
    resolved_channel_args =
        AddResolverResultToChannelArgs(channel_args_, result);
  }
  // TODO(elessar): Are there any ordering guarantees here?
  New<SetNextArgs>(Ref(), resolved_channel_args, error,
                   grpc_combiner_scheduler(combiner()));
}

void PluginResolver::SetNextResultLocked(void* raw_args, grpc_error* error) {
  auto* args = static_cast<SetNextArgs*>(raw_args);
  auto* resolver = static_cast<PluginResolver*>(args->resolver.get());
  grpc_channel_args_destroy(resolver->resolved_channel_args_);
  if (resolver->resolved_error_ != nullptr)
    GRPC_ERROR_UNREF(resolver->resolved_error_);
  resolver->resolved_channel_args_ = args->resolved_channel_args;
  resolver->resolved_error_ = args->error;
  resolver->MaybeFinishNextLocked();
  Delete(args);
}

void PluginResolver::RefAsObserver() {
  Ref(DEBUG_LOCATION, "grpc_resolver_observer_ref").release();
}

void PluginResolver::UnrefAsObserver() {
  Unref(DEBUG_LOCATION, "grpc_resolver_observer_unref");
}

void PluginResolver::NextLocked(grpc_channel_args** target_result,
                                grpc_closure* on_complete) {
  GPR_ASSERT(next_completion_ == nullptr);
  next_completion_ = on_complete;
  target_result_ = target_result;
  MaybeFinishNextLocked();
}

void PluginResolver::MaybeStartResolvingLocked() {
  if (resolver_ == nullptr) return;
  // TODO(roth): Remove code duplication once this logic is handled by the
  // client channel.
  // NOTE(elessar): This logic is copied from DNS resolver.
  // If there is an existing timer, the time it fires is the
  // earliest time we can start the next resolution.
  if (have_next_resolution_timer_) return;
  if (last_resolution_timestamp_ >= 0) {
    const grpc_millis earliest_next_resolution =
        last_resolution_timestamp_ + min_time_between_resolutions_;
    const grpc_millis ms_until_next_resolution =
        earliest_next_resolution - grpc_core::ExecCtx::Get()->Now();
    if (ms_until_next_resolution > 0) {
      const grpc_millis last_resolution_ago =
          grpc_core::ExecCtx::Get()->Now() - last_resolution_timestamp_;
      gpr_log(GPR_DEBUG,
              "In cooldown from last resolution (from %" PRId64
              " ms ago). Will resolve again in %" PRId64 " ms",
              last_resolution_ago, ms_until_next_resolution);
      have_next_resolution_timer_ = true;
      // TODO(roth): We currently deal with this ref manually.  Once the
      // new closure API is done, find a way to track this ref with the timer
      // callback as part of the type system.
      RefCountedPtr<Resolver> self =
          Ref(DEBUG_LOCATION, "next_resolution_timer_cooldown");
      self.release();
      grpc_timer_init(&next_resolution_timer_, ms_until_next_resolution,
                      &on_next_resolution_);
      return;
    }
  }
  StartResolvingLocked();
}

void PluginResolver::OnNextResolutionLocked(void* arg, grpc_error* error) {
  PluginResolver* resolver = static_cast<PluginResolver*>(arg);
  resolver->have_next_resolution_timer_ = false;
  if (error == GRPC_ERROR_NONE) {
    resolver->StartResolvingLocked();
  }
  resolver->Unref(DEBUG_LOCATION, "retry-timer");
}

void PluginResolver::StartResolvingLocked() {
  gpr_log(GPR_DEBUG, "Requesting re-resolution.");
  resolver_->request_reresolution(resolver_);
  last_resolution_timestamp_ = grpc_core::ExecCtx::Get()->Now();
}

void PluginResolver::MaybeFinishNextLocked() {
  if (next_completion_ == nullptr) return;
  if (resolved_channel_args_ == nullptr && resolved_error_ == nullptr &&
      initialization_error_ == nullptr) {
    return;
  }
  grpc_error* error = nullptr;
  if (initialization_error_ != nullptr) {
    error = GRPC_ERROR_REF(initialization_error_);
  } else if (resolved_error_ != nullptr) {
    error = resolved_error_;
    resolved_error_ = nullptr;
  }
  *target_result_ = resolved_channel_args_;
  resolved_channel_args_ = nullptr;
  auto* next_completion = next_completion_;
  next_completion_ = nullptr;
  GRPC_CLOSURE_SCHED(next_completion, error);
}

void PluginResolver::RequestReresolutionLocked() {
  request_reresolution_requested_ = true;
  MaybeStartResolvingLocked();
}

void PluginResolver::ShutdownLocked() {
  shutdown_requested_ = true;
  if (resolver_ != nullptr) resolver_->destroy(resolver_);
  if (next_completion_ != nullptr) {
    auto* next_completion = next_completion_;
    next_completion_ = nullptr;
    GRPC_CLOSURE_SCHED(next_completion, GRPC_ERROR_CANCELLED);
  }
}

OnCreationArg::OnCreationArg(RefCountedPtr<Resolver> resolver,
                             grpc_closure_scheduler* scheduler)
    : resolver(std::move(resolver)) {
  GRPC_CLOSURE_INIT(&closure, PluginResolver::OnCreationLocked, this,
                    scheduler);
}

//
// Factory
//

class PluginResolverFactory : public ResolverFactory,
                              public grpc_resolver_factory {
 public:
  PluginResolverFactory(const char* scheme, grpc_resolver_factory* factory)
      : scheme_(gpr_strdup(scheme)), factory_(factory) {}

  ~PluginResolverFactory() { factory_->destroy(factory_); }

  OrphanablePtr<Resolver> CreateResolver(
      const ResolverArgs& args) const override {
    return OrphanablePtr<Resolver>(New<PluginResolver>(factory_, args));
  }

  const char* scheme() const override { return scheme_.get(); }

 private:
  UniquePtr<char> scheme_;
  grpc_resolver_factory* factory_;
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
grpc_channel_args* AddResolverResultToChannelArgs(
    grpc_channel_args* base_args, const grpc_resolver_result* result) {
  InlinedVector<const char*, 2> args_to_remove;
  InlinedVector<grpc_arg, 3> new_args;
  grpc_core::UniquePtr<grpc_core::ServiceConfig> service_config;
  grpc_lb_addresses* addresses =
      grpc_lb_addresses_create_from_resolver_result(result);
  new_args.emplace_back(grpc_lb_addresses_create_channel_arg(addresses));
  const grpc_arg* arg = grpc_channel_args_find(
      base_args, GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION);
  grpc_integer_options integer_options = {false, false, true};
  bool request_service_config =
      !grpc_channel_arg_get_integer(arg, integer_options);
  if (request_service_config && result->json_service_config != nullptr) {
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

void grpc_resolver_plugin_init() {}
void grpc_resolver_plugin_shutdown() {}

void grpc_resolver_factory_register(const char* scheme,
                                    grpc_resolver_factory* factory) {
  grpc_core::ExecCtx exec_ctx;
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      grpc_core::UniquePtr<grpc_core::ResolverFactory>(
          grpc_core::New<grpc_core::PluginResolverFactory>(scheme, factory)));
}

void grpc_resolver_observer_ref(grpc_resolver_observer* observer) {
  // Intentionally don't initialize ExecCtx as Ref operations is side-effect
  // free.
  auto* resolver = static_cast<grpc_core::PluginResolver*>(observer);
  resolver->RefAsObserver();
}

void grpc_resolver_observer_unref(grpc_resolver_observer* observer) {
  grpc_core::ExecCtx exec_ctx;
  auto* resolver = static_cast<grpc_core::PluginResolver*>(observer);
  resolver->UnrefAsObserver();
}

void grpc_resolver_observer_set_result(grpc_resolver_observer* observer,
                                       const grpc_resolver_result* result) {
  grpc_core::ExecCtx exec_ctx;
  auto* plugin_observer = static_cast<grpc_core::PluginResolver*>(observer);
  plugin_observer->SetNextResult(result, nullptr);
}

void grpc_resolver_observer_set_error(grpc_resolver_observer* observer,
                                      const char* error_details) {
  grpc_core::ExecCtx exec_ctx;
  auto* plugin_observer = static_cast<grpc_core::PluginResolver*>(observer);
  plugin_observer->SetNextResult(
      nullptr, GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_details));
}
