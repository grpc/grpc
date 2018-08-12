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
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/mutex_lock.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

struct grpc_addresses {};
struct grpc_resolver {};
struct grpc_resolver_observer {};
struct grpc_resolver_factory {};
struct grpc_resolver_args {
  const grpc_core::ResolverArgs* args;
};

namespace grpc_core {

namespace {

struct ResolverFactoryVTable {
  void* (*resolve)(void* factory_user_data, grpc_resolver_args* args,
                   grpc_resolver_observer* observer);
  void (*destroy)(void* factory_user_data);
};

struct ResolverVTable {
  void (*request_reresolution)(void* resolver_user_data);
  void (*destroy)(void* resolver_user_data);
};

class CustomResolverObserver;

class ResolverAddresses : public grpc_addresses {
 public:
  ResolverAddresses(size_t capacity) : capacity_(capacity) {
    addresses_ = grpc_lb_addresses_create(capacity, nullptr);
    addresses_->num_addresses = 0;
  }

  ~ResolverAddresses() { grpc_lb_addresses_destroy(addresses_); }

  bool AddAddress(const char* target, bool is_balancer,
                  const char* balancer_name) {
    if (addresses_->num_addresses >= capacity_) {
      return false;
    };
    grpc_uri* uri = grpc_uri_parse(target, false);
    if (!uri) return false;
    size_t index = addresses_->num_addresses++;
    bool result = grpc_lb_addresses_set_address_from_uri(
        addresses_, index, uri, is_balancer, balancer_name, nullptr);
    grpc_uri_destroy(uri);
    if (!result) {
      addresses_->num_addresses--;
    }
    return result;
  }

  grpc_lb_addresses* GetAddresses() { return addresses_; }

 private:
  grpc_lb_addresses* addresses_;
  size_t capacity_;
};

class CustomResolverFactory;

class CustomResolver : public Resolver {
 public:
  explicit CustomResolver(const ResolverArgs& args,
                          const ResolverVTable& vtable);

  void Init(void* user_data) { user_data_ = user_data; }
  void SetNextResult(grpc_channel_args* resolved_channel_args);

  void NextLocked(grpc_channel_args** result,
                  grpc_closure* on_complete) override;

  void RequestReresolutionLocked() override;

  void InitObserver(CustomResolverObserver* observer);

 private:
  friend class CustomResolverFactory;

  virtual ~CustomResolver();

  void MaybeFinishNextLocked();

  void ShutdownLocked() override;

  void* user_data_ = nullptr;
  ResolverVTable vtable_;

  // Next resolved addresses.
  gpr_mu resolved_lock_;
  grpc_closure resolved_closure_;
  grpc_channel_args* resolved_channel_args_ = nullptr;
  // Pending next completion, or NULL.
  grpc_closure* next_completion_ = nullptr;
  grpc_channel_args** target_result_ = nullptr;
};

CustomResolver::CustomResolver(const ResolverArgs& args,
                               const ResolverVTable& vtable)
    : Resolver(args.combiner), vtable_(vtable) {
  gpr_mu_init(&resolved_lock_);
}

CustomResolver::~CustomResolver() {
  grpc_channel_args_destroy(resolved_channel_args_);
  gpr_mu_destroy(&resolved_lock_);
}

struct SetNextArgs {
  CustomResolver* resolver;
  RefCountedPtr<Resolver> resolver_guard;
};

void CustomResolver::SetNextResult(grpc_channel_args* resolved_channel_args) {
  bool has_scheduled_closure = false;
  {
    MutexLock lock(&resolved_lock_);
    if (resolved_channel_args_ != nullptr) {
      has_scheduled_closure = true;
      grpc_channel_args_destroy(resolved_channel_args_);
    }
    resolved_channel_args_ = resolved_channel_args;
  }
  if (!has_scheduled_closure) {
    auto args = New<SetNextArgs>(SetNextArgs{this, Ref()});
    GRPC_CLOSURE_INIT(&resolved_closure_,
                      [](void* raw_args, grpc_error* error) {
                        auto args = static_cast<SetNextArgs*>(raw_args);
                        args->resolver->MaybeFinishNextLocked();
                        Delete(args);
                      },
                      args, grpc_combiner_scheduler(combiner()));
    GRPC_CLOSURE_SCHED(&resolved_closure_, GRPC_ERROR_NONE);
  }
}

void CustomResolver::NextLocked(grpc_channel_args** target_result,
                                grpc_closure* on_complete) {
  GPR_ASSERT(next_completion_ == nullptr);
  next_completion_ = on_complete;
  target_result_ = target_result;
  MaybeFinishNextLocked();
}

void CustomResolver::RequestReresolutionLocked() {
  vtable_.request_reresolution(user_data_);
}

void CustomResolver::MaybeFinishNextLocked() {
  if (next_completion_ != nullptr) {
    grpc_channel_args* channel_args;
    {
      MutexLock lock(&resolved_lock_);
      channel_args = resolved_channel_args_;
      resolved_channel_args_ = nullptr;
    }
    if (channel_args != nullptr) {
      *target_result_ = channel_args;
      auto next_completion = next_completion_;
      next_completion_ = nullptr;
      GRPC_CLOSURE_SCHED(next_completion, GRPC_ERROR_NONE);
    }
  }
}

void CustomResolver::ShutdownLocked() {
  vtable_.destroy(user_data_);
  if (next_completion_ != nullptr) {
    *target_result_ = nullptr;
    auto next_completion = next_completion_;
    next_completion_ = nullptr;
    GRPC_CLOSURE_SCHED(next_completion, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                            "Resolver Shutdown"));
  }
}

//
// Observer
//

class CustomResolverObserver : public grpc_resolver_observer {
 public:
  CustomResolverObserver(const ResolverArgs& args,
                         RefCountedPtr<Resolver> resolver) {
    resolver_ = std::move(resolver);
    channel_args_ = grpc_channel_args_copy(args.args);
  }

  ~CustomResolverObserver() { grpc_channel_args_destroy(channel_args_); }

  void SetAddresses(grpc_lb_addresses* addresses) {
    grpc_arg new_arg = grpc_lb_addresses_create_channel_arg(addresses);
    auto channel_args =
        grpc_channel_args_copy_and_add(channel_args_, &new_arg, 1);
    auto resolver = static_cast<CustomResolver*>(resolver_.get());
    resolver->SetNextResult(channel_args);
  }

 private:
  RefCountedPtr<Resolver> resolver_;
  grpc_channel_args* channel_args_ = nullptr;
};

//
// Factory
//

class CustomResolverFactory : public ResolverFactory,
                              public grpc_resolver_factory {
 public:
  CustomResolverFactory(const char* scheme, void* user_data,
                        const ResolverFactoryVTable& factory_vtable,
                        const ResolverVTable& resolver_vtable)
      : scheme_(gpr_strdup(scheme)),
        user_data_(user_data),
        factory_vtable_(factory_vtable),
        resolver_vtable_(resolver_vtable) {}

  ~CustomResolverFactory() {
    gpr_free(scheme_);
    factory_vtable_.destroy(user_data_);
  }

  OrphanablePtr<Resolver> CreateResolver(
      const ResolverArgs& args) const override {
    auto resolver = New<CustomResolver>(args, resolver_vtable_);
    auto observer = New<CustomResolverObserver>(args, resolver->Ref());
    grpc_resolver_args api_args = {&args};
    auto user_data = factory_vtable_.resolve(user_data_, &api_args, observer);
    if (user_data == nullptr) {
      resolver->Unref();
      Delete(observer);
      return nullptr;
    }
    resolver->Init(user_data);
    return OrphanablePtr<Resolver>(resolver);
  }

  const char* scheme() const override { return scheme_; }

 private:
  char* scheme_;
  void* user_data_;
  ResolverFactoryVTable factory_vtable_;
  ResolverVTable resolver_vtable_;
};

}  // namespace

}  // namespace grpc_core

grpc_uri* grpc_resolver_args_get_target(grpc_resolver_args* args) {
  GPR_ASSERT(args != nullptr);
  return args->args->uri;
}

grpc_addresses* grpc_addresses_create_with_capacity(size_t capacity,
                                                    void* reserved) {
  GPR_ASSERT(reserved == nullptr);
  return grpc_core::New<grpc_core::ResolverAddresses>(capacity);
}

void grpc_addresses_destroy(grpc_addresses* addresses) {
  grpc_core::Delete(addresses);
}

grpc_lb_addresses* grpc_addresses_copy_lb_addresses_for_test(
    grpc_addresses* addresses) {
  auto resolver_addrs = static_cast<grpc_core::ResolverAddresses*>(addresses);
  return grpc_lb_addresses_copy(resolver_addrs->GetAddresses());
}

bool grpc_addresses_add_backend_address(grpc_addresses* addresses,
                                        const char* target, void* reserved) {
  GPR_ASSERT(reserved == nullptr);
  auto resolver_addrs = static_cast<grpc_core::ResolverAddresses*>(addresses);
  return resolver_addrs->AddAddress(target, false, nullptr);
}

bool grpc_addresses_add_balancer_address(grpc_addresses* addresses,
                                         const char* target,
                                         const char* balancer_name,
                                         void* reserved) {
  GPR_ASSERT(reserved == nullptr);
  auto resolver_addrs = static_cast<grpc_core::ResolverAddresses*>(addresses);
  return resolver_addrs->AddAddress(target, true, balancer_name);
}

void grpc_resolver_factory_register(
    const char* scheme, void* factory_user_data,
    void* (*resolver_factory_resolve)(void* factory_user_data,
                                      grpc_resolver_args* args,
                                      grpc_resolver_observer* observer),
    void (*resolver_factory_destroy)(void* factory_user_data),
    void (*resolver_request_reresolution)(void* resolver_user_data),
    void (*resolver_destroy)(void* resolver_user_data), void* reserved) {
  GPR_ASSERT(reserved == nullptr);
  grpc_core::ResolverFactoryVTable resolver_factory_vtable = {
      resolver_factory_resolve, resolver_factory_destroy};
  grpc_core::ResolverVTable resolver_vtable = {resolver_request_reresolution,
                                               resolver_destroy};
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      grpc_core::UniquePtr<grpc_core::ResolverFactory>(
          grpc_core::New<grpc_core::CustomResolverFactory>(
              scheme, factory_user_data, resolver_factory_vtable,
              resolver_vtable)));
}

void grpc_resolver_observer_destroy(grpc_resolver_observer* observer) {
  auto custom_observer =
      static_cast<grpc_core::CustomResolverObserver*>(observer);
  grpc_core::Delete(custom_observer);
}

void grpc_resolver_observer_set_addresses(grpc_resolver_observer* observer,
                                          grpc_addresses* addresses) {
  auto resolver_addrs = static_cast<grpc_core::ResolverAddresses*>(addresses);
  auto custom_observer =
      static_cast<grpc_core::CustomResolverObserver*>(observer);
  custom_observer->SetAddresses(resolver_addrs->GetAddresses());
}
