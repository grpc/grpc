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

#include <grpc/support/port_platform.h>

#include <inttypes.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/timer.h"

#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2

namespace grpc_core {

namespace {

const char kDefaultPort[] = "https";

class NativeDnsResolver : public Resolver {
 public:
  explicit NativeDnsResolver(const ResolverArgs& args);

  void NextLocked(grpc_channel_args** result,
                  grpc_closure* on_complete) override;

  void ChannelSawErrorLocked() override;

  void ShutdownLocked() override;

 private:
  virtual ~NativeDnsResolver();

  void StartResolvingLocked();
  void MaybeFinishNextLocked();

  static void OnRetryTimerLocked(void* arg, grpc_error* error);
  static void OnResolvedLocked(void* arg, grpc_error* error);

  /// name to resolve
  char* name_to_resolve_ = nullptr;
  /// channel args
  grpc_channel_args* channel_args_ = nullptr;
  /// pollset_set to drive the name resolution process
  grpc_pollset_set* interested_parties_ = nullptr;
  /// are we currently resolving?
  bool resolving_ = false;
  /// which version of the result have we published?
  int published_version_ = 0;
  /// which version of the result is current?
  int resolved_version_ = 0;
  /// pending next completion, or nullptr
  grpc_closure* next_completion_ = nullptr;
  /// target result address for next completion
  grpc_channel_args** target_result_ = nullptr;
  /// current (fully resolved) result
  grpc_channel_args* resolved_result_ = nullptr;
  /// retry timer
  bool have_retry_timer_ = false;
  grpc_timer retry_timer_;
  grpc_closure on_retry_;
  /// retry backoff state
  BackOff backoff_;
  /// currently resolving addresses
  grpc_resolved_addresses* addresses_ = nullptr;
};

NativeDnsResolver::NativeDnsResolver(const ResolverArgs& args)
    : Resolver(args.combiner),
      backoff_(
          BackOff::Options()
              .set_initial_backoff(GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS *
                                   1000)
              .set_multiplier(GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_DNS_RECONNECT_JITTER)
              .set_max_backoff(GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)) {
  char* path = args.uri->path;
  if (path[0] == '/') ++path;
  name_to_resolve_ = gpr_strdup(path);
  channel_args_ = grpc_channel_args_copy(args.args);
  interested_parties_ = grpc_pollset_set_create();
  if (args.pollset_set != nullptr) {
    grpc_pollset_set_add_pollset_set(interested_parties_, args.pollset_set);
  }
}

NativeDnsResolver::~NativeDnsResolver() {
  if (resolved_result_ != nullptr) {
    grpc_channel_args_destroy(resolved_result_);
  }
  grpc_pollset_set_destroy(interested_parties_);
  gpr_free(name_to_resolve_);
  grpc_channel_args_destroy(channel_args_);
}

void NativeDnsResolver::NextLocked(grpc_channel_args** result,
                                   grpc_closure* on_complete) {
  GPR_ASSERT(next_completion_ == nullptr);
  next_completion_ = on_complete;
  target_result_ = result;
  if (resolved_version_ == 0 && !resolving_) {
    backoff_.Reset();
    StartResolvingLocked();
  } else {
    MaybeFinishNextLocked();
  }
}

void NativeDnsResolver::ChannelSawErrorLocked() {
  if (!resolving_) {
    backoff_.Reset();
    StartResolvingLocked();
  }
}

void NativeDnsResolver::ShutdownLocked() {
  if (have_retry_timer_) {
    grpc_timer_cancel(&retry_timer_);
  }
  if (next_completion_ != nullptr) {
    *target_result_ = nullptr;
    GRPC_CLOSURE_SCHED(next_completion_, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                             "Resolver Shutdown"));
    next_completion_ = nullptr;
  }
}

void NativeDnsResolver::OnRetryTimerLocked(void* arg, grpc_error* error) {
  NativeDnsResolver* r = static_cast<NativeDnsResolver*>(arg);
  r->have_retry_timer_ = false;
  if (error == GRPC_ERROR_NONE) {
    if (!r->resolving_) {
      r->StartResolvingLocked();
    }
  }
  r->Unref(DEBUG_LOCATION, "retry-timer");
}

void NativeDnsResolver::OnResolvedLocked(void* arg, grpc_error* error) {
  NativeDnsResolver* r = static_cast<NativeDnsResolver*>(arg);
  grpc_channel_args* result = nullptr;
  GPR_ASSERT(r->resolving_);
  r->resolving_ = false;
  GRPC_ERROR_REF(error);
  error =
      grpc_error_set_str(error, GRPC_ERROR_STR_TARGET_ADDRESS,
                         grpc_slice_from_copied_string(r->name_to_resolve_));
  if (r->addresses_ != nullptr) {
    grpc_lb_addresses* addresses = grpc_lb_addresses_create(
        r->addresses_->naddrs, nullptr /* user_data_vtable */);
    for (size_t i = 0; i < r->addresses_->naddrs; ++i) {
      grpc_lb_addresses_set_address(
          addresses, i, &r->addresses_->addrs[i].addr,
          r->addresses_->addrs[i].len, false /* is_balancer */,
          nullptr /* balancer_name */, nullptr /* user_data */);
    }
    grpc_arg new_arg = grpc_lb_addresses_create_channel_arg(addresses);
    result = grpc_channel_args_copy_and_add(r->channel_args_, &new_arg, 1);
    grpc_resolved_addresses_destroy(r->addresses_);
    grpc_lb_addresses_destroy(addresses);
  } else {
    grpc_millis next_try = r->backoff_.NextAttemptTime();
    grpc_millis timeout = next_try - ExecCtx::Get()->Now();
    gpr_log(GPR_INFO, "dns resolution failed (will retry): %s",
            grpc_error_string(error));
    GPR_ASSERT(!r->have_retry_timer_);
    r->have_retry_timer_ = true;
    r->Ref(DEBUG_LOCATION, "retry-timer");
    if (timeout > 0) {
      gpr_log(GPR_DEBUG, "retrying in %" PRIdPTR " milliseconds", timeout);
    } else {
      gpr_log(GPR_DEBUG, "retrying immediately");
    }
    GRPC_CLOSURE_INIT(&r->on_retry_, NativeDnsResolver::OnRetryTimerLocked, r,
                      grpc_combiner_scheduler(r->combiner()));
    grpc_timer_init(&r->retry_timer_, next_try, &r->on_retry_);
  }
  if (r->resolved_result_ != nullptr) {
    grpc_channel_args_destroy(r->resolved_result_);
  }
  r->resolved_result_ = result;
  ++r->resolved_version_;
  r->MaybeFinishNextLocked();
  GRPC_ERROR_UNREF(error);
  r->Unref(DEBUG_LOCATION, "dns-resolving");
}

void NativeDnsResolver::StartResolvingLocked() {
  Ref(DEBUG_LOCATION, "dns-resolving");
  GPR_ASSERT(!resolving_);
  resolving_ = true;
  addresses_ = nullptr;
  grpc_resolve_address(
      name_to_resolve_, kDefaultPort, interested_parties_,
      GRPC_CLOSURE_CREATE(NativeDnsResolver::OnResolvedLocked, this,
                          grpc_combiner_scheduler(combiner())),
      &addresses_);
}

void NativeDnsResolver::MaybeFinishNextLocked() {
  if (next_completion_ != nullptr && resolved_version_ != published_version_) {
    *target_result_ = resolved_result_ == nullptr
                          ? nullptr
                          : grpc_channel_args_copy(resolved_result_);
    GRPC_CLOSURE_SCHED(next_completion_, GRPC_ERROR_NONE);
    next_completion_ = nullptr;
    published_version_ = resolved_version_;
  }
}

//
// Factory
//

class NativeDnsResolverFactory : public ResolverFactory {
 public:
  OrphanablePtr<Resolver> CreateResolver(
      const ResolverArgs& args) const override {
    if (0 != strcmp(args.uri->authority, "")) {
      gpr_log(GPR_ERROR, "authority based dns uri's not supported");
      return OrphanablePtr<Resolver>(nullptr);
    }
    return OrphanablePtr<Resolver>(New<NativeDnsResolver>(args));
  }

  const char* scheme() const override { return "dns"; }
};

}  // namespace

}  // namespace grpc_core

void grpc_resolver_dns_native_init() {
  char* resolver_env = gpr_getenv("GRPC_DNS_RESOLVER");
  if (resolver_env != nullptr && gpr_stricmp(resolver_env, "native") == 0) {
    gpr_log(GPR_DEBUG, "Using native dns resolver");
    grpc_core::ResolverRegistry::Global()->RegisterResolverFactory(
        grpc_core::UniquePtr<grpc_core::ResolverFactory>(
            grpc_core::New<grpc_core::NativeDnsResolverFactory>()));
  } else {
    grpc_core::ResolverFactory* existing_factory =
        grpc_core::ResolverRegistry::Global()->LookupResolverFactory("dns");
    if (existing_factory == nullptr) {
      gpr_log(GPR_DEBUG, "Using native dns resolver");
      grpc_core::ResolverRegistry::Global()->RegisterResolverFactory(
          grpc_core::UniquePtr<grpc_core::ResolverFactory>(
              grpc_core::New<grpc_core::NativeDnsResolverFactory>()));
    }
  }
  gpr_free(resolver_env);
}

void grpc_resolver_dns_native_shutdown() {}
