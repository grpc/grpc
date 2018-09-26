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
struct grpc_resolver_observer {};

namespace grpc_core {

namespace {

class PluginResolverObserver;
class PluginResolverFactory;

class PluginResolver : public Resolver {
 public:
  explicit PluginResolver(const ResolverArgs& args);

  bool Init(grpc_resolver resolver) {
    resolver_ = resolver;
    return resolver_.user_data != nullptr;
  }
  void SetNextResult(grpc_channel_args* resolved_channel_args,
                     grpc_error* error);
  static void SetNextResultLocked(void* raw_args, grpc_error* error);

  void NextLocked(grpc_channel_args** result,
                  grpc_closure* on_complete) override;

  void RequestReresolutionLocked() override;

  void InitObserver(PluginResolverObserver* observer);

 private:
  friend class PluginResolverFactory;

  virtual ~PluginResolver();

  void MaybeFinishNextLocked();

  void ShutdownLocked() override;

  grpc_resolver resolver_;

  // Next resolved addresses.
  grpc_closure resolved_closure_;
  grpc_channel_args* resolved_channel_args_ = nullptr;
  grpc_error* resolved_error_ = nullptr;
  // Pending next completion, or NULL.
  grpc_closure* next_completion_ = nullptr;
  grpc_channel_args** target_result_ = nullptr;
};

PluginResolver::PluginResolver(const ResolverArgs& args)
    : Resolver(args.combiner) {
  memset(&resolver_, 0, sizeof(resolver_));
}

PluginResolver::~PluginResolver() {
  grpc_channel_args_destroy(resolved_channel_args_);
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

void PluginResolver::SetNextResult(grpc_channel_args* resolved_channel_args,
                                   grpc_error* error) {
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

void PluginResolver::NextLocked(grpc_channel_args** target_result,
                                grpc_closure* on_complete) {
  GPR_ASSERT(next_completion_ == nullptr);
  next_completion_ = on_complete;
  target_result_ = target_result;
  MaybeFinishNextLocked();
}

void PluginResolver::RequestReresolutionLocked() {
  resolver_.request_reresolution(resolver_.user_data);
}

void PluginResolver::MaybeFinishNextLocked() {
  if (next_completion_ == nullptr) return;
  if (resolved_channel_args_ == nullptr && resolved_error_ == nullptr) return;
  *target_result_ = resolved_channel_args_;
  auto* next_completion = next_completion_;
  next_completion_ = nullptr;
  GRPC_CLOSURE_SCHED(next_completion, resolved_error_);
  resolved_channel_args_ = nullptr;
  resolved_error_ = nullptr;
}

void PluginResolver::ShutdownLocked() {
  if (resolver_.user_data != nullptr) {
    resolver_.destroy(resolver_.user_data);
  }
  if (next_completion_ != nullptr) {
    *target_result_ = nullptr;
    auto* next_completion = next_completion_;
    next_completion_ = nullptr;
    GRPC_CLOSURE_SCHED(next_completion, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                            "Resolver Shutdown"));
  }
}

//
// Observer
//

class PluginResolverObserver : public grpc_resolver_observer {
 public:
  PluginResolverObserver(const ResolverArgs& args,
                         RefCountedPtr<Resolver> resolver)
      : resolver_(std::move(resolver)),
        channel_args_(grpc_channel_args_copy(args.args)) {}

  ~PluginResolverObserver() { grpc_channel_args_destroy(channel_args_); }

  void SetResult(const grpc_resolver_result* result) {
    auto* channel_args =
        add_resolver_result_to_channel_args(channel_args_, result);
    auto* resolver = static_cast<PluginResolver*>(resolver_.get());
    resolver->SetNextResult(channel_args, nullptr);
  }

  void SetError(grpc_error* error) {
    auto* resolver = static_cast<PluginResolver*>(resolver_.get());
    resolver->SetNextResult(nullptr, error);
  }

 private:
  RefCountedPtr<Resolver> resolver_;
  grpc_channel_args* channel_args_;
};

//
// Factory
//

class PluginResolverFactory : public ResolverFactory,
                              public grpc_resolver_factory {
 public:
  PluginResolverFactory(const char* scheme,
                        const grpc_resolver_factory& factory)
      : scheme_(gpr_strdup(scheme)), factory_(factory) {}

  ~PluginResolverFactory() { factory_.destroy(factory_.user_data); }

  OrphanablePtr<Resolver> CreateResolver(
      const ResolverArgs& args) const override {
    auto resolver = MakeOrphanable<PluginResolver>(args);
    auto* observer = New<PluginResolverObserver>(args, resolver->Ref());
    grpc_resolver_args api_args = {args.target, observer};
    if (!resolver->Init(
            factory_.create_resolver(factory_.user_data, &api_args))) {
      Delete(observer);
      return nullptr;
    }
    return OrphanablePtr<Resolver>(resolver.release());
  }

  const char* scheme() const override { return scheme_.get(); }

 private:
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

void grpc_resolver_observer_destroy(grpc_resolver_observer* observer) {
  auto* plugin_observer =
      static_cast<grpc_core::PluginResolverObserver*>(observer);
  grpc_core::Delete(plugin_observer);
}

void grpc_resolver_observer_set_result(grpc_resolver_observer* observer,
                                       const grpc_resolver_result* result) {
  auto* plugin_observer =
      static_cast<grpc_core::PluginResolverObserver*>(observer);
  plugin_observer->SetResult(result);
}

void grpc_resolver_observer_set_error(grpc_resolver_observer* observer,
                                      const char* file, int line,
                                      grpc_slice desc) {
  auto* plugin_observer =
      static_cast<grpc_core::PluginResolverObserver*>(observer);
  grpc_error* error = grpc_error_create(file, line, desc, nullptr, 0);
  plugin_observer->SetError(error);
}
