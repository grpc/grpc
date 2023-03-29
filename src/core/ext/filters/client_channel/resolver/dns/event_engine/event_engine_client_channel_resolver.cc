// Copyright 2023 The gRPC Authors
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

#include "src/core/ext/filters/client_channel/resolver/dns/event_engine/event_engine_client_channel_resolver.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.h"
#include "src/core/ext/filters/client_channel/resolver/dns/event_engine/service_config_helper.h"
#include "src/core/ext/filters/client_channel/resolver/polling_resolver.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/resolved_address_internal.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/service_config/service_config_impl.h"

// IWYU pragma: no_include <ratio>

namespace grpc_core {
namespace {

#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2
#define GRPC_DNS_DEFAULT_QUERY_TIMEOUT_MS 120000

using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::LookupTaskHandleSet;

TraceFlag grpc_event_engine_client_channel_resolver_trace(
    false, "event_engine_client_channel_resolver");

#define GRPC_EVENT_ENGINE_RESOLVER_TRACE(format, ...)                    \
  if (GRPC_TRACE_FLAG_ENABLED(                                           \
          grpc_event_engine_client_channel_resolver_trace)) {            \
    gpr_log(GPR_DEBUG, "(event_engine client channel resolver) " format, \
            __VA_ARGS__);                                                \
  }

// ----------------------------------------------------------------------------
// EventEngineClientChannelDNSResolver
// ----------------------------------------------------------------------------
class EventEngineClientChannelDNSResolver : public PollingResolver {
 public:
  EventEngineClientChannelDNSResolver(ResolverArgs args,
                                      Duration min_time_between_resolutions);
  OrphanablePtr<Orphanable> StartRequest() override;

 private:
  // ----------------------------------------------------------------------------
  // EventEngineDNSRequestWrapper declaration
  // ----------------------------------------------------------------------------
  class EventEngineDNSRequestWrapper
      : public InternallyRefCounted<EventEngineDNSRequestWrapper> {
   public:
    EventEngineDNSRequestWrapper(
        RefCountedPtr<EventEngineClientChannelDNSResolver> resolver,
        std::unique_ptr<EventEngine::DNSResolver> event_engine_resolver);
    ~EventEngineDNSRequestWrapper() override;

    // Note that thread safety cannot be analyzed due to this being invoked from
    // OrphanablePtr<>, and there's no way to pass the lock annotation through
    // there.
    void Orphan() override ABSL_NO_THREAD_SAFETY_ANALYSIS;

    void OnHostnameResolved(
        absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses);
    void OnSRVResolved(
        absl::StatusOr<std::vector<EventEngine::DNSResolver::SRVRecord>>
            srv_records);
    void OnBalancerHostnamesResolved(
        std::string authority,
        absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses);
    void OnTXTResolved(absl::StatusOr<std::string> service_config);

    // Returns a Result if resolution is complete.
    // callers must release the lock and call OnRequestComplete if a Result is
    // returned. This is because OnRequestComplete may Orphan the resolver,
    // which requires taking the lock.
    absl::optional<Resolver::Result> OnResolvedLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(on_resolved_mu_);

   private:
    // Helper method for generating the combined resolution status error
    absl::Status GetResolutionFailureErrorMessageLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(on_resolved_mu_);
    // Helper method to populate server addresses on resolver result.
    void MaybePopulateAddressesLocked(Resolver::Result& result)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(on_resolved_mu_);
    // Helper method to populate balancer addresses on resolver result.
    void MaybePopulateBalancerAddressesLocked(Resolver::Result& result)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(on_resolved_mu_);
    // Helper method to populate service config on resolver result.
    void MaybePopulateServiceConfigLocked(Resolver::Result& result)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(on_resolved_mu_);
    // Convert empty results into resolution errors.
    //
    // TODO(roth): We should someday separate empty results (NXDOMAIN) from
    // actual DNS resolution errors. For now, they are treated the same so this
    // resolver behaves similarly to the old c-ares resolver.
    void NormalizeEmptyResultsToErrorsLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(on_resolved_mu_);

    RefCountedPtr<EventEngineClientChannelDNSResolver> resolver_;
    Mutex on_resolved_mu_;
    // Lookup callbacks
    absl::optional<EventEngine::DNSResolver::LookupTaskHandle> hostname_handle_
        ABSL_GUARDED_BY(on_resolved_mu_);
    absl::optional<EventEngine::DNSResolver::LookupTaskHandle> srv_handle_
        ABSL_GUARDED_BY(on_resolved_mu_);
    absl::optional<EventEngine::DNSResolver::LookupTaskHandle> txt_handle_
        ABSL_GUARDED_BY(on_resolved_mu_);
    LookupTaskHandleSet balancer_hostname_handles_
        ABSL_GUARDED_BY(on_resolved_mu_);
    // Output fields from requests.
    absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses_
        ABSL_GUARDED_BY(on_resolved_mu_);
    ServerAddressList balancer_addresses_ ABSL_GUARDED_BY(on_resolved_mu_);
    ValidationErrors balancer_address_lookup_errors_
        ABSL_GUARDED_BY(on_resolved_mu_);
    absl::StatusOr<std::string> service_config_json_
        ABSL_GUARDED_BY(on_resolved_mu_);
    // Other internal state
    size_t number_of_balancer_hostnames_resolved_
        ABSL_GUARDED_BY(on_resolved_mu_) = 0;
    bool orphaned_ ABSL_GUARDED_BY(on_resolved_mu_) = false;
    std::unique_ptr<EventEngine::DNSResolver> event_engine_resolver_;
  };

  /// whether to request the service config
  const bool request_service_config_;
  // whether or not to enable SRV DNS queries
  const bool enable_srv_queries_;
  // timeout in milliseconds for active DNS queries
  EventEngine::Duration query_timeout_ms_;
  std::shared_ptr<EventEngine> event_engine_;
};

EventEngineClientChannelDNSResolver::EventEngineClientChannelDNSResolver(
    ResolverArgs args, Duration min_time_between_resolutions)
    : PollingResolver(std::move(args), min_time_between_resolutions,
                      BackOff::Options()
                          .set_initial_backoff(Duration::Milliseconds(
                              GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS * 1000))
                          .set_multiplier(GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER)
                          .set_jitter(GRPC_DNS_RECONNECT_JITTER)
                          .set_max_backoff(Duration::Milliseconds(
                              GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)),
                      &grpc_event_engine_client_channel_resolver_trace),
      request_service_config_(
          !channel_args()
               .GetBool(GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION)
               .value_or(true)),
      enable_srv_queries_(channel_args()
                              .GetBool(GRPC_ARG_DNS_ENABLE_SRV_QUERIES)
                              .value_or(false)),
      // TODO(yijiem): decide if the ares channel arg timeout should be reused.
      query_timeout_ms_(std::chrono::milliseconds(
          std::max(0, channel_args()
                          .GetInt(GRPC_ARG_DNS_ARES_QUERY_TIMEOUT_MS)
                          .value_or(GRPC_DNS_DEFAULT_QUERY_TIMEOUT_MS)))),
      event_engine_(channel_args().GetObjectRef<EventEngine>()) {}

OrphanablePtr<Orphanable> EventEngineClientChannelDNSResolver::StartRequest() {
  return MakeOrphanable<EventEngineDNSRequestWrapper>(
      Ref(DEBUG_LOCATION, "dns-resolving"),
      event_engine_->GetDNSResolver({/*dns_server=*/authority()}));
}

// ----------------------------------------------------------------------------
// EventEngineDNSRequestWrapper definition
// ----------------------------------------------------------------------------

EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    EventEngineDNSRequestWrapper(
        RefCountedPtr<EventEngineClientChannelDNSResolver> resolver,
        std::unique_ptr<EventEngine::DNSResolver> event_engine_resolver)
    : resolver_(std::move(resolver)),
      event_engine_resolver_(std::move(event_engine_resolver)) {
  // Locking to prevent completion before all records are queried
  MutexLock lock(&on_resolved_mu_);
  hostname_handle_ = event_engine_resolver_->LookupHostname(
      absl::bind_front(&EventEngineDNSRequestWrapper::OnHostnameResolved,
                       Ref(DEBUG_LOCATION, "OnHostnameResolved")),
      resolver_->name_to_resolve(), kDefaultSecurePort,
      resolver_->query_timeout_ms_);
  GRPC_EVENT_ENGINE_RESOLVER_TRACE(
      "DNSResolver::%p Started resolving hostname. Handle::%s", resolver_.get(),
      HandleToString(*hostname_handle_).c_str());
  if (resolver_->enable_srv_queries_) {
    srv_handle_ = event_engine_resolver_->LookupSRV(
        absl::bind_front(&EventEngineDNSRequestWrapper::OnSRVResolved,
                         Ref(DEBUG_LOCATION, "OnSRVResolved")),
        resolver_->name_to_resolve(), resolver_->query_timeout_ms_);
    GRPC_EVENT_ENGINE_RESOLVER_TRACE(
        "DNSResolver::%p Started resolving SRV. Handle::%s", resolver_.get(),
        HandleToString(*srv_handle_).c_str());
  }
  if (resolver_->request_service_config_) {
    txt_handle_ = event_engine_resolver_->LookupTXT(
        absl::bind_front(&EventEngineDNSRequestWrapper::OnTXTResolved,
                         Ref(DEBUG_LOCATION, "OnTXTResolved")),
        resolver_->name_to_resolve(), resolver_->query_timeout_ms_);
    GRPC_EVENT_ENGINE_RESOLVER_TRACE(
        "DNSResolver::%p Started resolving TXT. Handle::%s", resolver_.get(),
        HandleToString(*txt_handle_).c_str());
  }
}

EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    ~EventEngineDNSRequestWrapper() {
  resolver_.reset(DEBUG_LOCATION, "dns-resolving");
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    Orphan() {
  {
    MutexLock lock(&on_resolved_mu_);
    orphaned_ = true;
    // Event if cancellation fails here, OnResolvedLocked will return early, and
    // the resolver will never see a completed request.
    if (hostname_handle_.has_value()) {
      event_engine_resolver_->CancelLookup(*hostname_handle_);
    }
    if (srv_handle_.has_value()) {
      event_engine_resolver_->CancelLookup(*srv_handle_);
    }
    for (const auto& handle : balancer_hostname_handles_) {
      event_engine_resolver_->CancelLookup(handle);
    }
    if (txt_handle_.has_value()) {
      event_engine_resolver_->CancelLookup(*txt_handle_);
    }
  }
  Unref(DEBUG_LOCATION, "Orphan");
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    OnHostnameResolved(
        absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses) {
  absl::optional<Resolver::Result> result;
  {
    MutexLock lock(&on_resolved_mu_);
    if (orphaned_) return;
    hostname_handle_.reset();
    addresses_ = addresses;
    result = OnResolvedLocked();
  }
  if (result.has_value()) {
    resolver_->OnRequestComplete(std::move(*result));
  }
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    OnSRVResolved(
        absl::StatusOr<std::vector<EventEngine::DNSResolver::SRVRecord>>
            srv_records) {
  absl::optional<Resolver::Result> result;
  auto cleanup = absl::MakeCleanup([&]() {
    if (result.has_value()) {
      resolver_->OnRequestComplete(std::move(*result));
    }
  });
  {
    MutexLock lock(&on_resolved_mu_);
    if (orphaned_) return;
    srv_handle_.reset();
    if (!srv_records.ok()) {
      // An error has occurred, finish resolving.
      balancer_address_lookup_errors_.AddError(srv_records.status().ToString());
      result = OnResolvedLocked();
      return;
    }
    // Do a subsequent hostname query since SRV records were returned
    for (const auto& srv_record : *srv_records) {
      balancer_hostname_handles_.insert(event_engine_resolver_->LookupHostname(
          absl::bind_front(
              &EventEngineDNSRequestWrapper::OnBalancerHostnamesResolved,
              Ref(DEBUG_LOCATION, "OnBalancerHostnamesResolved"),
              srv_record.host),
          srv_record.host, std::to_string(srv_record.port),
          resolver_->query_timeout_ms_));
      GRPC_EVENT_ENGINE_RESOLVER_TRACE(
          "DNSResolver::%p Started resolving balancer hostname %s:%d",
          resolver_.get(), srv_record.host.c_str(), srv_record.port);
    }
  }
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    OnBalancerHostnamesResolved(
        std::string authority,
        absl::StatusOr<std::vector<EventEngine::ResolvedAddress>>
            new_balancer_addresses) {
  absl::optional<Resolver::Result> result;
  auto cleanup = absl::MakeCleanup([&]() {
    if (result.has_value()) {
      resolver_->OnRequestComplete(std::move(*result));
    }
  });
  {
    MutexLock lock(&on_resolved_mu_);
    if (orphaned_) return;
    ++number_of_balancer_hostnames_resolved_;
    if (!new_balancer_addresses.ok()) {
      // An error has occurred, finish resolving.
      balancer_address_lookup_errors_.AddError(absl::StrCat(
          authority, ": ", new_balancer_addresses.status().ToString()));
    } else {
      // Capture the addresses and finish resolving.
      balancer_addresses_.reserve(balancer_addresses_.size() +
                                  new_balancer_addresses->size());
      auto srv_channel_args =
          resolver_->channel_args().Set(GRPC_ARG_DEFAULT_AUTHORITY, authority);
      for (const auto& addr : *new_balancer_addresses) {
        balancer_addresses_.emplace_back(CreateGRPCResolvedAddress(addr),
                                         srv_channel_args);
      }
    }
    result = OnResolvedLocked();
  }
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    OnTXTResolved(absl::StatusOr<std::string> service_config) {
  absl::optional<Resolver::Result> result;
  {
    MutexLock lock(&on_resolved_mu_);
    if (orphaned_) return;
    GPR_ASSERT(txt_handle_.has_value());
    txt_handle_.reset();
    service_config_json_ = service_config;
    result = OnResolvedLocked();
  }
  if (result.has_value()) {
    resolver_->OnRequestComplete(std::move(*result));
  }
}

absl::Status EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    GetResolutionFailureErrorMessageLocked() {
  ValidationErrors all_errors;
  if (!addresses_.ok()) {
    all_errors.AddError(absl::StrCat("Error resolving hostname: ",
                                     addresses_.status().message()));
  }
  if (balancer_address_lookup_errors_.FieldHasErrors()) {
    all_errors.AddError(balancer_address_lookup_errors_
                            .status("Errors looking up balancer addresses")
                            .message());
  }
  if (!service_config_json_.ok()) {
    all_errors.AddError(absl::StrCat("Error resolving TXT records: ",
                                     service_config_json_.status().message()));
  }
  return all_errors.status(
      absl::StrFormat("resolver:%p dns resolution failed.", this),
      absl::StatusCode::kUnavailable);
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    MaybePopulateAddressesLocked(Resolver::Result& result) {
  if (addresses_.ok()) {
    result.addresses->reserve(addresses_->size());
    for (const auto& addr : *addresses_) {
      result.addresses->emplace_back(CreateGRPCResolvedAddress(addr),
                                     resolver_->channel_args());
    }
  }
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    MaybePopulateBalancerAddressesLocked(Resolver::Result& result) {
  if (!balancer_addresses_.empty()) {
    result.args = SetGrpcLbBalancerAddresses(result.args, balancer_addresses_);
  }
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    MaybePopulateServiceConfigLocked(Resolver::Result& result) {
  // TODO(roth): We should not ignore errors here, but instead have users of the
  // resolver handle them appropriately. Some test/cpp/naming tests fail.
  if (service_config_json_.ok() && service_config_json_->empty()) {
    result.service_config = service_config_json_.status();
  }
  if (service_config_json_.ok() && !service_config_json_->empty()) {
    auto service_config = ChooseServiceConfig(*service_config_json_);
    if (!service_config.ok()) {
      result.service_config = absl::UnavailableError(
          absl::StrCat("failed to parse service config: ",
                       service_config.status().message()));
    } else if (!service_config->empty()) {
      GRPC_EVENT_ENGINE_RESOLVER_TRACE(
          "resolver:%p selected service config choice: %s", this,
          service_config->c_str());
      result.service_config =
          ServiceConfigImpl::Create(resolver_->channel_args(), *service_config);
      if (!result.service_config.ok()) {
        result.service_config = absl::UnavailableError(
            absl::StrCat("failed to parse service config: ",
                         result.service_config.status().message()));
      }
    }
  }
  if (balancer_address_lookup_errors_.FieldHasErrors()) {
    result.resolution_note = std::string(
        balancer_address_lookup_errors_.status("SRV Lookup error").message());
  }
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    NormalizeEmptyResultsToErrorsLocked() {
  if (addresses_.ok() && addresses_->empty()) {
    addresses_ = absl::NotFoundError("No addresses found.");
  }
}

absl::optional<Resolver::Result> EventEngineClientChannelDNSResolver::
    EventEngineDNSRequestWrapper::OnResolvedLocked() {
  if (orphaned_) return absl::nullopt;
  // Wait for all requested queries to return.
  if (hostname_handle_.has_value() || srv_handle_.has_value() ||
      txt_handle_.has_value() ||
      number_of_balancer_hostnames_resolved_ !=
          balancer_hostname_handles_.size()) {
    GRPC_EVENT_ENGINE_RESOLVER_TRACE(
        "resolver:%p OnResolved() waiting for results (hostname: %s, "
        "srv: %s, "
        "txt: %s, "
        "balancer addresses: %" PRIuPTR "/%" PRIuPTR " complete",
        this, hostname_handle_.has_value() ? "waiting" : "done",
        srv_handle_.has_value() ? "waiting" : "done",
        txt_handle_.has_value() ? "waiting" : "done",
        number_of_balancer_hostnames_resolved_,
        balancer_hostname_handles_.size());
    return absl::nullopt;
  }
  GRPC_EVENT_ENGINE_RESOLVER_TRACE("resolver:%p OnResolvedLocked() proceeding",
                                   this);
  Resolver::Result result;
  result.args = resolver_->channel_args();
  NormalizeEmptyResultsToErrorsLocked();
  // If both addresses and balancer addresses failed, return an error for //
  // both addresses and service config.
  if (!addresses_.ok() && balancer_addresses_.empty()) {
    absl::Status status = GetResolutionFailureErrorMessageLocked();
    GRPC_EVENT_ENGINE_RESOLVER_TRACE("%s", status.ToString().c_str());
    result.addresses = status;
    result.service_config = status;
    return std::move(result);
  }
  // We have at least one of addresses or balancer addresses, so we're going to
  // return a non-error for addresses.
  result.addresses.emplace();
  MaybePopulateAddressesLocked(result);
  MaybePopulateServiceConfigLocked(result);
  MaybePopulateBalancerAddressesLocked(result);
  return std::move(result);
}

}  // namespace

bool EventEngineClientChannelDNSResolverFactory::IsValidUri(
    const URI& uri) const {
  if (absl::StripPrefix(uri.path(), "/").empty()) {
    gpr_log(GPR_ERROR, "no server name supplied in dns URI");
    return false;
  }
  return true;
}

OrphanablePtr<Resolver>
EventEngineClientChannelDNSResolverFactory::CreateResolver(
    ResolverArgs args) const {
  Duration min_time_between_resolutions = std::max(
      Duration::Zero(), args.args
                            .GetDurationFromIntMillis(
                                GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS)
                            .value_or(Duration::Seconds(30)));
  return MakeOrphanable<EventEngineClientChannelDNSResolver>(
      std::move(args), min_time_between_resolutions);
}

}  // namespace grpc_core
