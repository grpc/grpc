//
// Copyright 2026 gRPC authors.
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

#include "src/core/filter/ext_authz/ext_authz_filter.h"

#include <grpc/support/time.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "src/core/call/evaluate_args.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/client_channel/client_channel_args.h"
#include "src/core/filter/ext_authz/ext_authz_messages.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/surface/call.h"
#include "src/core/telemetry/instrument.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/transport/auth_context.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/match.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/time.h"
#include "src/core/util/time_precise.h"
#include "src/core/xds/grpc/streaming_call_promise_wrapper.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/random/distributions.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

//
// ExtAuthzFilter::ClientTelemetryDomain
//

ExtAuthzFilter::ClientTelemetryDomain::CounterHandle
    ExtAuthzFilter::ClientTelemetryDomain::kAllowedRpcs =
        ExtAuthzFilter::ClientTelemetryDomain::RegisterCounter(
            "grpc.client_ext_authz.allowed_rpcs",
            "Number of RPCs that were allowed by the ext_authz server or by "
            "virtue of failure_mode_allow being true.",
            "{RPCs}");

ExtAuthzFilter::ClientTelemetryDomain::CounterHandle
    ExtAuthzFilter::ClientTelemetryDomain::kDeniedRpcs =
        ExtAuthzFilter::ClientTelemetryDomain::RegisterCounter(
            "grpc.client_ext_authz.denied_rpcs",
            "Number of RPCs that were denied by the ext_authz server.",
            "{RPCs}");

ExtAuthzFilter::ClientTelemetryDomain::CounterHandle
    ExtAuthzFilter::ClientTelemetryDomain::kFilterDisabledRpcs =
        ExtAuthzFilter::ClientTelemetryDomain::RegisterCounter(
            "grpc.client_ext_authz.filter_disabled_rpcs",
            "Number of RPCs for which the filter was disabled.", "{RPCs}");

ExtAuthzFilter::ClientTelemetryDomain::CounterHandle
    ExtAuthzFilter::ClientTelemetryDomain::kFailedRpcs =
        ExtAuthzFilter::ClientTelemetryDomain::RegisterCounter(
            "grpc.client_ext_authz.failed_rpcs",
            "Number of RPCs for which the ext_authz call-out failed or "
            "contained invalid data (e.g., disallowed header mutations).",
            "{RPCs}");

//
// ExtAuthzFilter::ServerTelemetryDomain
//

ExtAuthzFilter::ServerTelemetryDomain::CounterHandle
    ExtAuthzFilter::ServerTelemetryDomain::kAllowedRpcs =
        ExtAuthzFilter::ServerTelemetryDomain::RegisterCounter(
            "grpc.server_ext_authz.allowed_rpcs",
            "Number of RPCs that were allowed by the ext_authz server or by "
            "virtue of failure_mode_allow being true.",
            "{RPCs}");

ExtAuthzFilter::ServerTelemetryDomain::CounterHandle
    ExtAuthzFilter::ServerTelemetryDomain::kDeniedRpcs =
        ExtAuthzFilter::ServerTelemetryDomain::RegisterCounter(
            "grpc.server_ext_authz.denied_rpcs",
            "Number of RPCs that were denied by the ext_authz server.",
            "{RPCs}");

ExtAuthzFilter::ServerTelemetryDomain::CounterHandle
    ExtAuthzFilter::ServerTelemetryDomain::kFilterDisabledRpcs =
        ExtAuthzFilter::ServerTelemetryDomain::RegisterCounter(
            "grpc.server_ext_authz.filter_disabled_rpcs",
            "Number of RPCs for which the filter was disabled.", "{RPCs}");

ExtAuthzFilter::ServerTelemetryDomain::CounterHandle
    ExtAuthzFilter::ServerTelemetryDomain::kFailedRpcs =
        ExtAuthzFilter::ServerTelemetryDomain::RegisterCounter(
            "grpc.server_ext_authz.failed_rpcs",
            "Number of RPCs for which the ext_authz call-out failed or "
            "contained invalid data (e.g., disallowed header mutations).",
            "{RPCs}");

//
// ExtAuthzFilter::Config
//

bool ExtAuthzFilter::Config::Equals(const FilterConfig& other) const {
  const auto& o = DownCast<const Config&>(other);
  return channel_info == o.channel_info && filter_enabled == o.filter_enabled &&
         deny_at_disable == o.deny_at_disable &&
         failure_mode_allow == o.failure_mode_allow &&
         failure_mode_allow_header_add == o.failure_mode_allow_header_add &&
         status_on_error == o.status_on_error &&
         allowed_headers == o.allowed_headers &&
         disallowed_headers == o.disallowed_headers &&
         decoder_header_mutation_rules == o.decoder_header_mutation_rules &&
         include_peer_certificate == o.include_peer_certificate;
}

std::string ExtAuthzFilter::Config::ToString() const {
  std::string result = "{";
  bool is_first = true;
  Match(
      channel_info,
      [&](const GrpcXdsServerTarget& target) {
        StrAppend(result, "grpc_service=");
        StrAppend(result, target.Key());
        is_first = false;
      },
      [&](const RefCountedPtr<ExtAuthzChannel>& channel) {
        if (channel != nullptr) {
          StrAppend(result, "ext_authz_channel=");
          StrAppend(result, channel->server().Key());
          is_first = false;
        }
      });
  if (filter_enabled < kFilterEnabledMax) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "filter_enabled=");
    StrAppend(result, std::to_string(filter_enabled));
    is_first = false;
  }
  if (deny_at_disable) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "deny_at_disable=true");
    is_first = false;
  }
  if (failure_mode_allow) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "failure_mode_allow=true");
    is_first = false;
  }
  if (failure_mode_allow_header_add) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "failure_mode_allow_header_add=true");
    is_first = false;
  }
  if (status_on_error != GRPC_STATUS_PERMISSION_DENIED) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "status_on_error=");
    StrAppend(result, std::to_string(static_cast<int>(status_on_error)));
    is_first = false;
  }
  if (include_peer_certificate) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "include_peer_certificate=true");
    is_first = false;
  }
  if (!(decoder_header_mutation_rules == HeaderMutationRules())) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "decoder_header_mutation_rules=");
    StrAppend(result, decoder_header_mutation_rules.ToString());
    is_first = false;
  }
  if (!allowed_headers.empty()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "allowed_headers=[");
    bool first_matcher = true;
    for (const auto& matcher : allowed_headers) {
      if (!first_matcher) StrAppend(result, ", ");
      StrAppend(result, matcher.ToString());
      first_matcher = false;
    }
    StrAppend(result, "]");
    is_first = false;
  }
  if (!disallowed_headers.empty()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "disallowed_headers=[");
    bool first_matcher = true;
    for (const auto& matcher : disallowed_headers) {
      if (!first_matcher) StrAppend(result, ", ");
      StrAppend(result, matcher.ToString());
      first_matcher = false;
    }
    StrAppend(result, "]");
  }
  StrAppend(result, "}");
  return result;
}

//
// ExtAuthzFilter::Call
//

namespace {

// Handles failure to talk to the authorization service based on the
// failure_mode_allow and failure_mode_allow_header_add configurations.
absl::Status HandleFailure(const ExtAuthzFilter& filter,
                           const ExtAuthzFilter::Config& config,
                           ClientMetadata& md,
                           absl::string_view error_message) {
  filter.RecordFailedRpc();
  if (!config.failure_mode_allow) {
    filter.RecordDeniedRpc();
    return absl::Status(static_cast<absl::StatusCode>(config.status_on_error),
                        error_message);
  }
  filter.RecordAllowedRpc();
  if (config.failure_mode_allow_header_add) {
    md.Set(XEnvoyAuthFailureModeAllowedMetadata(),
           Slice::FromStaticString("true"));
  }
  return absl::OkStatus();
}

// Returns whether the mutation rules allow \a header_name to be mutated.
// Fails if they don't and they say that's an error. Even the default rules
// never allow certain headers to be mutated (see gRFC A102). (Additions of
// these headers are already rejected when parsing the CheckResponse, so in
// practice this affects only removals.)
absl::StatusOr<bool> IsMutationAllowed(const HeaderMutationRules& rules,
                                       absl::string_view header_name,
                                       absl::string_view mutation_type) {
  if (rules.IsMutationAllowed(std::string(header_name))) return true;
  if (rules.disallow_is_error) {
    return absl::InternalError(
        absl::StrCat("Forbidden header ", mutation_type, ": ", header_name));
  }
  return false;
}

// Returns the header additions (XdsHeaderValueOption) or removals (header
// names) that the mutation rules allow, or an error if one isn't allowed and
// the rules say that's an error.
template <typename Mutation>
absl::StatusOr<std::vector<Mutation>> GetAllowedMutations(
    const HeaderMutationRules& rules, const std::vector<Mutation>& mutations,
    absl::string_view mutation_type) {
  std::vector<Mutation> allowed_mutations;
  for (const Mutation& mutation : mutations) {
    absl::string_view header_name;
    if constexpr (std::is_same_v<Mutation, std::string>) {
      header_name = mutation;
    } else {
      header_name = mutation.header.first;
    }
    auto allowed = IsMutationAllowed(rules, header_name, mutation_type);
    if (!allowed.ok()) return allowed.status();
    if (*allowed) allowed_mutations.push_back(mutation);
  }
  return allowed_mutations;
}

// Adds headers that were already checked by GetAllowedMutations().
void AddHeaders(const std::vector<XdsHeaderValueOption>& headers,
                grpc_metadata_batch& md) {
  for (const auto& header : headers) {
    // Without mutation rules, this can't fail.
    ApplyXdsHeaderMutationsAddition(header, /*rules=*/nullptr, md)
        .IgnoreError();
  }
}

}  // namespace

std::string ExtAuthzFilter::Call::DebugTag(const ExtAuthzFilter* filter) const {
  return absl::StrCat(
      "[",
      Activity::current() != nullptr ? Activity::current()->DebugTag()
                                     : "<unknown>",
      " ext_authz_filter=0x", absl::Hex(reinterpret_cast<uintptr_t>(filter)),
      " ext_authz_call=0x", absl::Hex(reinterpret_cast<uintptr_t>(this)), "] ");
}

ArenaPromise<absl::Status> ExtAuthzFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, ExtAuthzFilter* filter) {
  const auto& config = *filter->config_;
  // Check runtime filter enablement sampling.
  if (config.filter_enabled < Config::kFilterEnabledMax) {
    uint32_t random_number =
        absl::Uniform<uint32_t>(SharedBitGen(), 0, Config::kFilterEnabledMax);
    if (random_number >= config.filter_enabled) {
      GRPC_TRACE_LOG(ext_authz_filter, INFO)
          << DebugTag(filter)
          << "filter disabled, deny_at_disable=" << config.deny_at_disable;
      filter->RecordFilterDisabledRpc();
      // If the filter is disabled, deny the request if configured to do so.
      if (config.deny_at_disable) {
        filter->RecordDeniedRpc();
        return Immediate(
            absl::Status(static_cast<absl::StatusCode>(config.status_on_error),
                         "ExtAuthz filter is not enabled"));
      }
      // Otherwise, allow the request to pass through without authorization.
      filter->RecordAllowedRpc();
      return Immediate(absl::OkStatus());
    }
  }
  // Fail if the ext_authz side-channel or transport is unavailable.
  const RefCountedPtr<ExtAuthzChannel> channel = config.channel();
  if (channel == nullptr || channel->transport() == nullptr) {
    return Immediate(HandleFailure(
        *filter, config, md, "ext_authz channel or transport not available"));
  }
  // Construct the CheckRequest parameters from client metadata and
  // configuration.
  ExtAuthzRequest params;
  if (const auto* path = md.get_pointer(HttpPathMetadata())) {
    params.path = path->as_string_view();
  }
  params.metadata = &md;
  params.is_client_call = filter->is_client_;
  // AttributeContext.Request.time is the RPC's start time if it is recorded;
  // otherwise, CreateExtAuthzRequest() uses the current time.
  // Note that we compute the start time from the time elapsed since the call
  // was created rather than via Timestamp::FromCycleCounterRoundDown(), since
  // the latter can be off by up to 2 seconds (the process epoch that it is
  // relative to is truncated to whole seconds).
  // The alias is needed because ExtAuthzFilter::Call shadows the core Call
  // type in this scope.
  using CoreCall = ::grpc_core::Call;
  if (const CoreCall* call = MaybeGetContext<CoreCall>(); call != nullptr) {
    const Duration elapsed = Duration::FromTimespec(
        gpr_cycle_counter_sub(gpr_get_cycle_counter(), call->start_time()));
    params.start_time =
        Timestamp::FromTimespecRoundDown(gpr_now(GPR_CLOCK_MONOTONIC)) -
        elapsed;
  }
  params.allowed_headers = config.allowed_headers;
  params.disallowed_headers = config.disallowed_headers;
  // Populates AttributeContext.source and .destination on the server side.
  // Note that this must outlive the CreateExtAuthzRequest() call below, which
  // reads through params.args.
  std::optional<EvaluateArgs> evaluate_args;
  if (filter->per_channel_args_.has_value()) {
    evaluate_args.emplace(&md, &*filter->per_channel_args_);
    params.args = &*evaluate_args;
    // Left empty unless the config sets include_peer_certificate.
    params.peer_certificate = filter->peer_certificate_;
  }
  // Serialize the CheckRequest proto payload.
  auto payload = CreateExtAuthzRequest(params);
  if (!payload.ok()) {
    return Immediate(
        HandleFailure(*filter, config, md, payload.status().message()));
  }
  // Start the call to the external authorization service.
  GRPC_TRACE_LOG(ext_authz_filter, INFO)
      << DebugTag(filter) << "starting ext_authz call";
  // Pass the data plane RPC's tracing context, so that the ext_authz call is
  // traced as its child.
  auto call = MakeRefCounted<XdsStreamingCallPromiseWrapper>(
      *channel->transport(), "/envoy.service.auth.v3.Authorization/Check",
      XdsTransport::CallOptions()
          .set_start_upon_send_message(true)
          .set_parent_census_context(
              GetContext<Arena>()->GetContext<census_context>()));
  // Start the read before the send, so that a read is already pending when
  // the response arrives.
  auto pull_message = call->PullMessage();
  return Seq(
      call->PushMessage(std::move(*payload), /*send_half_close=*/true),
      // The send result is ignored: if the send failed, the call fails, and
      // its status is handled below.
      [pull_message = std::move(pull_message)](StatusFlag) mutable {
        return std::move(pull_message);
      },
      // Stores the response until the call's status is received.
      [this, call](std::optional<std::string> result) {
        check_response_ = std::move(result);
        return call->PullServerTrailingMetadata();
      },
      // Holds a ref to call until its status is received, since dropping the
      // last ref cancels it.
      [this, &md, filter, call](absl::Status status) -> absl::Status {
        GRPC_TRACE_LOG(ext_authz_filter, INFO)
            << DebugTag(filter)
            << "ext_authz call completed: status=" << status;
        const auto& config = *filter->config_;
        if (!status.ok()) {
          return HandleFailure(*filter, config, md, status.message());
        }
        if (!check_response_.has_value()) {
          return HandleFailure(*filter, config, md,
                               "No response message received");
        }
        // Parse the received CheckResponse.
        auto response = ExtAuthzResponse::Parse(*check_response_);
        if (!response.ok()) {
          return HandleFailure(*filter, config, md,
                               response.status().message());
        }
        const HeaderMutationRules& rules = config.decoder_header_mutation_rules;
        return Match(
            response->response,
            [&](const ExtAuthzResponse::DeniedResponse& denied)
                -> absl::Status {
              GRPC_TRACE_LOG(ext_authz_filter, INFO)
                  << DebugTag(filter)
                  << "ext_authz denied request: status=" << denied.status
                  << " message=" << response->status_message;
              auto trailers =
                  GetAllowedMutations(rules, denied.headers, "mutation");
              if (!trailers.ok()) {
                return HandleFailure(*filter, config, md,
                                     trailers.status().message());
              }
              // The denied response headers are applied to the trailing
              // metadata in OnServerTrailingMetadata(), which sees the
              // metadata synthesized from the status returned below.
              response_trailers_to_add_ = std::move(*trailers);
              filter->RecordDeniedRpc();
              std::string status_message = "ExtAuthz request is denied";
              if (!response->status_message.empty()) {
                absl::StrAppend(&status_message,
                                ", error message: ", response->status_message);
              }
              return absl::Status(static_cast<absl::StatusCode>(denied.status),
                                  status_message);
            },
            [&](const ExtAuthzResponse::OkResponse& ok_resp) -> absl::Status {
              GRPC_TRACE_LOG(ext_authz_filter, INFO)
                  << DebugTag(filter) << "ext_authz allowed request";
              // Check all of the mutations before applying any of them, so
              // that none of them are applied if any of them fails.
              auto additions = GetAllowedMutations(
                  rules, ok_resp.header_mutation.add_headers, "mutation");
              if (!additions.ok()) {
                return HandleFailure(*filter, config, md,
                                     additions.status().message());
              }
              auto removals = GetAllowedMutations(
                  rules, ok_resp.header_mutation.remove_headers, "removal");
              if (!removals.ok()) {
                return HandleFailure(*filter, config, md,
                                     removals.status().message());
              }
              auto response_headers = GetAllowedMutations(
                  rules, ok_resp.response_headers_to_add, "mutation");
              if (!response_headers.ok()) {
                return HandleFailure(*filter, config, md,
                                     response_headers.status().message());
              }
              // Apply the request header mutations. Header additions should
              // be applied before header removals.
              AddHeaders(*additions, md);
              for (const auto& header : *removals) {
                md.Remove(absl::string_view(header));
              }
              // Store any response headers to inject into server initial
              // metadata later.
              response_headers_to_add_ = std::move(*response_headers);
              filter->RecordAllowedRpc();
              return absl::OkStatus();
            });
      });
}

void ExtAuthzFilter::Call::OnServerInitialMetadata(ServerMetadata& md) {
  // If the RPC returned trailers-only, the response headers are applied to
  // the trailing metadata instead.
  if (md.get(GrpcTrailersOnly()).value_or(false)) return;
  // Apply the response headers returned by the authorization service.
  AddHeaders(response_headers_to_add_, md);
  response_headers_to_add_.clear();
}

void ExtAuthzFilter::Call::OnServerTrailingMetadata(ServerMetadata& md) {
  // Response headers that were not applied to the server initial metadata
  // mean that the RPC is Trailers-Only or the server sent no initial
  // metadata, so apply them here.
  AddHeaders(response_headers_to_add_, md);
  // Apply the response trailers returned by the authorization service.
  AddHeaders(response_trailers_to_add_, md);
}

//
// ExtAuthzFilter::ExtAuthzChannel
//

ExtAuthzFilter::ExtAuthzChannel::ExtAuthzChannel(
    GrpcXdsServerTarget server, RefCountedPtr<XdsTransport> transport)
    : server_(std::move(server)), transport_(std::move(transport)) {
  GRPC_TRACE_LOG(ext_authz_filter, INFO)
      << "creating ext_authz channel " << this << " for server "
      << server_.server_uri();
}

ExtAuthzFilter::ExtAuthzChannel::~ExtAuthzChannel() {
  GRPC_TRACE_LOG(ext_authz_filter, INFO)
      << "destroying ext_authz channel " << this << " for server "
      << server_.server_uri();
}

//
// ExtAuthzFilter
//

const grpc_channel_filter ExtAuthzFilter::kFilterVtable =
    MakePromiseBasedFilter<ExtAuthzFilter, FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata>();

absl::StatusOr<std::unique_ptr<ExtAuthzFilter>> ExtAuthzFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args filter_args) {
  // Get filter config.
  if (filter_args.config() == nullptr) {
    return absl::InvalidArgumentError("ext_authz filter config is missing");
  }
  if (filter_args.config()->type() != Config::Type()) {
    return absl::InternalError("ext_authz filter config has wrong type");
  }
  auto config = filter_args.config().TakeAsSubclass<const Config>();
  return std::unique_ptr<ExtAuthzFilter>(
      new ExtAuthzFilter(args, std::move(config)));
}

ExtAuthzFilter::ExtAuthzFilter(const ChannelArgs& args,
                               RefCountedPtr<const Config> filter_config)
    : config_(std::move(filter_config)),
      is_client_(
          !args.GetBool(GRPC_ARG_IS_SERVER_FILTER_STACK).value_or(false)),
      auth_context_(is_client_ ? nullptr
                               : args.GetObjectRef<grpc_auth_context>()),
      telemetry_storage_([&]() -> TelemetryStorage {
        auto stats_plugin_group =
            args.GetObjectRef<GlobalStatsPluginRegistry::StatsPluginGroup>();
        if (stats_plugin_group == nullptr) return std::monostate{};
        auto scope = stats_plugin_group->GetCollectionScope();
        if (scope == nullptr) return std::monostate{};
        if (!is_client_) {
          return ServerTelemetryDomain::GetStorage(std::move(scope));
        }
        return ClientTelemetryDomain::GetStorage(
            std::move(scope), args.GetString(GRPC_ARG_SERVER_URI).value_or(""));
      }()) {
  // AttributeContext.source and .destination are populated only for incoming
  // calls (see gRFC A92), so clients build none of this.
  if (is_client_) return;
  per_channel_args_.emplace(auth_context_.get(), args);
  if (config_->include_peer_certificate) {
    peer_certificate_ = GetUrlEncodedPemPeerCertificate(auth_context_.get());
  }
}

ExtAuthzFilter::~ExtAuthzFilter() = default;

void ExtAuthzFilter::IncrementCounter(
    ClientTelemetryDomain::CounterHandle client_counter,
    ServerTelemetryDomain::CounterHandle server_counter) const {
  Match(
      telemetry_storage_, [](const std::monostate&) {},
      [&](const InstrumentStorageRefPtr<ClientTelemetryDomain>& storage) {
        storage->Increment(client_counter);
      },
      [&](const InstrumentStorageRefPtr<ServerTelemetryDomain>& storage) {
        storage->Increment(server_counter);
      });
}

void ExtAuthzFilter::RecordAllowedRpc() const {
  IncrementCounter(ClientTelemetryDomain::kAllowedRpcs,
                   ServerTelemetryDomain::kAllowedRpcs);
}

void ExtAuthzFilter::RecordDeniedRpc() const {
  IncrementCounter(ClientTelemetryDomain::kDeniedRpcs,
                   ServerTelemetryDomain::kDeniedRpcs);
}

void ExtAuthzFilter::RecordFilterDisabledRpc() const {
  IncrementCounter(ClientTelemetryDomain::kFilterDisabledRpcs,
                   ServerTelemetryDomain::kFilterDisabledRpcs);
}

void ExtAuthzFilter::RecordFailedRpc() const {
  IncrementCounter(ClientTelemetryDomain::kFailedRpcs,
                   ServerTelemetryDomain::kFailedRpcs);
}

}  // namespace grpc_core
