//
// Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <inttypes.h>

#include <climits>
#include <cstring>

#include "absl/functional/bind_front.h"
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

class NativeClientChannelDNSResolver : public Resolver {
 public:
  explicit NativeClientChannelDNSResolver(ResolverArgs args);

  void StartLocked() override;

  void RequestReresolutionLocked() override;

  void ResetBackoffLocked() override;

  void ShutdownLocked() override;

 private:
  ~NativeClientChannelDNSResolver() override;

  void MaybeStartResolvingLocked();
  void StartResolvingLocked();

  static void OnNextResolution(void* arg, grpc_error_handle error);
  void OnNextResolutionLocked(grpc_error_handle error);
  void OnResolved(
      absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or);
  void OnResolvedLocked(
      absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or);

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
  /// tracks pending resolutions
  OrphanablePtr<DNSResolver::Request> dns_request_;
};

NativeClientChannelDNSResolver::NativeClientChannelDNSResolver(
    ResolverArgs args)
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

NativeClientChannelDNSResolver::~NativeClientChannelDNSResolver() {
  grpc_channel_args_destroy(channel_args_);
  grpc_pollset_set_destroy(interested_parties_);
}

void NativeClientChannelDNSResolver::StartLocked() {
  MaybeStartResolvingLocked();
}

void NativeClientChannelDNSResolver::RequestReresolutionLocked() {
  if (!resolving_) {
    MaybeStartResolvingLocked();
  }
}

void NativeClientChannelDNSResolver::ResetBackoffLocked() {
  if (have_next_resolution_timer_) {
    grpc_timer_cancel(&next_resolution_timer_);
  }
  backoff_.Reset();
}

void NativeClientChannelDNSResolver::ShutdownLocked() {
  shutdown_ = true;
  if (have_next_resolution_timer_) {
    grpc_timer_cancel(&next_resolution_timer_);
  }
  dns_request_.reset();
}

void NativeClientChannelDNSResolver::OnNextResolution(void* arg,
                                                      grpc_error_handle error) {
  NativeClientChannelDNSResolver* r =
      static_cast<NativeClientChannelDNSResolver*>(arg);
  (void)GRPC_ERROR_REF(error);  // ref owned by lambda
  r->work_serializer_->Run([r, error]() { r->OnNextResolutionLocked(error); },
                           DEBUG_LOCATION);
}

void NativeClientChannelDNSResolver::OnNextResolutionLocked(
    grpc_error_handle error) {
  have_next_resolution_timer_ = false;
  if (error == GRPC_ERROR_NONE && !resolving_) {
    StartResolvingLocked();
  }
  Unref(DEBUG_LOCATION, "retry-timer");
  GRPC_ERROR_UNREF(error);
}

void NativeClientChannelDNSResolver::OnResolved(
    absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or) {
  work_serializer_->Run(
      [this, addresses_or]() { OnResolvedLocked(addresses_or); },
      DEBUG_LOCATION);
}

void NativeClientChannelDNSResolver::OnResolvedLocked(
    absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or) {
  GPR_ASSERT(resolving_);
  resolving_ = false;
  dns_request_.reset();
  if (shutdown_) {
    Unref(DEBUG_LOCATION, "dns-resolving");
    return;
  }
  if (addresses_or.ok()) {
    ServerAddressList addresses;
    for (auto& addr : *addresses_or) {
      addresses.emplace_back(addr, nullptr /* args */);
    }
    Result result;
    result.addresses = std::move(addresses);
    result.args = grpc_channel_args_copy(channel_args_);
    result_handler_->ReportResult(std::move(result));
    // Reset backoff state so that we start from the beginning when the
    // next request gets triggered.
    backoff_.Reset();
  } else {
    std::string error_message = addresses_or.status().ToString();
    gpr_log(GPR_INFO, "dns resolution failed (will retry): %s",
            error_message.c_str());
    // Return transient error.
    Result result;
    result.addresses = absl::UnavailableError(absl::StrCat(
        "DNS resolution failed for ", name_to_resolve_, ": ", error_message));
    result.args = grpc_channel_args_copy(channel_args_);
    result_handler_->ReportResult(std::move(result));
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
    GRPC_CLOSURE_INIT(&on_next_resolution_,
                      NativeClientChannelDNSResolver::OnNextResolution, this,
                      grpc_schedule_on_exec_ctx);
    grpc_timer_init(&next_resolution_timer_, next_try, &on_next_resolution_);
  }
  Unref(DEBUG_LOCATION, "dns-resolving");
}

void NativeClientChannelDNSResolver::MaybeStartResolvingLocked() {
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
        earliest_next_resolution - ExecCtx::Get()->Now();
    if (ms_until_next_resolution > 0) {
      const grpc_millis last_resolution_ago =
          ExecCtx::Get()->Now() - last_resolution_timestamp_;
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
                        NativeClientChannelDNSResolver::OnNextResolution, this,
                        grpc_schedule_on_exec_ctx);
      grpc_timer_init(&next_resolution_timer_,
                      ExecCtx::Get()->Now() + ms_until_next_resolution,
                      &on_next_resolution_);
      return;
    }
  }
  StartResolvingLocked();
}

void NativeClientChannelDNSResolver::StartResolvingLocked() {
  gpr_log(GPR_DEBUG, "Start resolving.");
  // TODO(roth): We currently deal with this ref manually.  Once the
  // new closure API is done, find a way to track this ref with the timer
  // callback as part of the type system.
  Ref(DEBUG_LOCATION, "dns-resolving").release();
  GPR_ASSERT(!resolving_);
  resolving_ = true;
  dns_request_ = GetDNSResolver()->ResolveName(
      name_to_resolve_, kDefaultSecurePort, interested_parties_,
      absl::bind_front(&NativeClientChannelDNSResolver::OnResolved, this));
  dns_request_->Start();
  last_resolution_timestamp_ = ExecCtx::Get()->Now();
}

//
// Factory
//

class NativeClientChannelDNSResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const URI& uri) const override {
    if (GPR_UNLIKELY(!uri.authority().empty())) {
      gpr_log(GPR_ERROR, "authority based dns uri's not supported");
      return false;
    }
    if (absl::StripPrefix(uri.path(), "/").empty()) {
      gpr_log(GPR_ERROR, "no server name supplied in dns URI");
      return false;
    }
    return true;
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    if (!IsValidUri(args.uri)) return nullptr;
    return MakeOrphanable<NativeClientChannelDNSResolver>(std::move(args));
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
        absl::make_unique<grpc_core::NativeClientChannelDNSResolverFactory>());
  } else {
    grpc_core::ResolverRegistry::Builder::InitRegistry();
    grpc_core::ResolverFactory* existing_factory =
        grpc_core::ResolverRegistry::LookupResolverFactory("dns");
    if (existing_factory == nullptr) {
      gpr_log(GPR_DEBUG, "Using native dns resolver");
      grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
          absl::make_unique<
              grpc_core::NativeClientChannelDNSResolverFactory>());
    }
  }
}

void grpc_resolver_dns_native_shutdown() {}
