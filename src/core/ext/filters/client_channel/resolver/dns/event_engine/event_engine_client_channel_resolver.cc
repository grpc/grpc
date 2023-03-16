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

#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <ratio>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/resolver/dns/event_engine/service_config_helper.h"
#include "src/core/ext/filters/client_channel/resolver/polling_resolver.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/resolved_address_internal.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/service_config/service_config_impl.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2
#define GRPC_DNS_DEFAULT_QUERY_TIMEOUT_MS 120000

using grpc_core::BackOff;
using grpc_core::Duration;
using grpc_core::InternallyRefCounted;
using grpc_core::MakeOrphanable;
using grpc_core::Mutex;
using grpc_core::MutexLock;
using grpc_core::Orphanable;
using grpc_core::OrphanablePtr;
using grpc_core::RefCountedPtr;
using grpc_core::ResolverArgs;

// ----------------------------------------------------------------------------
// EventEngineDNSRequestWrapper declaration
// ----------------------------------------------------------------------------
class EventEngineClientChannelDNSResolver;

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
  void OnTXTResolved(absl::StatusOr<std::string> service_config);

  // Returns a Result if resolution is complete.
  // callers must release the lock and call OnRequestComplete if a Result is
  // returned. This is because OnRequestComplete may Orphan the resolver, which
  // requires taking the lock.
  absl::optional<grpc_core::Resolver::Result> OnResolvedLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(on_resolved_mu_);

 private:
  RefCountedPtr<EventEngineClientChannelDNSResolver> resolver_;
  Mutex on_resolved_mu_;
  absl::optional<EventEngine::DNSResolver::LookupTaskHandle> hostname_handle_;
  absl::optional<EventEngine::DNSResolver::LookupTaskHandle> srv_handle_;
  absl::optional<EventEngine::DNSResolver::LookupTaskHandle> txt_handle_;
  // Output fields from requests.
  std::vector<EventEngine::ResolvedAddress> addresses_
      ABSL_GUARDED_BY(on_resolved_mu_);
  std::vector<EventEngine::DNSResolver::SRVRecord> balancer_addresses_
      ABSL_GUARDED_BY(on_resolved_mu_);
  std::string service_config_json_ ABSL_GUARDED_BY(on_resolved_mu_);
  absl::Status resolution_error_ ABSL_GUARDED_BY(on_resolved_mu_);
  bool orphaned_ ABSL_GUARDED_BY(on_resolved_mu_) = false;
  std::unique_ptr<EventEngine::DNSResolver> event_engine_resolver_;
};

// ----------------------------------------------------------------------------
// EventEngineClientChannelDNSResolver
// ----------------------------------------------------------------------------
class EventEngineClientChannelDNSResolver : public grpc_core::PollingResolver {
 public:
  EventEngineClientChannelDNSResolver(ResolverArgs args,
                                      Duration min_time_between_resolutions);
  OrphanablePtr<Orphanable> StartRequest() override;

 private:
  friend class EventEngineDNSRequestWrapper;

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
                      &grpc_event_engine_dns_trace),
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

EventEngineDNSRequestWrapper::EventEngineDNSRequestWrapper(
    RefCountedPtr<EventEngineClientChannelDNSResolver> resolver,
    std::unique_ptr<EventEngine::DNSResolver> event_engine_resolver)
    : resolver_(std::move(resolver)),
      event_engine_resolver_(std::move(event_engine_resolver)) {
  // Locking to prevent completion before all records are queried
  MutexLock lock(&on_resolved_mu_);
  hostname_handle_ = event_engine_resolver_->LookupHostname(
      absl::bind_front(&EventEngineDNSRequestWrapper::OnHostnameResolved,
                       Ref(DEBUG_LOCATION, "OnHostnameResolved")),
      resolver_->name_to_resolve(), grpc_core::kDefaultSecurePort,
      resolver_->query_timeout_ms_);
  GRPC_EVENT_ENGINE_DNS_TRACE(
      "DNSResolver::%p Started resolving hostname. Handle::%s", resolver_.get(),
      HandleToString(*hostname_handle_).c_str());
  if (resolver_->enable_srv_queries_) {
    srv_handle_ = event_engine_resolver_->LookupSRV(
        absl::bind_front(&EventEngineDNSRequestWrapper::OnSRVResolved,
                         Ref(DEBUG_LOCATION, "OnSRVResolved")),
        resolver_->name_to_resolve(), resolver_->query_timeout_ms_);
    GRPC_EVENT_ENGINE_DNS_TRACE(
        "DNSResolver::%p Started resolving SRV. Handle::%s", resolver_.get(),
        HandleToString(*srv_handle_).c_str());
  }
  if (resolver_->request_service_config_) {
    txt_handle_ = event_engine_resolver_->LookupTXT(
        absl::bind_front(&EventEngineDNSRequestWrapper::OnTXTResolved,
                         Ref(DEBUG_LOCATION, "OnTXTResolved")),
        resolver_->name_to_resolve(), resolver_->query_timeout_ms_);
    GRPC_EVENT_ENGINE_DNS_TRACE(
        "DNSResolver::%p Started resolving TXT. Handle::%s", resolver_.get(),
        HandleToString(*txt_handle_).c_str());
  }
}

EventEngineDNSRequestWrapper::~EventEngineDNSRequestWrapper() {
  resolver_.reset(DEBUG_LOCATION, "dns-resolving");
}

void EventEngineDNSRequestWrapper::Orphan() {
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
    if (txt_handle_.has_value()) {
      event_engine_resolver_->CancelLookup(*txt_handle_);
    }
  }
  Unref(DEBUG_LOCATION, "Orphan");
}

void EventEngineDNSRequestWrapper::OnHostnameResolved(
    absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses) {
  absl::optional<grpc_core::Resolver::Result> result;
  {
    MutexLock lock(&on_resolved_mu_);
    hostname_handle_.reset();
    if (addresses.ok()) {
      addresses_ = std::move(*addresses);
    } else if (resolution_error_.ok()) {
      resolution_error_ = addresses.status();
    } else {
      grpc_core::StatusAddChild(&resolution_error_, addresses.status());
    }
    result = OnResolvedLocked();
  }
  if (result.has_value()) {
    resolver_->OnRequestComplete(std::move(*result));
  }
}

void EventEngineDNSRequestWrapper::OnSRVResolved(
    absl::StatusOr<std::vector<EventEngine::DNSResolver::SRVRecord>>
        srv_records) {
  absl::optional<grpc_core::Resolver::Result> result;
  {
    // DO NOT SUBMIT(hork): handle subsequent requesting hostname records
    MutexLock lock(&on_resolved_mu_);
    hostname_handle_.reset();
    if (srv_records.ok()) {
      balancer_addresses_ = std::move(*srv_records);
    } else if (resolution_error_.ok()) {
      resolution_error_ = srv_records.status();
    } else {
      grpc_core::StatusAddChild(&resolution_error_, srv_records.status());
    }
    result = OnResolvedLocked();
  }
  if (result.has_value()) {
    resolver_->OnRequestComplete(std::move(*result));
  }
}

void EventEngineDNSRequestWrapper::OnTXTResolved(
    absl::StatusOr<std::string> service_config) {
  absl::optional<grpc_core::Resolver::Result> result;
  {
    MutexLock lock(&on_resolved_mu_);
    hostname_handle_.reset();
    if (service_config.ok()) {
      service_config_json_ = std::move(*service_config);
    } else if (resolution_error_.ok()) {
      resolution_error_ = service_config.status();
    } else {
      grpc_core::StatusAddChild(&resolution_error_, service_config.status());
    }
    result = OnResolvedLocked();
  }
  if (result.has_value()) {
    resolver_->OnRequestComplete(std::move(*result));
  }
}

absl::optional<grpc_core::Resolver::Result>
EventEngineDNSRequestWrapper::OnResolvedLocked() {
  if (orphaned_) return absl::nullopt;
  if (hostname_handle_.has_value() || srv_handle_.has_value() ||
      txt_handle_.has_value()) {
    GRPC_EVENT_ENGINE_DNS_TRACE(
        "resolver:%p OnResolved() waiting for results (hostname: %s, srv: %s, "
        "txt: %s)",
        this, hostname_handle_.has_value() ? "waiting" : "done",
        srv_handle_.has_value() ? "waiting" : "done",
        txt_handle_.has_value() ? "waiting" : "done");
    return absl::nullopt;
  }
  GRPC_EVENT_ENGINE_DNS_TRACE("resolver:%p OnResolvedLocked() proceeding",
                              this);
  grpc_core::Resolver::Result result;
  result.args = resolver_->channel_args();
  if (!resolution_error_.ok()) {
    GRPC_EVENT_ENGINE_DNS_TRACE(
        "resolver:%p dns resolution failed: %s", this,
        grpc_core::StatusToString(resolution_error_).c_str());
    auto status = grpc_core::StatusCreate(
        absl::StatusCode::kUnavailable,
        absl::StrCat("DNS resolution failed for ",
                     resolver_->name_to_resolve()),
        DEBUG_LOCATION, /*children=*/{resolution_error_});
    result.addresses = status;
    result.service_config = status;
    return std::move(result);
  }
  // TODO(roth): Change logic to be able to report failures for addresses
  // and service config independently of each other.
  if (!addresses_.empty() || !balancer_addresses_.empty()) {
    if (!addresses_.empty()) {
      result.addresses->reserve(addresses_.size());
      for (const auto& addr : addresses_) {
        result.addresses->emplace_back(CreateGRPCResolvedAddress(addr),
                                       resolver_->channel_args());
      }
    } else {
      result.addresses = grpc_core::ServerAddressList();
    }
    if (!service_config_json_.empty()) {
      auto service_config = ChooseServiceConfig(service_config_json_);
      if (!service_config.ok()) {
        result.service_config = absl::UnavailableError(
            absl::StrCat("failed to parse service config: ",
                         grpc_core::StatusToString(service_config.status())));
      } else if (!service_config->empty()) {
        GRPC_EVENT_ENGINE_DNS_TRACE(
            "resolver:%p selected service config choice: %s", this,
            service_config->c_str());
        result.service_config = grpc_core::ServiceConfigImpl::Create(
            resolver_->channel_args(), *service_config);
        if (!result.service_config.ok()) {
          result.service_config = absl::UnavailableError(
              absl::StrCat("failed to parse service config: ",
                           result.service_config.status().message()));
        }
      }
    }
    if (!balancer_addresses_.empty()) {
      // DO NOT SUBMIT(hork): need to do a subsequent lookup
      grpc_core::Crash("unimplemented");
    }
  }
  return std::move(result);
}

}  // namespace

bool EventEngineClientChannelDNSResolverFactory::IsValidUri(
    const grpc_core::URI& uri) const {
  if (absl::StripPrefix(uri.path(), "/").empty()) {
    gpr_log(GPR_ERROR, "no server name supplied in dns URI");
    return false;
  }
  return true;
}

grpc_core::OrphanablePtr<grpc_core::Resolver>
EventEngineClientChannelDNSResolverFactory::CreateResolver(
    grpc_core::ResolverArgs args) const {
  Duration min_time_between_resolutions = std::max(
      Duration::Zero(), args.args
                            .GetDurationFromIntMillis(
                                GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS)
                            .value_or(Duration::Seconds(30)));
  return MakeOrphanable<EventEngineClientChannelDNSResolver>(
      std::move(args), min_time_between_resolutions);
}

}  // namespace experimental
}  // namespace grpc_event_engine
