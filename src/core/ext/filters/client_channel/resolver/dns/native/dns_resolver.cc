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

#include "src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
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
  explicit NativeDnsResolver(ResolverArgs args);

  void StartLocked() override;

  void RequestReresolutionLocked() override;

  void ResetBackoffLocked() override;

  void ShutdownLocked() override;

 private:
  virtual ~NativeDnsResolver();

  void MaybeStartResolvingLocked();
  void StartResolvingLocked();

  static void OnNextResolution(void* arg, grpc_error* error);
  static void OnNextResolutionLocked(void* arg, grpc_error* error);
  static void OnResolved(void* arg, grpc_error* error);
  static void OnResolvedLocked(void* arg, grpc_error* error);

  /// name to resolve
  char* name_to_resolve_ = nullptr;
  /// channel args
  grpc_channel_args* channel_args_ = nullptr;
  /// pollset_set to drive the name resolution process
  grpc_pollset_set* interested_parties_ = nullptr;
  /// are we shutting down?
  bool shutdown_ = false;
  /// are we currently resolving?
  bool resolving_ = false;
  grpc_closure on_resolved_;
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

NativeDnsResolver::NativeDnsResolver(ResolverArgs args)
    : Resolver(args.combiner, std::move(args.result_handler)),
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
      grpc_channel_arg_get_integer(arg, {1000 * 30, 0, INT_MAX});
  interested_parties_ = grpc_pollset_set_create();
  if (args.pollset_set != nullptr) {
    grpc_pollset_set_add_pollset_set(interested_parties_, args.pollset_set);
  }
}

NativeDnsResolver::~NativeDnsResolver() {
  grpc_channel_args_destroy(channel_args_);
  grpc_pollset_set_destroy(interested_parties_);
  gpr_free(name_to_resolve_);
}

void NativeDnsResolver::StartLocked() { MaybeStartResolvingLocked(); }

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
  shutdown_ = true;
  if (have_next_resolution_timer_) {
    grpc_timer_cancel(&next_resolution_timer_);
  }
}

void NativeDnsResolver::OnNextResolution(void* arg, grpc_error* error) {
  NativeDnsResolver* r = static_cast<NativeDnsResolver*>(arg);
  r->combiner()->Run(
      GRPC_CLOSURE_INIT(&r->on_next_resolution_,
                        NativeDnsResolver::OnNextResolutionLocked, r, nullptr),
      GRPC_ERROR_REF(error));
}

void NativeDnsResolver::OnNextResolutionLocked(void* arg, grpc_error* error) {
  NativeDnsResolver* r = static_cast<NativeDnsResolver*>(arg);
  r->have_next_resolution_timer_ = false;
  if (error == GRPC_ERROR_NONE && !r->resolving_) {
    r->StartResolvingLocked();
  }
  r->Unref(DEBUG_LOCATION, "retry-timer");
}

void NativeDnsResolver::OnResolved(void* arg, grpc_error* error) {
  NativeDnsResolver* r = static_cast<NativeDnsResolver*>(arg);
  r->combiner()->Run(
      GRPC_CLOSURE_INIT(&r->on_resolved_, NativeDnsResolver::OnResolvedLocked,
                        r, nullptr),
      GRPC_ERROR_REF(error));
}

void NativeDnsResolver::OnResolvedLocked(void* arg, grpc_error* error) {
  NativeDnsResolver* r = static_cast<NativeDnsResolver*>(arg);
  GPR_ASSERT(r->resolving_);
  r->resolving_ = false;
  if (r->shutdown_) {
    r->Unref(DEBUG_LOCATION, "dns-resolving");
    return;
  }
  if (r->addresses_ != nullptr) {
    Result result;
    for (size_t i = 0; i < r->addresses_->naddrs; ++i) {
      result.addresses.emplace_back(&r->addresses_->addrs[i].addr,
                                    r->addresses_->addrs[i].len,
                                    nullptr /* args */);
    }
    grpc_resolved_addresses_destroy(r->addresses_);
    result.args = grpc_channel_args_copy(r->channel_args_);
    r->result_handler()->ReturnResult(std::move(result));
    // Reset backoff state so that we start from the beginning when the
    // next request gets triggered.
    r->backoff_.Reset();
  } else {
    gpr_log(GPR_INFO, "dns resolution failed (will retry): %s",
            grpc_error_string(error));
    // Return transient error.
    r->result_handler()->ReturnError(grpc_error_set_int(
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "DNS resolution failed", &error, 1),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
    // Set up for retry.
    grpc_millis next_try = r->backoff_.NextAttemptTime();
    grpc_millis timeout = next_try - ExecCtx::Get()->Now();
    GPR_ASSERT(!r->have_next_resolution_timer_);
    r->have_next_resolution_timer_ = true;
    // TODO(roth): We currently deal with this ref manually.  Once the
    // new closure API is done, find a way to track this ref with the timer
    // callback as part of the type system.
    r->Ref(DEBUG_LOCATION, "next_resolution_timer").release();
    if (timeout > 0) {
      gpr_log(GPR_DEBUG, "retrying in %" PRId64 " milliseconds", timeout);
    } else {
      gpr_log(GPR_DEBUG, "retrying immediately");
    }
    GRPC_CLOSURE_INIT(&r->on_next_resolution_,
                      NativeDnsResolver::OnNextResolution, r,
                      grpc_schedule_on_exec_ctx);
    grpc_timer_init(&r->next_resolution_timer_, next_try,
                    &r->on_next_resolution_);
  }
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
      Ref(DEBUG_LOCATION, "next_resolution_timer_cooldown").release();
      GRPC_CLOSURE_INIT(&on_next_resolution_,
                        NativeDnsResolver::OnNextResolution, this,
                        grpc_schedule_on_exec_ctx);
      grpc_timer_init(&next_resolution_timer_,
                      ExecCtx::Get()->Now() + ms_until_next_resolution,
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
  Ref(DEBUG_LOCATION, "dns-resolving").release();
  GPR_ASSERT(!resolving_);
  resolving_ = true;
  addresses_ = nullptr;
  GRPC_CLOSURE_INIT(&on_resolved_, NativeDnsResolver::OnResolved, this,
                    grpc_schedule_on_exec_ctx);
  grpc_resolve_address(name_to_resolve_, kDefaultPort, interested_parties_,
                       &on_resolved_, &addresses_);
  last_resolution_timestamp_ = grpc_core::ExecCtx::Get()->Now();
}

//
// Factory
//

class NativeDnsResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const grpc_uri* uri) const override {
    if (GPR_UNLIKELY(0 != strcmp(uri->authority, ""))) {
      gpr_log(GPR_ERROR, "authority based dns uri's not supported");
      return false;
    }
    return true;
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    if (!IsValidUri(args.uri)) return nullptr;
    return MakeOrphanable<NativeDnsResolver>(std::move(args));
  }

  const char* scheme() const override { return "dns"; }
};

}  // namespace

}  // namespace grpc_core

void grpc_resolver_dns_native_init() {
  grpc_core::UniquePtr<char> resolver =
      GPR_GLOBAL_CONFIG_GET(grpc_dns_resolver);
  if (gpr_stricmp(resolver.get(), "native") == 0) {
    gpr_log(GPR_DEBUG, "Using native dns resolver");
    grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
        absl::make_unique<grpc_core::NativeDnsResolverFactory>());
  } else {
    grpc_core::ResolverRegistry::Builder::InitRegistry();
    grpc_core::ResolverFactory* existing_factory =
        grpc_core::ResolverRegistry::LookupResolverFactory("dns");
    if (existing_factory == nullptr) {
      gpr_log(GPR_DEBUG, "Using native dns resolver");
      grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
          absl::make_unique<grpc_core::NativeDnsResolverFactory>());
    }
  }
}

void grpc_resolver_dns_native_shutdown() {}
