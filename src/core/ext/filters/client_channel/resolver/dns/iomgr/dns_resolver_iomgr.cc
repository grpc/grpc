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

#include <grpc/support/port_platform.h>

#include <grpc/event_engine/event_engine.h>
#include "absl/functional/bind_front.h"
#include "absl/strings/str_join.h"

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.h"
#include "src/core/ext/filters/client_channel/resolver/dns/service_config_parser.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/event_engine/resolved_address_internal.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/transport/authority_override.h"

grpc_core::TraceFlag grpc_trace_iomgr_resolver(false, "iomgr_resolver");

#define GRPC_IOMGR_DNS_TRACE_LOG(format, ...)                      \
  do {                                                             \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_iomgr_resolver)) {      \
      gpr_log(GPR_DEBUG, "(iomgr resolver) " format, __VA_ARGS__); \
    }                                                              \
  } while (0)

namespace grpc_core {

namespace {
using ::grpc_event_engine::experimental::CreateGRPCResolvedAddress;
using ::grpc_event_engine::experimental::EventEngine;
}  // namespace

class IomgrDnsResolver : public Resolver {
 public:
  explicit IomgrDnsResolver(ResolverArgs args);
  void StartLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_) override;
  void RequestReresolutionLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_) override;
  void ResetBackoffLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_) override;
  void ShutdownLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_) override;

 private:
  ~IomgrDnsResolver() override;
  //////////////////////////////////////////////////////////////////////////////
  // Callbacks execute outside the work_serializer_
  //////////////////////////////////////////////////////////////////////////////
  static void OnNextResolution(void* arg, grpc_error_handle error);
  static void OnHostnameResolved(
      IomgrDnsResolver* self,
      absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses);
  static void OnSrvResolved(
      IomgrDnsResolver* self,
      absl::StatusOr<std::vector<EventEngine::DNSResolver::SRVRecord>> records);
  static void OnBalancerResolved(
      IomgrDnsResolver* self,
      absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses);
  static void OnTxtResolved(IomgrDnsResolver* self,
                            absl::StatusOr<std::string> txt_record);

  //////////////////////////////////////////////////////////////////////////////
  // All of the remaining methods must be called from within the
  // work_serializer_
  //////////////////////////////////////////////////////////////////////////////
  void MaybeStartResolvingLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  void StartResolvingLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  void OnNextResolutionLocked(grpc_error_handle error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  void OnHostnamesResolvedLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  void OnBalancerResolvedLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  void OnSrvResolvedLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  void OnTxtResolvedLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  void FinishResolutionLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  absl::Status ParseResolvedHostnames(Result& result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  absl::Status ParseResolvedBalancerHostnames(Result& result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  absl::Status ParseResolvedServiceConfig(Result& result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  void SetRetryTimer() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  // Whether all component resolution steps are complete, and the results can be
  // processed.
  bool DoneResolving() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);

  //////////////////////////////////////////////////////////////////////////////
  // Data members
  //////////////////////////////////////////////////////////////////////////////
  /// DNS server to use (if not system default)
  std::string dns_server_;
  /// name to resolve (usually the same as target_name)
  std::string name_to_resolve_;
  /// channel args
  grpc_channel_args* channel_args_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  std::unique_ptr<ResultHandler> result_handler_;
  /// pollset_set to drive the name resolution process
  grpc_pollset_set* interested_parties_;
  /// whether to request the service config
  bool request_service_config_;
  // whether or not to enable SRV DNS queries
  bool enable_srv_queries_;
  // timeout in milliseconds for active DNS queries
  int query_timeout_ms_;
  /// min interval between DNS requests
  grpc_millis min_time_between_resolutions_;
  /// closures used by the work_serializer
  grpc_closure on_next_resolution_;
  /// are we currently resolving hostnames?
  bool resolving_hostnames_ = false;
  /// are we currently resolving srv records?
  bool resolving_srv_ = false;
  /// are we currently resolving txt records?
  bool resolving_txt_ = false;
  /// are we currently resolving balancer addresses from the SRV response?
  bool resolving_balancers_ = false;
  /// number of remaining balancer hostname queries outstanding.
  int remaining_balancer_query_count_ ABSL_GUARDED_BY(balancer_mu_);
  /// are we waiting on any of the 3 resolutions?
  bool resolution_in_progress_ = false;
  /// next resolution timer
  bool have_next_resolution_timer_ = false;
  grpc_timer next_resolution_timer_;
  /// timestamp of last DNS request
  grpc_millis last_resolution_timestamp_ = -1;
  /// retry backoff state
  BackOff backoff_;
  // has shutdown been initiated
  bool shutdown_initiated_ = false;
  // task handles for lookup cancellation
  EventEngine::DNSResolver::LookupTaskHandle host_handle_;
  EventEngine::DNSResolver::LookupTaskHandle srv_handle_;
  EventEngine::DNSResolver::LookupTaskHandle txt_handle_;
  std::vector<EventEngine::DNSResolver::LookupTaskHandle> balancer_handles_;
  // temporary storage for resolved data
  absl::StatusOr<std::vector<EventEngine::ResolvedAddress>>
      tmp_hostname_addresses_;
  absl::StatusOr<std::vector<EventEngine::DNSResolver::SRVRecord>>
      tmp_srv_records_;
  absl::StatusOr<std::vector<EventEngine::ResolvedAddress>>
      tmp_balancer_addresses_ ABSL_GUARDED_BY(balancer_mu_);
  absl::StatusOr<std::string> tmp_txt_record_;
  // pre-bound callback function for hostname resolution
  EventEngine::DNSResolver::LookupHostnameCallback on_hostnames_resolved_;
  // pre-bound callback function for srv resolution
  EventEngine::DNSResolver::LookupSRVCallback on_srv_resolved_;
  // pre-bound callback function for balancer hostname resolution
  EventEngine::DNSResolver::LookupHostnameCallback
      on_balancer_hostname_resolved_;
  // pre-bound callback function for txt resolution
  EventEngine::DNSResolver::LookupTXTCallback on_txt_resolved_;
  // All balancer callback processessing happens under this mutex.
  Mutex balancer_mu_;
};

static bool target_matches_localhost(absl::string_view name) {
  std::string host;
  std::string port;
  if (!grpc_core::SplitHostPort(name, &host, &port)) {
    gpr_log(GPR_ERROR, "Unable to split host and port for name: %s",
            std::string(name.data(), name.length()).c_str());
    return false;
  }
  return gpr_stricmp(host.c_str(), "localhost") == 0;
}

IomgrDnsResolver::IomgrDnsResolver(ResolverArgs args)
    : dns_server_(args.uri.authority()),
      name_to_resolve_(absl::StripPrefix(args.uri.path(), "/")),
      channel_args_(grpc_channel_args_copy(args.args)),
      work_serializer_(std::move(args.work_serializer)),
      result_handler_(std::move(args.result_handler)),
      interested_parties_(args.pollset_set),
      request_service_config_(!grpc_channel_args_find_bool(
          channel_args_, GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION, true)),
      enable_srv_queries_(grpc_channel_args_find_bool(
          channel_args_, GRPC_ARG_DNS_ENABLE_SRV_QUERIES, false)),
      query_timeout_ms_(grpc_channel_args_find_integer(
          channel_args_, GRPC_ARG_DNS_ARES_QUERY_TIMEOUT_MS,
          {GRPC_DNS_DEFAULT_QUERY_TIMEOUT_MS, 0, INT_MAX})),
      min_time_between_resolutions_(grpc_channel_args_find_integer(
          channel_args_, GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS,
          {1000 * 30, 0, INT_MAX})),
      backoff_(
          BackOff::Options()
              .set_initial_backoff(GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS *
                                   1000)
              .set_multiplier(GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_DNS_RECONNECT_JITTER)
              .set_max_backoff(GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)),
      on_hostnames_resolved_(
          absl::bind_front(&IomgrDnsResolver::OnHostnameResolved, this)),
      on_srv_resolved_(
          absl::bind_front(&IomgrDnsResolver::OnSrvResolved, this)),
      on_balancer_hostname_resolved_(
          absl::bind_front(&IomgrDnsResolver::OnBalancerResolved, this)),
      on_txt_resolved_(
          absl::bind_front(&IomgrDnsResolver::OnTxtResolved, this)) {
  GRPC_CLOSURE_INIT(&on_next_resolution_, OnNextResolution, this,
                    grpc_schedule_on_exec_ctx);
}

IomgrDnsResolver::~IomgrDnsResolver() {
  grpc_channel_args_destroy(channel_args_);
}

void IomgrDnsResolver::StartLocked() {
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
      GRPC_IOMGR_DNS_TRACE_LOG(
          "resolver:%p In cooldown from last resolution (from %" PRId64
          " ms ago). Will resolve again in %" PRId64 " ms",
          this, last_resolution_ago, ms_until_next_resolution);
      have_next_resolution_timer_ = true;
      // TODO(roth): We currently deal with this ref manually.  Once the
      // new closure API is done, find a way to track this ref with the timer
      // callback as part of the type system.
      Ref(DEBUG_LOCATION, "next_resolution_timer_cooldown").release();
      grpc_timer_init(&next_resolution_timer_,
                      ExecCtx::Get()->Now() + ms_until_next_resolution,
                      &on_next_resolution_);
      return;
    }
  }
  StartResolvingLocked();
}

void IomgrDnsResolver::RequestReresolutionLocked() {
  if (!resolution_in_progress_) {
    StartLocked();
  }
}

void IomgrDnsResolver::ResetBackoffLocked() {
  if (have_next_resolution_timer_) {
    grpc_timer_cancel(&next_resolution_timer_);
  }
  backoff_.Reset();
}

void IomgrDnsResolver::ShutdownLocked() {
  shutdown_initiated_ = true;
  if (have_next_resolution_timer_) {
    grpc_timer_cancel(&next_resolution_timer_);
  }
  if (resolving_hostnames_) grpc_dns_try_cancel(host_handle_);
  if (resolving_srv_) grpc_dns_try_cancel(srv_handle_);
  if (resolving_txt_) grpc_dns_try_cancel(txt_handle_);
  if (resolving_balancers_) {
    for (const auto& handle : balancer_handles_) {
      grpc_dns_try_cancel(handle);
    }
  }
  // TODO(hork): ensure no other cleanup is necessary
}

void IomgrDnsResolver::OnNextResolution(void* arg, grpc_error_handle error) {
  IomgrDnsResolver* self = static_cast<IomgrDnsResolver*>(arg);
  GRPC_ERROR_REF(error);  // ref owned by lambda
  self->work_serializer_->Run(
      [self, error]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(self->work_serializer_) {
        self->OnNextResolutionLocked(error);
      },
      DEBUG_LOCATION);
}

void IomgrDnsResolver::OnNextResolutionLocked(grpc_error_handle error) {
  GRPC_IOMGR_DNS_TRACE_LOG(
      "resolver:%p re-resolution timer fired. error: %s. shutdown_initiated_: "
      "%d",
      this, grpc_error_std_string(error).c_str(), shutdown_initiated_);
  have_next_resolution_timer_ = false;
  if (error == GRPC_ERROR_NONE && !shutdown_initiated_) {
    if (!resolution_in_progress_) {
      GRPC_IOMGR_DNS_TRACE_LOG(
          "resolver:%p start resolving due to re-resolution timer", this);
      StartResolvingLocked();
    }
  }
  Unref(DEBUG_LOCATION, "next_resolution_timer");
  GRPC_ERROR_UNREF(error);
}

void IomgrDnsResolver::StartResolvingLocked() {
  // TODO(roth): We currently deal with this ref manually.  Once the
  // new closure API is done, find a way to track this ref with the timer
  // callback as part of the type system.
  Ref(DEBUG_LOCATION, "dns-resolving").release();
  GPR_ASSERT(DoneResolving());
  GPR_ASSERT(!resolution_in_progress_);
  resolution_in_progress_ = true;
  Ref(DEBUG_LOCATION, "dns-resolving - hostnames").release();
  resolving_hostnames_ = true;
  host_handle_ = grpc_dns_lookup_hostname(
      on_hostnames_resolved_, name_to_resolve_, kDefaultSecurePort,
      ToAbslTime(
          grpc_millis_to_timespec(query_timeout_ms_, GPR_CLOCK_MONOTONIC)),
      interested_parties_);
  bool is_localhost = target_matches_localhost(name_to_resolve_);
  if (!is_localhost && enable_srv_queries_) {
    Ref(DEBUG_LOCATION, "dns-resolving - srv records").release();
    resolving_srv_ = true;
    std::string service_name = absl::StrCat("_grpclb._tcp.", name_to_resolve_);
    srv_handle_ =
        grpc_dns_lookup_srv_record(on_srv_resolved_, service_name,
                                   ToAbslTime(grpc_millis_to_timespec(
                                       query_timeout_ms_, GPR_CLOCK_MONOTONIC)),
                                   interested_parties_);
  }
  if (!is_localhost && request_service_config_) {
    Ref(DEBUG_LOCATION, "dns-resolving - txt records").release();
    resolving_txt_ = true;
    std::string config_name = absl::StrCat("_grpc_config.", name_to_resolve_);
    txt_handle_ =
        grpc_dns_lookup_txt_record(on_txt_resolved_, config_name,
                                   ToAbslTime(grpc_millis_to_timespec(
                                       query_timeout_ms_, GPR_CLOCK_MONOTONIC)),
                                   interested_parties_);
  }
  last_resolution_timestamp_ = grpc_core::ExecCtx::Get()->Now();
  GRPC_IOMGR_DNS_TRACE_LOG(
      "resolver:%p Started resolving. handles: host(%" PRIdPTR ",%" PRIdPTR
      "), srv(%" PRIdPTR ",%" PRIdPTR "), txt(%" PRIdPTR ",%" PRIdPTR ")",
      this, host_handle_.key[0], host_handle_.key[1], srv_handle_.key[0],
      srv_handle_.key[1], txt_handle_.key[0], txt_handle_.key[1]);
}

void IomgrDnsResolver::OnHostnameResolved(
    IomgrDnsResolver* self,
    absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses) {
  GPR_ASSERT(self->resolving_hostnames_);
  // hostname resolution won't occur again until `OnHostnamesResolvedLocked`
  // finishes and the `tmp_hostname_addresses_` member is cleared. It should be
  // safe to assign to `tmp_hostname_addresses_` in this callback, outside the
  // work_serializer.
  self->tmp_hostname_addresses_ = std::move(addresses);
  self->work_serializer_->Run(
      [self]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(self->work_serializer_) {
        self->OnHostnamesResolvedLocked();
      },
      DEBUG_LOCATION);
}

// Handles hostname resolution alone.
// Hostname resolution may fail if querying for SRV or TXT records, which is ok.
// If all resolution steps are complete, this triggers further processing.
// Otherwise, hostname resolution is marked as complete and the resolver waits
// for other steps to finish.
void IomgrDnsResolver::OnHostnamesResolvedLocked() {
  GPR_ASSERT(resolving_hostnames_);
  resolving_hostnames_ = false;
  if (shutdown_initiated_) {
    Unref(DEBUG_LOCATION, "OnHostnamesResolvedLocked() shutdown");
    return;
  }
  if (DoneResolving()) FinishResolutionLocked();
  Unref(DEBUG_LOCATION, "OnHostnamesResolvedLocked() complete");
}

void IomgrDnsResolver::OnBalancerResolved(
    IomgrDnsResolver* self,
    absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> balancers) {
  GPR_ASSERT(self->resolving_balancers_);
  {
    MutexLock lock(&self->balancer_mu_);
    if (self->tmp_balancer_addresses_.ok()) {
      if (!balancers.ok()) {
        self->tmp_balancer_addresses_ = balancers.status();
      } else {
        self->tmp_balancer_addresses_->insert(
            self->tmp_balancer_addresses_->end(), balancers->begin(),
            balancers->end());
      }
    }
  }
  self->work_serializer_->Run(
      [self]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(self->work_serializer_) {
        self->OnBalancerResolvedLocked();
      },
      DEBUG_LOCATION);
}

void IomgrDnsResolver::OnBalancerResolvedLocked() {
  GPR_ASSERT(resolving_balancers_);
  {
    MutexLock lock(&balancer_mu_);
    --remaining_balancer_query_count_;
    if (shutdown_initiated_) {
      if (remaining_balancer_query_count_ == 0) {
        Unref(DEBUG_LOCATION,
              "OnBalancerResolvedLocked() shutdown, final remaining balancer."
              "query");
      }
      return;
    }
    if (remaining_balancer_query_count_ != 0) return;
    resolving_balancers_ = false;
  }
  if (DoneResolving()) FinishResolutionLocked();
}

void IomgrDnsResolver::OnSrvResolved(
    IomgrDnsResolver* self,
    absl::StatusOr<std::vector<EventEngine::DNSResolver::SRVRecord>>
        srv_records) {
  GPR_ASSERT(self->resolving_srv_);
  // srv resolution won't occur again until `OnSrvResolvedLocked` finishes and
  // the `tmp_srv_records_` member is cleared. It should be safe to assign to
  // `tmp_srv_records_` in this callback, outside the work_serializer.
  self->tmp_srv_records_ = std::move(srv_records);
  self->work_serializer_->Run(
      [self]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(self->work_serializer_) {
        self->OnSrvResolvedLocked();
      },
      DEBUG_LOCATION);
}

// Handles SRV record resolution.
// If all resolution steps are complete, this triggers further processing.
// Otherwise, SRV resolution is marked as complete and the resolver waits
// for other steps to finish.
void IomgrDnsResolver::OnSrvResolvedLocked() {
  GPR_ASSERT(resolving_srv_);
  if (shutdown_initiated_) {
    Unref(DEBUG_LOCATION, "OnSrvResolvedLocked() shutdown");
    return;
  }
  if (tmp_srv_records_.ok()) {
    // Each SRV record will be queried concurrently and processed serially.
    MutexLock lock(&balancer_mu_);
    resolving_balancers_ = true;
    remaining_balancer_query_count_ = tmp_srv_records_->size();
    tmp_balancer_addresses_->clear();
    balancer_handles_.clear();
    Ref(DEBUG_LOCATION, "dns-resolving - balancers").release();
    for (const auto& srv_record : *tmp_srv_records_) {
      balancer_handles_.push_back(grpc_dns_lookup_hostname(
          on_balancer_hostname_resolved_, srv_record.host,
          std::to_string(srv_record.port),
          ToAbslTime(
              grpc_millis_to_timespec(query_timeout_ms_, GPR_CLOCK_MONOTONIC)),
          interested_parties_));
    }
  }
  resolving_srv_ = false;
  if (DoneResolving()) FinishResolutionLocked();
  Unref(DEBUG_LOCATION, "OnSrvResolvedLocked() complete");
}

void IomgrDnsResolver::OnTxtResolved(IomgrDnsResolver* self,
                                     absl::StatusOr<std::string> txt_record) {
  GPR_ASSERT(self->resolving_txt_);
  // txt resolution won't occur again until `OnTxtResolvedLocked` finishes and
  // the `tmp_txt_record_` member is cleared. It should be safe to assign to
  // `tmp_txt_record_` in this callback, outside the work_serializer.
  self->tmp_txt_record_ = std::move(txt_record);
  self->work_serializer_->Run(
      [self]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(self->work_serializer_) {
        self->OnTxtResolvedLocked();
      },
      DEBUG_LOCATION);
}

void IomgrDnsResolver::OnTxtResolvedLocked() {
  GPR_ASSERT(resolving_txt_);
  resolving_txt_ = false;
  if (shutdown_initiated_) {
    Unref(DEBUG_LOCATION, "OnTxtResolvedLocked() shutdown");
    return;
  }
  if (DoneResolving()) FinishResolutionLocked();
  Unref(DEBUG_LOCATION, "OnTxtResolvedLocked() complete");
}

void IomgrDnsResolver::FinishResolutionLocked() {
  GPR_ASSERT(DoneResolving());
  GPR_ASSERT(resolution_in_progress_);
  resolution_in_progress_ = false;
  Result result;
  std::vector<std::string> error_msgs;
  // DO NOT SUBMIT(hork): it's not an error if hostnames fail to resolve if SRV
  // or TXT queries succeed.
  absl::Status parse_error = ParseResolvedHostnames(result);
  if (!parse_error.ok()) error_msgs.emplace_back(parse_error.ToString());
  parse_error = ParseResolvedBalancerHostnames(result);
  if (!parse_error.ok()) error_msgs.emplace_back(parse_error.ToString());
  parse_error = ParseResolvedServiceConfig(result);
  if (!parse_error.ok()) error_msgs.emplace_back(parse_error.ToString());
  if (!error_msgs.empty()) {
    std::string error_msg =
        absl::StrCat("DNS query errors: ", absl::StrJoin(error_msgs, "; "));
    GRPC_IOMGR_DNS_TRACE_LOG(
        "resolver:%p dns resolution failed (will retry): %s", this,
        error_msg.c_str());
    result_handler_->ReturnError(grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_STRING_VIEW(error_msg),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
    SetRetryTimer();
    Unref(DEBUG_LOCATION, "FinishResolutionLocked() error (retrying)");
    return;
  }
  result_handler_->ReturnResult(std::move(result));
  // Reset backoff state so that we start from the beginning when the
  // next request gets triggered.
  backoff_.Reset();
}

absl::Status IomgrDnsResolver::ParseResolvedHostnames(Result& result) {
  if (!tmp_hostname_addresses_.ok()) {
    return absl::UnavailableError(
        absl::StrCat("hostname query error: '%s'",
                     tmp_hostname_addresses_.status().ToString()));
  }
  for (const auto& address : *tmp_hostname_addresses_) {
    // DO NOT SUBMIT(hork): do we need attributes for the  ServerAddress?
    result.addresses.emplace_back(CreateGRPCResolvedAddress(address), nullptr);
  }
  return absl::OkStatus();
}

absl::Status IomgrDnsResolver::ParseResolvedBalancerHostnames(Result& result) {
  if (!enable_srv_queries_) return absl::OkStatus();
  if (!tmp_srv_records_.ok()) {
    return absl::UnavailableError(absl::StrCat(
        "SRV query error: ", tmp_srv_records_.status().ToString()));
  }
  MutexLock lock(&balancer_mu_);
  if (!tmp_balancer_addresses_.ok()) {
    return absl::UnavailableError(absl::StrCat(
        "Balancer query error: ", tmp_balancer_addresses_.status().ToString()));
  }
  // DO NOT SUBMIT(hork): Needs the SRV query name, not original hostname!
  auto override_arg =
      grpc_core::CreateAuthorityOverrideChannelArg(name_to_resolve_.c_str());
  grpc_channel_args override_args = {1, &override_arg};
  if (!tmp_balancer_addresses_->empty()) {
    // Convert to ServerAddressList
    ServerAddressList* server_addr_list = new ServerAddressList();
    for (const auto& address : *tmp_balancer_addresses_) {
      server_addr_list->emplace_back(CreateGRPCResolvedAddress(address),
                                     &override_args);
    }
    absl::InlinedVector<grpc_arg, 1> new_args;
    new_args.push_back(CreateGrpclbBalancerAddressesArg(server_addr_list));
    result.args = grpc_channel_args_copy_and_add(channel_args_, new_args.data(),
                                                 new_args.size());
  }
  return absl::OkStatus();
}

absl::Status IomgrDnsResolver::ParseResolvedServiceConfig(Result& result) {
  if (!request_service_config_) return absl::OkStatus();
  if (!tmp_txt_record_.ok()) {
    return absl::UnavailableError(
        absl::StrCat("txt query error: ", tmp_txt_record_.status().ToString()));
  }
  std::string service_config_string =
      ChooseServiceConfig(*tmp_txt_record_, &result.service_config_error);
  if (result.service_config_error == GRPC_ERROR_NONE &&
      !service_config_string.empty()) {
    GRPC_IOMGR_DNS_TRACE_LOG("resolver:%p selected service config choice: %s",
                             this, service_config_string.c_str());
    result.service_config = ServiceConfig::Create(
        channel_args_, service_config_string, &result.service_config_error);
  }
  return absl::OkStatus();
}

void IomgrDnsResolver::SetRetryTimer() {
  // Set retry timer
  ExecCtx::Get()->InvalidateNow();
  grpc_millis next_try = backoff_.NextAttemptTime();
  grpc_millis timeout = next_try - ExecCtx::Get()->Now();
  GPR_ASSERT(!have_next_resolution_timer_);
  have_next_resolution_timer_ = true;
  // TODO(roth): We currently deal with this ref manually.  Once the
  // new closure API is done, find a way to track this ref with the timer
  // callback as part of the type system.
  Ref(DEBUG_LOCATION, "retry-timer").release();
  GRPC_IOMGR_DNS_TRACE_LOG("resolver:%p retrying in %" PRId64 " milliseconds",
                           this, timeout);
  grpc_timer_init(&next_resolution_timer_, next_try, &on_next_resolution_);
}

bool IomgrDnsResolver::DoneResolving() {
  return !resolving_hostnames_ && !resolving_srv_ && !resolving_txt_ &&
         !resolving_balancers_;
}

class IomgrDnsResolverFactory : public ResolverFactory {
 public:
  bool IsValidUri(const URI& /*uri*/) const override { return true; }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return MakeOrphanable<IomgrDnsResolver>(std::move(args));
  }

  const char* scheme() const override { return "dns"; }
};

}  // namespace grpc_core

void grpc_iomgr_dns_resolver_init() {
  // TODO(hork): Enable this when the Ares DNS resolver is disabled.
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      absl::make_unique<grpc_core::IomgrDnsResolverFactory>());
}

void grpc_iomgr_dns_resolver_shutdown() {}
