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

#include "src/core/ext/filters/ext_authz/ext_authz_filter.h"

#include <grpc/support/time.h>

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "src/core/call/evaluate_args.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/client_channel/client_channel_args.h"
#include "src/core/ext/filters/ext_authz/ext_authz_messages.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/status_flag.h"
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
#include "absl/strings/match.h"
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
  bool has_server = false;
  Match(
      channel_info,
      [&](const RefCountedPtr<ExtAuthzChannel>& channel) {
        if (channel != nullptr) {
          StrAppend(result, "server_uri=");
          StrAppend(result, channel->server().server_uri());
          has_server = true;
        }
      },
      [&](const GrpcXdsServerTarget& target) {
        StrAppend(result, "server_uri=");
        StrAppend(result, target.server_uri());
        has_server = true;
      });
  if (has_server) {
    if (filter_enabled.has_value()) {
      StrAppend(result, ", filter_enabled=");
      StrAppend(result, std::to_string(*filter_enabled));
    }
    StrAppend(result, ", deny_at_disable=");
    StrAppend(result, deny_at_disable ? "true" : "false");
    StrAppend(result, ", failure_mode_allow=");
    StrAppend(result, failure_mode_allow ? "true" : "false");
    StrAppend(result, ", failure_mode_allow_header_add=");
    StrAppend(result, failure_mode_allow_header_add ? "true" : "false");
    StrAppend(result, ", status_on_error=");
    StrAppend(result, std::to_string(static_cast<int>(status_on_error)));
    StrAppend(result, ", include_peer_certificate=");
    StrAppend(result, include_peer_certificate ? "true" : "false");
    if (decoder_header_mutation_rules.has_value()) {
      StrAppend(result, ", decoder_header_mutation_rules=");
      StrAppend(result, decoder_header_mutation_rules->ToString());
    }
    if (!allowed_headers.empty()) {
      StrAppend(result, ", allowed_headers=[");
      bool is_first = true;
      for (const auto& matcher : allowed_headers) {
        if (!is_first) StrAppend(result, ", ");
        is_first = false;
        StrAppend(result, matcher.ToString());
      }
      StrAppend(result, "]");
    }
    if (!disallowed_headers.empty()) {
      StrAppend(result, ", disallowed_headers=[");
      bool is_first = true;
      for (const auto& matcher : disallowed_headers) {
        if (!is_first) StrAppend(result, ", ");
        is_first = false;
        StrAppend(result, matcher.ToString());
      }
      StrAppend(result, "]");
    }
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
// Fails if they don't and they say that's an error.
absl::StatusOr<bool> IsMutationAllowed(const HeaderMutationRules* rules,
                                       absl::string_view header_name,
                                       absl::string_view mutation_type) {
  // Even without mutation rules, gRPC never allows certain headers to be
  // mutated (see gRFC A102), so such mutations are ignored. (Additions of
  // these headers are already rejected when parsing the CheckResponse, so in
  // practice this affects only removals.)
  if (rules == nullptr) {
    return !absl::StartsWith(header_name, ":") &&
           !absl::StartsWith(header_name, "grpc-") && header_name != "host";
  }
  if (rules->IsMutationAllowed(std::string(header_name))) return true;
  if (rules->disallow_is_error) {
    return absl::InternalError(
        absl::StrCat("Forbidden header ", mutation_type, ": ", header_name));
  }
  return false;
}

// Returns the headers to add that the mutation rules allow, or an error if
// one isn't allowed and the rules say that's an error.
absl::StatusOr<std::vector<XdsHeaderValueOption>> GetAllowedAdditions(
    const HeaderMutationRules* rules,
    const std::vector<XdsHeaderValueOption>& headers) {
  std::vector<XdsHeaderValueOption> allowed_headers;
  for (const auto& header : headers) {
    auto allowed = IsMutationAllowed(rules, header.header.first, "mutation");
    if (!allowed.ok()) return allowed.status();
    if (*allowed) allowed_headers.push_back(header);
  }
  return allowed_headers;
}

// Returns the headers to remove that the mutation rules allow, or an error if
// one isn't allowed and the rules say that's an error.
absl::StatusOr<std::vector<std::string>> GetAllowedRemovals(
    const HeaderMutationRules* rules, const std::vector<std::string>& headers) {
  std::vector<std::string> allowed_headers;
  for (const auto& header : headers) {
    auto allowed = IsMutationAllowed(rules, header, "removal");
    if (!allowed.ok()) return allowed.status();
    if (*allowed) allowed_headers.push_back(header);
  }
  return allowed_headers;
}

// Adds headers that were already checked by GetAllowedAdditions().
void AddHeaders(const std::vector<XdsHeaderValueOption>& headers,
                grpc_metadata_batch& md) {
  for (const auto& header : headers) {
    // Without mutation rules, this can't fail.
    ApplyXdsHeaderMutationsAddition(header, /*rules=*/nullptr, md)
        .IgnoreError();
  }
}

}  // namespace

ArenaPromise<absl::Status> ExtAuthzFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, ExtAuthzFilter* filter) {
  const auto& config = *filter->config_;
  // Check runtime filter enablement sampling.
  if (config.filter_enabled.has_value() && *config.filter_enabled < 1000000) {
    uint32_t random_number =
        absl::Uniform<uint32_t>(SharedBitGen(), 0, 1000000);
    if (random_number >= *config.filter_enabled) {
      filter->RecordFilterDisabledRpc();
      // If the filter is disabled, deny the request if configured to do so.
      if (config.deny_at_disable) {
        return Immediate(
            absl::Status(static_cast<absl::StatusCode>(config.status_on_error),
                         "ExtAuthz filter is not enabled"));
      }
      // Otherwise, allow the request to pass through without authorization.
      return Immediate(absl::OkStatus());
    }
  }
  // Fail if the ext_authz side-channel or transport is unavailable.
  if (filter->channel() == nullptr ||
      filter->channel()->transport() == nullptr) {
    return Immediate(HandleFailure(
        *filter, config, md, "ext_authz channel or transport not available"));
  }
  // Construct the CheckRequest parameters from client metadata and
  // configuration.
  ExtAuthzRequest params;
  if (const auto* path = md.get_pointer(HttpPathMetadata())) {
    params.path = std::string(path->as_string_view());
  }
  params.metadata = &md;
  params.is_client_call = filter->is_client_;
  // AttributeContext.Request.time is the RPC's start time if it is recorded;
  // otherwise, CreateExtAuthzRequest() uses the current time.
  // Note that we compute the start time from the time elapsed since the call
  // was created rather than via Timestamp::FromCycleCounterRoundDown(), since
  // the latter can be off by up to 2 seconds (the process epoch that it is
  // relative to is truncated to whole seconds).
  if (const grpc_core::Call* call = MaybeGetContext<grpc_core::Call>();
      call != nullptr) {
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
  GRPC_TRACE_LOG(ext_authz_filter, INFO) << "starting ext_authz call";
  auto call = MakeRefCounted<XdsStreamingCallPromiseWrapper>(
      *filter->channel()->transport(),
      "/envoy.service.auth.v3.Authorization/Check",
      XdsTransport::CallOptions().set_start_upon_send_message(true));
  return Seq(
      Join(call->PushMessage(std::move(*payload), /*send_half_close=*/true),
           call->PullMessage()),
      [this, md = &md, filter,
       call](std::tuple<StatusFlag, std::optional<std::string>> results) {
        return Map(
            call->PullServerTrailingMetadata(),
            // Holds a ref to call until its status is received, since
            // dropping the last ref cancels it.
            [this, md, filter, call, result = std::move(std::get<1>(results))](
                absl::Status status) -> absl::Status {
              const auto& config = *filter->config_;
              if (!status.ok()) {
                return HandleFailure(*filter, config, *md, status.message());
              }
              if (!result.has_value()) {
                return HandleFailure(*filter, config, *md,
                                     "No response message received");
              }
              // Parse the received CheckResponse.
              auto response = ExtAuthzResponse::Parse(*result);
              if (!response.ok()) {
                return HandleFailure(*filter, config, *md,
                                     response.status().message());
              }
              const HeaderMutationRules* rules =
                  config.decoder_header_mutation_rules.has_value()
                      ? &*config.decoder_header_mutation_rules
                      : nullptr;
              // Handle non-OK status (denied response).
              if (response->status_code != GRPC_STATUS_OK) {
                std::string status_message = "ExtAuthz request is denied";
                if (!response->status_message.empty()) {
                  absl::StrAppend(&status_message, ", error message: ",
                                  response->status_message);
                }
                const auto& denied =
                    std::get<ExtAuthzResponse::DeniedResponse>(
                        response->response);
                auto trailers = GetAllowedAdditions(rules, denied.headers);
                if (!trailers.ok()) {
                  return HandleFailure(*filter, config, *md,
                                       trailers.status().message());
                }
                // The denied response headers are applied to the trailing
                // metadata in OnServerTrailingMetadata(), which sees the
                // metadata synthesized from the status returned below.
                response_trailer_to_add = std::move(*trailers);
                filter->RecordDeniedRpc();
                return absl::Status(
                    static_cast<absl::StatusCode>(denied.status),
                    status_message);
              }
              // Handle OK response.
              const auto& ok_resp =
                  std::get<ExtAuthzResponse::OkResponse>(response->response);
              // Check all of the mutations before applying any of them, so
              // that none of them are applied if any of them fails.
              auto additions = GetAllowedAdditions(
                  rules, ok_resp.header_mutation.set_headers);
              if (!additions.ok()) {
                return HandleFailure(*filter, config, *md,
                                     additions.status().message());
              }
              auto removals = GetAllowedRemovals(
                  rules, ok_resp.header_mutation.remove_headers);
              if (!removals.ok()) {
                return HandleFailure(*filter, config, *md,
                                     removals.status().message());
              }
              auto response_headers =
                  GetAllowedAdditions(rules, ok_resp.response_headers_to_add);
              if (!response_headers.ok()) {
                return HandleFailure(*filter, config, *md,
                                     response_headers.status().message());
              }
              // Apply the request header mutations. Header additions should be
              // applied before header removals.
              AddHeaders(*additions, *md);
              for (const auto& header : *removals) {
                md->Remove(absl::string_view(header));
              }
              // Store any response headers to inject into server initial
              // metadata later.
              response_headers_to_add = std::move(*response_headers);
              filter->RecordAllowedRpc();
              return absl::OkStatus();
            });
      });
}

void ExtAuthzFilter::Call::OnServerInitialMetadata(ServerMetadata& md) {
  // If the RPC returned trailers-only, response headers are skipped.
  if (md.get(GrpcTrailersOnly()).value_or(false)) return;
  // Apply the response headers returned by the authorization service.
  if (response_headers_to_add.has_value()) {
    AddHeaders(*response_headers_to_add, md);
  }
}

void ExtAuthzFilter::Call::OnServerTrailingMetadata(ServerMetadata& md) {
  // Apply the response trailers returned by the authorization service.
  if (response_trailer_to_add.has_value()) {
    AddHeaders(*response_trailer_to_add, md);
  }
}

//
// ExtAuthzFilter::ExtAuthzChannel
//

ExtAuthzFilter::ExtAuthzChannel::ExtAuthzChannel(
    GrpcXdsServerTarget server, RefCountedPtr<XdsTransport> transport)
    : server_(std::move(server)), transport_(std::move(transport)) {}

ExtAuthzFilter::ExtAuthzChannel::~ExtAuthzChannel() = default;

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
    return absl::InternalError("ext_authz: filter config not set");
  }
  if (filter_args.config()->type() != Config::Type()) {
    return absl::InternalError(
        absl::StrCat("wrong config type passed to ext_authz filter: ",
                     filter_args.config()->type().name()));
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
