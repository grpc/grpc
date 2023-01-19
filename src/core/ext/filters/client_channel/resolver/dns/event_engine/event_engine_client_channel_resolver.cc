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

#include <chrono>

#include "absl/status//statusor.h"

#include "src/core/ext/filters/client_channel/resolver/polling_resolver.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2
#define GRPC_DNS_DEFAULT_QUERY_TIMEOUT_MS 120000

using grpc_core::BackOff;
using grpc_core::ChannelArgs;
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
  std::unique_ptr<EventEngine::DNSResolver> event_engine_resolver_;
};

// ----------------------------------------------------------------------------
// EventEngineClientChannelDNSResolver
// ----------------------------------------------------------------------------
class EventEngineClientChannelDNSResolver : public grpc_core::PollingResolver {
  EventEngineClientChannelDNSResolver(ResolverArgs args,
                                      const ChannelArgs& channel_args);
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
    ResolverArgs args, const ChannelArgs& channel_args)
    : PollingResolver(
          std::move(args), channel_args,
          /*min_time_between_resolutions=*/
          std::max(Duration::Zero(),
                   channel_args
                       .GetDurationFromIntMillis(
                           GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS)
                       .value_or(Duration::Seconds(30))),
          BackOff::Options()
              .set_initial_backoff(Duration::Milliseconds(
                  GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS * 1000))
              .set_multiplier(GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_DNS_RECONNECT_JITTER)
              .set_max_backoff(Duration::Milliseconds(
                  GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)),
          &grpc_event_engine_dns_trace),
      request_service_config_(
          !channel_args.GetBool(GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION)
               .value_or(true)),
      enable_srv_queries_(channel_args.GetBool(GRPC_ARG_DNS_ENABLE_SRV_QUERIES)
                              .value_or(false)),
      // TODO(yijiem): decide if the ares channel arg timeout should be reused.
      query_timeout_ms_(std::chrono::milliseconds(
          std::max(0, channel_args.GetInt(GRPC_ARG_DNS_ARES_QUERY_TIMEOUT_MS)
                          .value_or(GRPC_DNS_DEFAULT_QUERY_TIMEOUT_MS)))),
      event_engine_(channel_args.GetObjectRef<EventEngine>()) {}

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
  Ref(DEBUG_LOCATION, "OnHostnameResolved").release();
  hostname_handle_ = event_engine_resolver_->LookupHostname(
      [](absl::StatusOr<
          std::vector<EventEngine::ResolvedAddress>> /* addresses */) {
        // DO NOT SUBMIT(hork): implement
        grpc_core::Crash("unimplemented");
      },
      resolver_->name_to_resolve(), grpc_core::kDefaultSecurePort,
      resolver_->query_timeout_ms_);
  if (grpc_event_engine_dns_trace.enabled()) {
    gpr_log(
        GPR_DEBUG,
        "DO NOT SUBMIT: resolver::%p Started resolving hostname. Handle::%s",
        resolver_.get(), HandleToString(hostname_handle_).c_str());
  }
  if (resolver_->enable_srv_queries_) {
    Ref(DEBUG_LOCATION, "OnSRVResolved").release();
    srv_handle_ = event_engine_resolver_->LookupSRV(
        [](absl::StatusOr<
            std::vector<EventEngine::DNSResolver::SRVRecord>> /* records */) {
          // DO NOT SUBMIT(hork): implement
          grpc_core::Crash("unimplemented");
        },
        resolver_->name_to_resolve(), resolver_->query_timeout_ms_);
    if (grpc_event_engine_dns_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "DO NOT SUBMIT: resolver::%p Started resolving SRV. Handle::%s",
              resolver_.get(), HandleToString(srv_handle_).c_str());
    }
  }
  if (resolver_->request_service_config_) {
    Ref(DEBUG_LOCATION, "OnTXTResolved").release();
    txt_handle_ = event_engine_resolver_->LookupTXT(
        [](absl::StatusOr<std::string> /* service_config */) {
          // DO NOT SUBMIT(hork): implement
          grpc_core::Crash("unimplemented");
        },
        resolver_->name_to_resolve(), resolver_->query_timeout_ms_);
    if (grpc_event_engine_dns_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "DO NOT SUBMIT: resolver::%p Started resolving TXT. Handle::%s",
              resolver_.get(), HandleToString(txt_handle_).c_str());
    }
  }
}

EventEngineDNSRequestWrapper::~EventEngineDNSRequestWrapper() {
  resolver_.reset(DEBUG_LOCATION, "dns-resolving");
}

void EventEngineDNSRequestWrapper::Orphan() {
  {
    MutexLock lock(&on_resolved_mu_);
    if (hostname_handle_.has_value()) {
      // DO NOT SUBMIT(hork): handle failed cancellation
      event_engine_resolver_->CancelLookup(*hostname_handle_);
    }
    if (srv_handle_.has_value()) {
      // DO NOT SUBMIT(hork): handle failed cancellation
      event_engine_resolver_->CancelLookup(*srv_handle_);
    }
    if (txt_handle_.has_value()) {
      // DO NOT SUBMIT(hork): handle failed cancellation
      event_engine_resolver_->CancelLookup(*txt_handle_);
    }
  }
  Unref(DEBUG_LOCATION, "Orphan");
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
  grpc_core::ChannelArgs channel_args = args.args;
  return MakeOrphanable<EventEngineClientChannelDNSResolver>(std::move(args),
                                                             channel_args);
}

}  // namespace experimental
}  // namespace grpc_event_engine
