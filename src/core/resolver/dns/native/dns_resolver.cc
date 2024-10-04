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

#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/port_platform.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/polling_resolver.h"
#include "src/core/resolver/resolver.h"
#include "src/core/resolver/resolver_factory.h"
#include "src/core/util/backoff.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/uri.h"

#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2

namespace grpc_core {

namespace {

class NativeClientChannelDNSResolver final : public PollingResolver {
 public:
  NativeClientChannelDNSResolver(ResolverArgs args,
                                 Duration min_time_between_resolutions);
  ~NativeClientChannelDNSResolver() override;

  OrphanablePtr<Orphanable> StartRequest() override;

 private:
  // No-op request class, used so that the PollingResolver code knows
  // when there is a request in flight, even if the request is not
  // actually cancellable.
  class Request final : public Orphanable {
   public:
    Request() = default;

    void Orphan() override { delete this; }
  };

  void OnResolved(
      absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or);
};

NativeClientChannelDNSResolver::NativeClientChannelDNSResolver(
    ResolverArgs args, Duration min_time_between_resolutions)
    : PollingResolver(std::move(args), min_time_between_resolutions,
                      BackOff::Options()
                          .set_initial_backoff(Duration::Milliseconds(
                              GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS * 1000))
                          .set_multiplier(GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER)
                          .set_jitter(GRPC_DNS_RECONNECT_JITTER)
                          .set_max_backoff(Duration::Milliseconds(
                              GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)),
                      &dns_resolver_trace) {
  GRPC_TRACE_VLOG(dns_resolver, 2) << "[dns_resolver=" << this << "] created";
}

NativeClientChannelDNSResolver::~NativeClientChannelDNSResolver() {
  GRPC_TRACE_VLOG(dns_resolver, 2) << "[dns_resolver=" << this << "] destroyed";
}

OrphanablePtr<Orphanable> NativeClientChannelDNSResolver::StartRequest() {
  Ref(DEBUG_LOCATION, "dns_request").release();
  auto dns_request_handle = GetDNSResolver()->LookupHostname(
      absl::bind_front(&NativeClientChannelDNSResolver::OnResolved, this),
      name_to_resolve(), kDefaultSecurePort, kDefaultDNSRequestTimeout,
      interested_parties(), /*name_server=*/"");
  GRPC_TRACE_VLOG(dns_resolver, 2)
      << "[dns_resolver=" << this << "] starting request="
      << DNSResolver::HandleToString(dns_request_handle);
  return MakeOrphanable<Request>();
}

void NativeClientChannelDNSResolver::OnResolved(
    absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or) {
  GRPC_TRACE_VLOG(dns_resolver, 2)
      << "[dns_resolver=" << this
      << "] request complete, status=" << addresses_or.status();
  // Convert result from iomgr DNS API into Resolver::Result.
  Result result;
  if (addresses_or.ok()) {
    EndpointAddressesList addresses;
    for (auto& addr : *addresses_or) {
      addresses.emplace_back(addr, ChannelArgs());
    }
    result.addresses = std::move(addresses);
  } else {
    result.addresses = absl::UnavailableError(
        absl::StrCat("DNS resolution failed for ", name_to_resolve(), ": ",
                     addresses_or.status().ToString()));
  }
  result.args = channel_args();
  OnRequestComplete(std::move(result));
  Unref(DEBUG_LOCATION, "dns_request");
}

//
// Factory
//

class NativeClientChannelDNSResolverFactory final : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "dns"; }

  bool IsValidUri(const URI& uri) const override {
    if (GPR_UNLIKELY(!uri.authority().empty())) {
      LOG(ERROR) << "authority based dns uri's not supported";
      return false;
    }
    if (absl::StripPrefix(uri.path(), "/").empty()) {
      LOG(ERROR) << "no server name supplied in dns URI";
      return false;
    }
    return true;
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    if (!IsValidUri(args.uri)) return nullptr;
    Duration min_time_between_resolutions = std::max(
        Duration::Zero(), args.args
                              .GetDurationFromIntMillis(
                                  GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS)
                              .value_or(Duration::Seconds(30)));
    return MakeOrphanable<NativeClientChannelDNSResolver>(
        std::move(args), min_time_between_resolutions);
  }
};

}  // namespace

void RegisterNativeDnsResolver(CoreConfiguration::Builder* builder) {
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<NativeClientChannelDNSResolverFactory>());
}

}  // namespace grpc_core
