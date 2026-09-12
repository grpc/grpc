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

#include <string>
#include <utility>
#include <variant>

#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/ext/filters/ext_authz/ext_authz_messages.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/random/distributions.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

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

ServerMetadataHandle ExtAuthzFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, ExtAuthzFilter* filter) {
  const auto& config = *filter->config_;
  // Check runtime filter enablement sampling.
  if (config.filter_enabled.has_value() && *config.filter_enabled < 1000000) {
    uint32_t random_number =
        absl::Uniform<uint32_t>(SharedBitGen(), 0, 1000000);
    if (random_number >= *config.filter_enabled) {
      // If the filter is disabled, deny the request if configured to do so.
      if (config.deny_at_disable) {
        return ServerMetadataFromStatus(config.status_on_error,
                                        "ExtAuthz filter is not enabled");
      }
      // Otherwise, allow the request to pass through without authorization.
      return nullptr;
    }
  }
  // Helper to handle failure based on failure_mode_allow and
  // failure_mode_allow_header_add configurations.
  auto handle_failure =
      [&](absl::string_view error_message) -> ServerMetadataHandle {
    if (!config.failure_mode_allow) {
      return ServerMetadataFromStatus(config.status_on_error, error_message);
    }
    if (config.failure_mode_allow_header_add) {
      md.Set(XEnvoyAuthFailureModeAllowedMetadata(),
             Slice::FromStaticString("true"));
    }
    return nullptr;
  };
  // Fail if the ext_authz side-channel or transport is unavailable.
  if (filter->channel() == nullptr ||
      filter->channel()->transport() == nullptr) {
    return handle_failure("ext_authz channel or transport not available");
  }
  // Construct the CheckRequest parameters from client metadata and
  // configuration.
  ExtAuthzRequest params;
  if (const auto* path = md.get_pointer(HttpPathMetadata())) {
    params.path = std::string(path->as_string_view());
  }
  params.metadata = &md;
  params.is_client_call = filter->is_client_;
  params.allowed_headers = config.allowed_headers;
  params.disallowed_headers = config.disallowed_headers;
  params.include_peer_certificate = config.include_peer_certificate;
  // Serialize the CheckRequest proto payload.
  auto payload = CreateExtAuthzRequest(params);
  if (!payload.ok()) {
    return handle_failure(payload.status().message());
  }
  // Create the unary call to the external authorization service.
  auto unary_call = filter->channel()->transport()->CreateUnaryCall(
      "/envoy.service.auth.v3.Authorization/Check");
  if (unary_call == nullptr) {
    return handle_failure("Failed to create ext_authz unary call");
  }
  GRPC_TRACE_LOG(ext_authz_filter, INFO) << "starting ext_authz call";
  // Dispatch the CheckRequest message to the external authorization service.
  auto result = unary_call->SendMessage(std::move(*payload));
  if (!result.ok()) {
    return handle_failure(result.status().message());
  }
  // Parse the received CheckResponse.
  auto response = ExtAuthzResponse::Parse(*result);
  if (!response.ok()) {
    return handle_failure(response.status().message());
  }
  const HeaderMutationRules* rules =
      config.decoder_header_mutation_rules.has_value()
          ? &*config.decoder_header_mutation_rules
          : nullptr;
  // Handle non-OK status (denied response).
  if (response->status_code != GRPC_STATUS_OK) {
    std::string status_message = "ExtAuthz request is denied";
    if (!response->status_message.empty()) {
      absl::StrAppend(&status_message,
                      ", error message: ", response->status_message);
    }
    const auto* denied =
        std::get_if<ExtAuthzResponse::DeniedResponse>(&response->response);
    grpc_status_code status =
        denied != nullptr ? denied->status : response->status_code;
    auto md_out = ServerMetadataFromStatus(status, status_message);
    if (denied != nullptr) {
      // Append denied response headers to the trailing metadata.
      for (const auto& header : denied->headers) {
        auto mutation_status =
            ApplyXdsHeaderMutationsAddition(header, rules, *md_out);
        if (!mutation_status.ok()) {
          return handle_failure(mutation_status.message());
        }
      }
    }
    return md_out;
  }
  // Handle OK response.
  const auto* ok_resp =
      std::get_if<ExtAuthzResponse::OkResponse>(&response->response);
  if (ok_resp == nullptr) {
    return handle_failure("ExtAuthz OK response missing payload");
  }
  // Apply header additions/mutations to client initial metadata.
  // Header additions should be applied before header removals.
  for (const auto& header : ok_resp->header_mutation.set_headers) {
    auto mutation_status = ApplyXdsHeaderMutationsAddition(header, rules, md);
    if (!mutation_status.ok()) {
      return handle_failure(mutation_status.message());
    }
  }
  // Apply header removals requested by the authorization service.
  for (const auto& header : ok_resp->header_mutation.remove_headers) {
    auto mutation_status = ApplyXdsHeaderMutationsRemoval(header, rules, md);
    if (!mutation_status.ok()) {
      return handle_failure(mutation_status.message());
    }
  }
  // Store any response headers to inject into server initial metadata later.
  if (!ok_resp->response_headers_to_add.empty()) {
    response_headers_to_add = ok_resp->response_headers_to_add;
  }
  return nullptr;
}

absl::Status ExtAuthzFilter::Call::OnServerInitialMetadata(
    ServerMetadata& md, ExtAuthzFilter* filter) {
  // If the RPC returned trailers-only, response headers are skipped.
  if (md.get(GrpcTrailersOnly()).value_or(false)) {
    return absl::OkStatus();
  }
  // Check if there are response headers to inject from ext_authz OK response.
  if (!response_headers_to_add.has_value()) {
    return absl::OkStatus();
  }
  const auto& config = *filter->config_;
  const HeaderMutationRules* rules =
      config.decoder_header_mutation_rules.has_value()
          ? &*config.decoder_header_mutation_rules
          : nullptr;
  // Apply the response headers returned by the authorization service.
  for (const auto& header : *response_headers_to_add) {
    auto status = ApplyXdsHeaderMutationsAddition(header, rules, md);
    if (!status.ok()) {
      response_headers_to_add.reset();
      return absl::Status(static_cast<absl::StatusCode>(config.status_on_error),
                          "ExtAuthz header mutation is not allowed");
    }
  }
  response_headers_to_add.reset();
  return absl::OkStatus();
}

absl::Status ExtAuthzFilter::Call::OnServerTrailingMetadata(
    ServerMetadata& md, ExtAuthzFilter* filter) {
  // Check if there are response trailers to inject from ext_authz.
  if (!response_trailer_to_add.has_value()) {
    return absl::OkStatus();
  }
  const auto& config = *filter->config_;
  const HeaderMutationRules* rules =
      config.decoder_header_mutation_rules.has_value()
          ? &*config.decoder_header_mutation_rules
          : nullptr;
  // Apply the response trailers returned by the authorization service.
  for (const auto& header : *response_trailer_to_add) {
    auto status = ApplyXdsHeaderMutationsAddition(header, rules, md);
    if (!status.ok()) {
      response_trailer_to_add.reset();
      return absl::Status(static_cast<absl::StatusCode>(config.status_on_error),
                          "ExtAuthz header mutation is not allowed");
    }
  }
  response_trailer_to_add.reset();
  return absl::OkStatus();
}

//
// ExtAuthzFilter::ExtAuthzChannel
//

ExtAuthzFilter::ExtAuthzChannel::ExtAuthzChannel(
    GrpcXdsServerTarget server,
    RefCountedPtr<XdsTransportFactory::XdsTransport> transport)
    : server_(std::move(server)), transport_(std::move(transport)) {}

ExtAuthzFilter::ExtAuthzChannel::~ExtAuthzChannel() = default;

//
// ExtAuthzFilter
//

const grpc_channel_filter ExtAuthzFilter::kFilterVtable =
    MakePromiseBasedFilter<ExtAuthzFilter, FilterEndpoint::kClient, 0>();

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
          !args.GetBool(GRPC_ARG_IS_SERVER_FILTER_STACK).value_or(false)) {}

}  // namespace grpc_core
