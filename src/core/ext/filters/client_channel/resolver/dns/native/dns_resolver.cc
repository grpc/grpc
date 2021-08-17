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

#include "absl/strings/str_cat.h"

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
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/work_serializer.h"

#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2

namespace grpc_core {

namespace {

class NativeDnsResolver : public Resolver {
 public:
  explicit NativeDnsResolver(ResolverArgs args);

  void StartLocked() override;

  void RequestReresolutionLocked() override;

  void ResetBackoffLocked() override;

  void ShutdownLocked() override;

 private:
  ~NativeDnsResolver() override;

  void MaybeStartResolvingLocked();
  void StartResolvingLocked();

  static void OnNextResolution(void* arg, grpc_error_handle error);
  void OnNextResolutionLocked(grpc_error_handle error);
  static void OnResolved(void* arg, grpc_error_handle error);
  void OnResolvedLocked(grpc_error_handle error);

  /// name to resolve
  std::string name_to_resolve_;
  /// channel args
  grpc_channel_args* channel_args_ = nullptr;
  std::shared_ptr<WorkSerializer> work_serializer_;
  std::unique_ptr<ResultHandler> result_handler_;
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
    : name_to_resolve_(absl::StripPrefix(args.uri.path(), "/")),
      channel_args_(grpc_channel_args_copy(args.args)),
      work_serializer_(std::move(args.work_serializer)),
      result_handler_(std::move(args.result_handler)),
      interested_parties_(grpc_pollset_set_create()),
      min_time_between_resolutions_(grpc_channel_args_find_integer(
          channel_args_, GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS,
          {1000 * 30, 0, INT_MAX})),
      backoff_(
          BackOff::Options()
              .set_initial_backoff(GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS *
                                   1000)
              .set_multiplier(GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_DNS_RECONNECT_JITTER)
              .set_max_backoff(GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)) {
  if (args.pollset_set != nullptr) {
    grpc_pollset_set_add_pollset_set(interested_parties_, args.pollset_set);
  }
}

NativeDnsResolver::~NativeDnsResolver() {
  grpc_channel_args_destroy(channel_args_);
  grpc_pollset_set_destroy(interested_parties_);
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

void NativeDnsResolver::OnNextResolution(void* arg, grpc_error_handle error) {
  NativeDnsResolver* r = static_cast<NativeDnsResolver*>(arg);
  GRPC_ERROR_REF(error);  // ref owned by lambda
  r->work_serializer_->Run([r, error]() { r->OnNextResolutionLocked(error); },
                           DEBUG_LOCATION);
}

void NativeDnsResolver::OnNextResolutionLocked(grpc_error_handle error) {
  have_next_resolution_timer_ = false;
  if (error == GRPC_ERROR_NONE && !resolving_) {
    StartResolvingLocked();
  }
  Unref(DEBUG_LOCATION, "retry-timer");
  GRPC_ERROR_UNREF(error);
}

void NativeDnsResolver::OnResolved(void* arg, grpc_error_handle error) {
  NativeDnsResolver* r = static_cast<NativeDnsResolver*>(arg);
  GRPC_ERROR_REF(error);  // owned by lambda
  r->work_serializer_->Run([r, error]() { r->OnResolvedLocked(error); },
                           DEBUG_LOCATION);
}

void NativeDnsResolver::OnResolvedLocked(grpc_error_handle error) {
  GPR_ASSERT(resolving_);
  resolving_ = false;
  if (shutdown_) {
    Unref(DEBUG_LOCATION, "dns-resolving");
    GRPC_ERROR_UNREF(error);
    return;
  }
  if (addresses_ != nullptr) {
    Result result;
    for (size_t i = 0; i < addresses_->naddrs; ++i) {
      result.addresses.emplace_back(&addresses_->addrs[i].addr,
                                    addresses_->addrs[i].len,
                                    nullptr /* args */);
    }
    grpc_resolved_addresses_destroy(addresses_);
    result.args = grpc_channel_args_copy(channel_args_);
    result_handler_->ReturnResult(std::move(result));
    // Reset backoff state so that we start from the beginning when the
    // next request gets triggered.
    backoff_.Reset();
  } else {
    gpr_log(GPR_INFO, "dns resolution failed (will retry): %s",
            grpc_error_std_string(error).c_str());
    // Return transient error.
    std::string error_message =
        absl::StrCat("DNS resolution failed for service: ", name_to_resolve_);
    result_handler_->ReturnError(grpc_error_set_int(
        GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(error_message.c_str(),
                                                         &error, 1),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
    // Set up for retry.
    // InvalidateNow to avoid getting stuck re-initializing this timer
    // in a loop while draining the currently-held WorkSerializer.
    // Also see https://github.com/grpc/grpc/issues/26079.
    ExecCtx::Get()->InvalidateNow();
    grpc_millis next_try = backoff_.NextAttemptTime();
    grpc_millis timeout = next_try - ExecCtx::Get()->Now();
    GPR_ASSERT(!have_next_resolution_timer_);
    have_next_resolution_timer_ = true;
    // TODO(roth): We currently deal with this ref manually.  Once the
    // new closure API is done, find a way to track this ref with the timer
    // callback as part of the type system.
    Ref(DEBUG_LOCATION, "next_resolution_timer").release();
    if (timeout > 0) {
      gpr_log(GPR_DEBUG, "retrying in %" PRId64 " milliseconds", timeout);
    } else {
      gpr_log(GPR_DEBUG, "retrying immediately");
    }
    GRPC_CLOSURE_INIT(&on_next_resolution_, NativeDnsResolver::OnNextResolution,
                      this, grpc_schedule_on_exec_ctx);
    grpc_timer_init(&next_resolution_timer_, next_try, &on_next_resolution_);
  }
  Unref(DEBUG_LOCATION, "dns-resolving");
  GRPC_ERROR_UNREF(error);
}

void NativeDnsResolver::MaybeStartResolvingLocked() {
  // If there is an existing timer, the time it fires is the earliest time we
  // can start the next resolution.
  if (have_next_resolution_timer_) return;
  if (last_resolution_timestamp_ >= 0) {
    // InvalidateNow to avoid getting stuck re-initializing this timer
    // in a loop while draining the currently-held WorkSerializer.
    // Also see https://github.com/grpc/grpc/issues/26079.
    ExecCtx::Get()->InvalidateNow();
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
  grpc_resolve_address(name_to_resolve_.c_str(), kDefaultSecurePort,
                       interested_parties_, &on_resolved_, &addresses_);
  last_resolution_timestamp_ = grpc_core::ExecCtx::Get()->Now();
}

//
// Factory
//

class NativeDnsResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const URI& uri) const override {
    if (GPR_UNLIKELY(!uri.authority().empty())) {
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
