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
#include "src/core/resolver/dns/event_engine/event_engine_client_channel_resolver.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>
#include <stddef.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/resolved_address_internal.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/load_balancing/grpclb/grpclb_balancer_addresses.h"
#include "src/core/resolver/dns/event_engine/service_config_helper.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/polling_resolver.h"
#include "src/core/resolver/resolver.h"
#include "src/core/resolver/resolver_factory.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/backoff.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"

// IWYU pragma: no_include <ratio>

namespace grpc_core {
namespace {

#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2
#define GRPC_DNS_DEFAULT_QUERY_TIMEOUT_MS 120000

using grpc_event_engine::experimental::EventEngine;

// TODO(hork): Investigate adding a resolver test scenario where the first
// balancer hostname lookup result is an error, and the second contains valid
// addresses.
// TODO(hork): Add a test that checks for proper authority from balancer
// addresses.

// ----------------------------------------------------------------------------
// EventEngineClientChannelDNSResolver
// ----------------------------------------------------------------------------
class EventEngineClientChannelDNSResolver final : public PollingResolver {
 public:
  EventEngineClientChannelDNSResolver(ResolverArgs args,
                                      Duration min_time_between_resolutions);
  OrphanablePtr<Orphanable> StartRequest() override;

 private:
  // ----------------------------------------------------------------------------
  // EventEngineDNSRequestWrapper declaration
  // ----------------------------------------------------------------------------
  class EventEngineDNSRequestWrapper final
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

   private:
    void OnTimeout() ABSL_LOCKS_EXCLUDED(on_resolved_mu_);
    void OnHostnameResolved(
        absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses);
    void OnSRVResolved(
        absl::StatusOr<std::vector<EventEngine::DNSResolver::SRVRecord>>
            srv_records);
    void OnBalancerHostnamesResolved(
        std::string authority,
        absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses);
    void OnTXTResolved(absl::StatusOr<std::vector<std::string>> service_config);
    // Returns a Result if resolution is complete.
    // callers must release the lock and call OnRequestComplete if a Result is
    // returned. This is because OnRequestComplete may Orphan the resolver,
    // which requires taking the lock.
    absl::optional<Resolver::Result> OnResolvedLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(on_resolved_mu_);
    // Helper method to populate server addresses on resolver result.
    void MaybePopulateAddressesLocked(Resolver::Result* result)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(on_resolved_mu_);
    // Helper method to populate balancer addresses on resolver result.
    void MaybePopulateBalancerAddressesLocked(Resolver::Result* result)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(on_resolved_mu_);
    // Helper method to populate service config on resolver result.
    void MaybePopulateServiceConfigLocked(Resolver::Result* result)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(on_resolved_mu_);

    RefCountedPtr<EventEngineClientChannelDNSResolver> resolver_;
    Mutex on_resolved_mu_;
    // Lookup callbacks
    bool is_hostname_inflight_ ABSL_GUARDED_BY(on_resolved_mu_) = false;
    bool is_srv_inflight_ ABSL_GUARDED_BY(on_resolved_mu_) = false;
    bool is_txt_inflight_ ABSL_GUARDED_BY(on_resolved_mu_) = false;
    // Output fields from requests.
    EndpointAddressesList addresses_ ABSL_GUARDED_BY(on_resolved_mu_);
    EndpointAddressesList balancer_addresses_ ABSL_GUARDED_BY(on_resolved_mu_);
    ValidationErrors errors_ ABSL_GUARDED_BY(on_resolved_mu_);
    absl::StatusOr<std::string> service_config_json_
        ABSL_GUARDED_BY(on_resolved_mu_);
    // Other internal state
    size_t number_of_balancer_hostnames_initiated_
        ABSL_GUARDED_BY(on_resolved_mu_) = 0;
    size_t number_of_balancer_hostnames_resolved_
        ABSL_GUARDED_BY(on_resolved_mu_) = 0;
    bool orphaned_ ABSL_GUARDED_BY(on_resolved_mu_) = false;
    absl::optional<EventEngine::TaskHandle> timeout_handle_
        ABSL_GUARDED_BY(on_resolved_mu_);
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
                      &event_engine_client_channel_resolver_trace),
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
  auto dns_resolver =
      event_engine_->GetDNSResolver({/*dns_server=*/authority()});
  if (!dns_resolver.ok()) {
    Result result;
    result.addresses = dns_resolver.status();
    result.service_config = dns_resolver.status();
    OnRequestComplete(std::move(result));
    return nullptr;
  }
  return MakeOrphanable<EventEngineDNSRequestWrapper>(
      RefAsSubclass<EventEngineClientChannelDNSResolver>(DEBUG_LOCATION,
                                                         "dns-resolving"),
      std::move(*dns_resolver));
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
  GRPC_TRACE_VLOG(event_engine_client_channel_resolver, 2)
      << "(event_engine client channel resolver) DNSResolver::"
      << resolver_.get() << " Starting hostname resolution for "
      << resolver_->name_to_resolve();
  is_hostname_inflight_ = true;
  event_engine_resolver_->LookupHostname(
      [self = Ref(DEBUG_LOCATION, "OnHostnameResolved")](
          absl::StatusOr<std::vector<EventEngine::ResolvedAddress>>
              addresses) mutable {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        self->OnHostnameResolved(std::move(addresses));
        self.reset();
      },
      resolver_->name_to_resolve(), kDefaultSecurePort);
  if (resolver_->enable_srv_queries_) {
    GRPC_TRACE_VLOG(event_engine_client_channel_resolver, 2)
        << "(event_engine client channel resolver) DNSResolver::"
        << resolver_.get() << " Starting SRV record resolution for "
        << resolver_->name_to_resolve();
    is_srv_inflight_ = true;
    event_engine_resolver_->LookupSRV(
        [self = Ref(DEBUG_LOCATION, "OnSRVResolved")](
            absl::StatusOr<std::vector<EventEngine::DNSResolver::SRVRecord>>
                srv_records) mutable {
          ApplicationCallbackExecCtx callback_exec_ctx;
          ExecCtx exec_ctx;
          self->OnSRVResolved(std::move(srv_records));
          self.reset();
        },
        absl::StrCat("_grpclb._tcp.", resolver_->name_to_resolve()));
  }
  if (resolver_->request_service_config_) {
    GRPC_TRACE_VLOG(event_engine_client_channel_resolver, 2)
        << "(event_engine client channel resolver) DNSResolver::"
        << resolver_.get() << " Starting TXT record resolution for "
        << resolver_->name_to_resolve();
    is_txt_inflight_ = true;
    event_engine_resolver_->LookupTXT(
        [self = Ref(DEBUG_LOCATION, "OnTXTResolved")](
            absl::StatusOr<std::vector<std::string>> service_config) mutable {
          ApplicationCallbackExecCtx callback_exec_ctx;
          ExecCtx exec_ctx;
          self->OnTXTResolved(std::move(service_config));
          self.reset();
        },
        absl::StrCat("_grpc_config.", resolver_->name_to_resolve()));
  }
  // Initialize overall DNS resolution timeout alarm.
  auto timeout = resolver_->query_timeout_ms_.count() == 0
                     ? EventEngine::Duration::max()
                     : resolver_->query_timeout_ms_;
  timeout_handle_ = resolver_->event_engine_->RunAfter(
      timeout, [self = Ref(DEBUG_LOCATION, "OnTimeout")]() mutable {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        self->OnTimeout();
        self.reset();
      });
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
    if (timeout_handle_.has_value()) {
      resolver_->event_engine_->Cancel(*timeout_handle_);
      timeout_handle_.reset();
    }
    // Even if cancellation fails here, OnResolvedLocked will return early, and
    // the resolver will never see a completed request.
    event_engine_resolver_.reset();
  }
  Unref(DEBUG_LOCATION, "Orphan");
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    OnTimeout() {
  MutexLock lock(&on_resolved_mu_);
  GRPC_TRACE_VLOG(event_engine_client_channel_resolver, 2)
      << "(event_engine client channel resolver) DNSResolver::"
      << resolver_.get() << " OnTimeout";
  timeout_handle_.reset();
  event_engine_resolver_.reset();
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    OnHostnameResolved(absl::StatusOr<std::vector<EventEngine::ResolvedAddress>>
                           new_addresses) {
  absl::optional<Resolver::Result> result;
  {
    MutexLock lock(&on_resolved_mu_);
    // Make sure field destroys before cleanup.
    ValidationErrors::ScopedField field(&errors_, "hostname lookup");
    if (orphaned_) return;
    is_hostname_inflight_ = false;
    if (!new_addresses.ok()) {
      errors_.AddError(new_addresses.status().message());
    } else {
      addresses_.reserve(addresses_.size() + new_addresses->size());
      for (const auto& addr : *new_addresses) {
        addresses_.emplace_back(CreateGRPCResolvedAddress(addr), ChannelArgs());
      }
    }
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
  MutexLock lock(&on_resolved_mu_);
  // Make sure field destroys before cleanup.
  ValidationErrors::ScopedField field(&errors_, "srv lookup");
  if (orphaned_) return;
  is_srv_inflight_ = false;
  if (!srv_records.ok()) {
    // An error has occurred, finish resolving.
    errors_.AddError(srv_records.status().message());
    result = OnResolvedLocked();
    return;
  }
  if (srv_records->empty()) {
    result = OnResolvedLocked();
    return;
  }
  if (!timeout_handle_.has_value()) {
    // We could reach here if timeout happened while an SRV query was finishing.
    errors_.AddError(
        "timed out - not initiating subsequent balancer hostname requests");
    result = OnResolvedLocked();
    return;
  }
  // Do a subsequent hostname query since SRV records were returned
  for (auto& srv_record : *srv_records) {
    GRPC_TRACE_VLOG(event_engine_client_channel_resolver, 2)
        << "(event_engine client channel resolver) DNSResolver::"
        << resolver_.get() << " Starting balancer hostname resolution for "
        << srv_record.host << ":" << srv_record.port;
    ++number_of_balancer_hostnames_initiated_;
    event_engine_resolver_->LookupHostname(
        [host = srv_record.host,
         self = Ref(DEBUG_LOCATION, "OnBalancerHostnamesResolved")](
            absl::StatusOr<std::vector<EventEngine::ResolvedAddress>>
                new_balancer_addresses) mutable {
          ApplicationCallbackExecCtx callback_exec_ctx;
          ExecCtx exec_ctx;
          self->OnBalancerHostnamesResolved(std::move(host),
                                            std::move(new_balancer_addresses));
          self.reset();
        },
        srv_record.host, std::to_string(srv_record.port));
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
  MutexLock lock(&on_resolved_mu_);
  // Make sure field destroys before cleanup.
  ValidationErrors::ScopedField field(
      &errors_, absl::StrCat("balancer lookup for ", authority));
  if (orphaned_) return;
  ++number_of_balancer_hostnames_resolved_;
  if (!new_balancer_addresses.ok()) {
    // An error has occurred, finish resolving.
    errors_.AddError(new_balancer_addresses.status().message());
  } else {
    // Capture the addresses and finish resolving.
    balancer_addresses_.reserve(balancer_addresses_.size() +
                                new_balancer_addresses->size());
    auto srv_channel_args =
        ChannelArgs().Set(GRPC_ARG_DEFAULT_AUTHORITY, authority);
    for (const auto& addr : *new_balancer_addresses) {
      balancer_addresses_.emplace_back(CreateGRPCResolvedAddress(addr),
                                       srv_channel_args);
    }
  }
  result = OnResolvedLocked();
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    OnTXTResolved(absl::StatusOr<std::vector<std::string>> service_config) {
  absl::optional<Resolver::Result> result;
  {
    MutexLock lock(&on_resolved_mu_);
    // Make sure field destroys before cleanup.
    ValidationErrors::ScopedField field(&errors_, "txt lookup");
    if (orphaned_) return;
    CHECK(is_txt_inflight_);
    is_txt_inflight_ = false;
    if (!service_config.ok()) {
      errors_.AddError(service_config.status().message());
      service_config_json_ = service_config.status();
    } else {
      static constexpr absl::string_view kServiceConfigAttributePrefix =
          "grpc_config=";
      auto result = std::find_if(service_config->begin(), service_config->end(),
                                 [&](absl::string_view s) {
                                   return absl::StartsWith(
                                       s, kServiceConfigAttributePrefix);
                                 });
      if (result != service_config->end()) {
        // Found a service config record.
        service_config_json_ =
            result->substr(kServiceConfigAttributePrefix.size());
        GRPC_TRACE_VLOG(event_engine_client_channel_resolver, 2)
            << "(event_engine client channel resolver) DNSResolver::"
            << event_engine_resolver_.get()
            << " found service config: " << service_config_json_->c_str();
      } else {
        service_config_json_ = absl::UnavailableError(absl::StrCat(
            "failed to find attribute prefix: ", kServiceConfigAttributePrefix,
            " in TXT records"));
        errors_.AddError(service_config_json_.status().message());
      }
    }
    result = OnResolvedLocked();
  }
  if (result.has_value()) {
    resolver_->OnRequestComplete(std::move(*result));
  }
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    MaybePopulateAddressesLocked(Resolver::Result* result) {
  if (addresses_.empty()) return;
  result->addresses = std::move(addresses_);
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    MaybePopulateBalancerAddressesLocked(Resolver::Result* result) {
  if (!balancer_addresses_.empty()) {
    result->args =
        SetGrpcLbBalancerAddresses(result->args, balancer_addresses_);
  }
}

void EventEngineClientChannelDNSResolver::EventEngineDNSRequestWrapper::
    MaybePopulateServiceConfigLocked(Resolver::Result* result) {
  // This function is called only if we are returning addresses.  In that case,
  // we currently ignore TXT lookup failures.
  // TODO(roth): Consider differentiating between NXDOMAIN and other failures,
  // so that we can return an error in the non-NXDOMAIN case.
  if (!service_config_json_.ok()) return;
  // TXT lookup succeeded, so parse the config.
  auto service_config = ChooseServiceConfig(*service_config_json_);
  if (!service_config.ok()) {
    result->service_config = absl::UnavailableError(absl::StrCat(
        "failed to parse service config: ", service_config.status().message()));
    return;
  }
  if (service_config->empty()) return;
  GRPC_TRACE_VLOG(event_engine_client_channel_resolver, 2)
      << "(event_engine client channel resolver) DNSResolver::"
      << event_engine_resolver_.get()
      << " selected service config choice: " << service_config->c_str();
  result->service_config =
      ServiceConfigImpl::Create(resolver_->channel_args(), *service_config);
  if (!result->service_config.ok()) {
    result->service_config = absl::UnavailableError(
        absl::StrCat("failed to parse service config: ",
                     result->service_config.status().message()));
  }
}

absl::optional<Resolver::Result> EventEngineClientChannelDNSResolver::
    EventEngineDNSRequestWrapper::OnResolvedLocked() {
  if (orphaned_) return absl::nullopt;
  // Wait for all requested queries to return.
  if (is_hostname_inflight_ || is_srv_inflight_ || is_txt_inflight_ ||
      number_of_balancer_hostnames_resolved_ !=
          number_of_balancer_hostnames_initiated_) {
    GRPC_TRACE_VLOG(event_engine_client_channel_resolver, 2)
        << "(event_engine client channel resolver) DNSResolver::" << this
        << " OnResolved() waiting for results (hostname: "
        << (is_hostname_inflight_ ? "waiting" : "done")
        << ", srv: " << (is_srv_inflight_ ? "waiting" : "done")
        << ", txt: " << (is_txt_inflight_ ? "waiting" : "done")
        << ", balancer addresses: " << number_of_balancer_hostnames_resolved_
        << "/" << number_of_balancer_hostnames_initiated_ << " complete";
    return absl::nullopt;
  }
  GRPC_TRACE_VLOG(event_engine_client_channel_resolver, 2)
      << "(event_engine client channel resolver) DNSResolver::" << this
      << " OnResolvedLocked() proceeding";
  Resolver::Result result;
  result.args = resolver_->channel_args();
  // If both addresses and balancer addresses failed, return an error for both
  // addresses and service config.
  if (addresses_.empty() && balancer_addresses_.empty()) {
    absl::Status status = errors_.status(
        absl::StatusCode::kUnavailable,
        absl::StrCat("errors resolving ", resolver_->name_to_resolve()));
    if (status.ok()) {
      // If no errors were returned, but the results are empty, we still need to
      // return an error. Validation errors may be empty.
      status = absl::UnavailableError("No results from DNS queries");
    }
    GRPC_TRACE_VLOG(event_engine_client_channel_resolver, 2)
        << "(event_engine client channel resolver) " << status.message().data();
    result.addresses = status;
    result.service_config = status;
    return std::move(result);
  }
  if (!errors_.ok()) {
    result.resolution_note = errors_.message(
        absl::StrCat("errors resolving ", resolver_->name_to_resolve()));
  }
  // We have at least one of addresses or balancer addresses, so we're going to
  // return a non-error for addresses.
  result.addresses.emplace();
  MaybePopulateAddressesLocked(&result);
  MaybePopulateServiceConfigLocked(&result);
  MaybePopulateBalancerAddressesLocked(&result);
  return std::move(result);
}

}  // namespace

bool EventEngineClientChannelDNSResolverFactory::IsValidUri(
    const URI& uri) const {
  if (absl::StripPrefix(uri.path(), "/").empty()) {
    LOG(ERROR) << "no server name supplied in dns URI";
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
