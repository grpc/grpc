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

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.h"
#include "src/core/ext/filters/client_channel/resolver/polling_resolver.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/global_config_generic.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/uri/uri_parser.h"

#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2

namespace grpc_core {

namespace {

TraceFlag grpc_trace_dns_resolver(false, "dns_resolver");

class NativeClientChannelDNSResolver : public PollingResolver {
 public:
  NativeClientChannelDNSResolver(ResolverArgs args,
                                 const ChannelArgs& channel_args);
  ~NativeClientChannelDNSResolver() override;

  OrphanablePtr<Orphanable> StartRequest() override;

 private:
  // No-op request class, used so that the PollingResolver code knows
  // when there is a request in flight, even if the request is not
  // actually cancellable.
  class Request : public Orphanable {
   public:
    Request() = default;

    void Orphan() override { delete this; }
  };

  void OnResolved(
      absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or);
};

NativeClientChannelDNSResolver::NativeClientChannelDNSResolver(
    ResolverArgs args, const ChannelArgs& channel_args)
    : PollingResolver(
          std::move(args), channel_args,
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
          &grpc_trace_dns_resolver) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_dns_resolver)) {
    gpr_log(GPR_DEBUG, "[dns_resolver=%p] created", this);
  }
}

NativeClientChannelDNSResolver::~NativeClientChannelDNSResolver() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_dns_resolver)) {
    gpr_log(GPR_DEBUG, "[dns_resolver=%p] destroyed", this);
  }
}

OrphanablePtr<Orphanable> NativeClientChannelDNSResolver::StartRequest() {
  Ref(DEBUG_LOCATION, "dns_request").release();
  auto dns_request_handle = GetDNSResolver()->LookupHostname(
      absl::bind_front(&NativeClientChannelDNSResolver::OnResolved, this),
      name_to_resolve(), kDefaultSecurePort, kDefaultDNSRequestTimeout,
      interested_parties(), /*name_server=*/"");
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_dns_resolver)) {
    gpr_log(GPR_DEBUG, "[dns_resolver=%p] starting request=%p", this,
            DNSResolver::HandleToString(dns_request_handle).c_str());
  }
  return MakeOrphanable<Request>();
}

void NativeClientChannelDNSResolver::OnResolved(
    absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_dns_resolver)) {
    gpr_log(GPR_DEBUG, "[dns_resolver=%p] request complete, status=\"%s\"",
            this, addresses_or.status().ToString().c_str());
  }
  // Convert result from iomgr DNS API into Resolver::Result.
  Result result;
  if (addresses_or.ok()) {
    ServerAddressList addresses;
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

class NativeClientChannelDNSResolverFactory : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "dns"; }

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
    auto channel_args = args.args;
    return MakeOrphanable<NativeClientChannelDNSResolver>(
        std::move(args), std::move(channel_args));
  }
};

}  // namespace

void RegisterNativeDnsResolver(CoreConfiguration::Builder* builder) {
  static const char* const resolver =
      GPR_GLOBAL_CONFIG_GET(grpc_dns_resolver).release();
  if (gpr_stricmp(resolver, "native") == 0) {
    gpr_log(GPR_DEBUG, "Using native dns resolver");
    builder->resolver_registry()->RegisterResolverFactory(
        std::make_unique<NativeClientChannelDNSResolverFactory>());
  } else {
    if (!builder->resolver_registry()->HasResolverFactory("dns")) {
      gpr_log(GPR_DEBUG, "Using native dns resolver");
      builder->resolver_registry()->RegisterResolverFactory(
          std::make_unique<NativeClientChannelDNSResolverFactory>());
    }
  }
}

}  // namespace grpc_core
