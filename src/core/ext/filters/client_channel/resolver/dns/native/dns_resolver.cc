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
#include <climits>
#include <cstring>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/host_port.h"
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

  void RequestReresolutionLocked() override;

  void ResetBackoffLocked() override;

  void ShutdownLocked() override;

 private:
  virtual ~NativeDnsResolver();

  void MaybeStartResolvingLocked();
  void StartResolvingLocked();
  void MaybeFinishNextLocked();

  static void OnNextResolutionLocked(void* arg, grpc_error* error);
  static void OnResolvedLocked(void* arg, grpc_error* error);

  /// name to resolve
  char* name_to_resolve_ = nullptr;
  /// channel args
  grpc_channel_args* channel_args_ = nullptr;
  /// pollset_set to drive the name resolution process
  grpc_pollset_set* interested_parties_ = nullptr;
  /// are we currently resolving?
  bool resolving_ = false;
  grpc_closure on_resolved_;
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
  /// next resolution timer
  bool have_next_resolution_timer_ = false;
  grpc_timer next_resolution_timer_;
  grpc_closure on_next_resolution_;
  /// min time between DNS requests
  grpc_millis min_time_between_resolutions_;
  /// timestamp of last DNS request
  grpc_millis last_resolution_timestamp_ = -1;
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
  const grpc_arg* arg = grpc_channel_args_find(
      args.args, GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS);
  min_time_between_resolutions_ =
      grpc_channel_arg_get_integer(arg, {1000, 0, INT_MAX});
  interested_parties_ = grpc_pollset_set_create();
  if (args.pollset_set != nullptr) {
    grpc_pollset_set_add_pollset_set(interested_parties_, args.pollset_set);
  }
  GRPC_CLOSURE_INIT(&on_next_resolution_,
                    NativeDnsResolver::OnNextResolutionLocked, this,
                    grpc_combiner_scheduler(args.combiner));
  GRPC_CLOSURE_INIT(&on_resolved_, NativeDnsResolver::OnResolvedLocked, this,
                    grpc_combiner_scheduler(args.combiner));
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
    MaybeStartResolvingLocked();
  } else {
    MaybeFinishNextLocked();
  }
}

void NativeDnsResolver::RequestReresolutionLocked() {
  if (!resolving_) {
    MaybeStartResolvingLocked();
  }
}

void NativeDnsResolver::ResetBackoffLocked() {
  if (have_next_resolution_timer_) {
    grpc_timer_cancel(&next_resolution_timer_);
  }
  backoff_.Reset();
}

void NativeDnsResolver::ShutdownLocked() {
  if (have_next_resolution_timer_) {
    grpc_timer_cancel(&next_resolution_timer_);
  }
  if (next_completion_ != nullptr) {
    *target_result_ = nullptr;
    GRPC_CLOSURE_SCHED(next_completion_, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                             "Resolver Shutdown"));
    next_completion_ = nullptr;
  }
}

void NativeDnsResolver::OnNextResolutionLocked(void* arg, grpc_error* error) {
  NativeDnsResolver* r = static_cast<NativeDnsResolver*>(arg);
  r->have_next_resolution_timer_ = false;
  if (error == GRPC_ERROR_NONE && !r->resolving_) {
    r->StartResolvingLocked();
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
    ServerAddressList addresses;
    for (size_t i = 0; i < r->addresses_->naddrs; ++i) {
      addresses.emplace_back(&r->addresses_->addrs[i].addr,
                             r->addresses_->addrs[i].len, nullptr /* args */);
    }
    grpc_arg new_arg = CreateServerAddressListChannelArg(&addresses);
    result = grpc_channel_args_copy_and_add(r->channel_args_, &new_arg, 1);
    grpc_resolved_addresses_destroy(r->addresses_);
    // Reset backoff state so that we start from the beginning when the
    // next request gets triggered.
    r->backoff_.Reset();
  } else {
    grpc_millis next_try = r->backoff_.NextAttemptTime();
    grpc_millis timeout = next_try - ExecCtx::Get()->Now();
    gpr_log(GPR_INFO, "dns resolution failed (will retry): %s",
            grpc_error_string(error));
    GPR_ASSERT(!r->have_next_resolution_timer_);
    r->have_next_resolution_timer_ = true;
    // TODO(roth): We currently deal with this ref manually.  Once the
    // new closure API is done, find a way to track this ref with the timer
    // callback as part of the type system.
    RefCountedPtr<Resolver> self =
        r->Ref(DEBUG_LOCATION, "next_resolution_timer");
    self.release();
    if (timeout > 0) {
      gpr_log(GPR_DEBUG, "retrying in %" PRId64 " milliseconds", timeout);
    } else {
      gpr_log(GPR_DEBUG, "retrying immediately");
    }
    grpc_timer_init(&r->next_resolution_timer_, next_try,
                    &r->on_next_resolution_);
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

void NativeDnsResolver::MaybeStartResolvingLocked() {
  // If there is an existing timer, the time it fires is the earliest time we
  // can start the next resolution.
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

void NativeDnsResolver::StartResolvingLocked() {
  gpr_log(GPR_DEBUG, "Start resolving.");
  // TODO(roth): We currently deal with this ref manually.  Once the
  // new closure API is done, find a way to track this ref with the timer
  // callback as part of the type system.
  RefCountedPtr<Resolver> self = Ref(DEBUG_LOCATION, "dns-resolving");
  self.release();
  GPR_ASSERT(!resolving_);
  resolving_ = true;
  addresses_ = nullptr;
  grpc_resolve_address(name_to_resolve_, kDefaultPort, interested_parties_,
                       &on_resolved_, &addresses_);
  last_resolution_timestamp_ = grpc_core::ExecCtx::Get()->Now();
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
    if (GPR_UNLIKELY(0 != strcmp(args.uri->authority, ""))) {
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
    grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
        grpc_core::UniquePtr<grpc_core::ResolverFactory>(
            grpc_core::New<grpc_core::NativeDnsResolverFactory>()));
  } else {
    grpc_core::ResolverRegistry::Builder::InitRegistry();
    grpc_core::ResolverFactory* existing_factory =
        grpc_core::ResolverRegistry::LookupResolverFactory("dns");
    if (existing_factory == nullptr) {
      gpr_log(GPR_DEBUG, "Using native dns resolver");
      grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
          grpc_core::UniquePtr<grpc_core::ResolverFactory>(
              grpc_core::New<grpc_core::NativeDnsResolverFactory>()));
    }
  }
  gpr_free(resolver_env);
}

void grpc_resolver_dns_native_shutdown() {}
