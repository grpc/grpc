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

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/random/distributions.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include "src/core/call/metadata_batch.h"
#include "src/core/ext/filters/ext_authz/ext_authz_client.h"
#include "src/core/ext/filters/ext_authz/ext_authz_messages.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/match.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/string.h"
#include "src/core/xds/grpc/xds_common_types.h"

namespace grpc_core {

//
// ExtAuthzFilter::Config
//

bool ExtAuthzFilter::Config::isHeaderAllowed(absl::string_view key) const {
  for (const auto& disallow : disallowed_headers) {
    if (disallow.Match(key)) {
      return false;
    }
  }
  if (allowed_headers.empty()) {
    return true;
  }
  for (const auto& allow : allowed_headers) {
    if (allow.Match(key)) {
      return true;
    }
  }
  return false;
}

ExtAuthzFilter::Config::CheckResult
ExtAuthzFilter::Config::CheckRequestAllowed() const {
  if (!filter_enabled.has_value()) {
    return CheckResult::kSendRequestToExtAuthzService;
  }
  if (*filter_enabled < 1000000) {
    uint32_t random_number =
        absl::Uniform<uint32_t>(SharedBitGen(), 0, 1000000);
    if (random_number >= *filter_enabled) {
      if (deny_at_disable) {
        return CheckResult::kDeny;
      } else {
        return CheckResult::kPassThrough;
      }
    }
  }
  return CheckResult::kSendRequestToExtAuthzService;
}

bool ExtAuthzFilter::Config::Equals(const FilterConfig& other) const {
  const auto& o = DownCast<const Config&>(other);
  if (instance_name != o.instance_name) return false;
  return channel_info == o.channel_info &&
         filter_enabled == o.filter_enabled &&
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
  StrAppend(result, "instance_name=");
  StrAppend(result, instance_name);
  bool has_server = false;
  Match(
      channel_info,
      [&](const RefCountedPtr<ExtAuthzChannel>& channel) {
        if (channel != nullptr) {
          StrAppend(result, ", server_uri=");
          StrAppend(result, channel->server().server_uri());
          has_server = true;
        }
      },
      [&](const GrpcXdsServerTarget& target) {
        StrAppend(result, ", server_uri=");
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
    StrAppend(result,
              std::to_string(static_cast<int>(status_on_error)));
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

ServerMetadataHandle MalformedRequest(
    absl::string_view explanation,
    grpc_status_code status_code = GRPC_STATUS_UNKNOWN) {
  auto* arena = GetContext<Arena>();
  auto hdl = arena->MakePooled<ServerMetadata>();
  hdl->Set(GrpcStatusMetadata(), status_code);
  hdl->Set(GrpcMessageMetadata(), Slice::FromStaticString(explanation));
  hdl->Set(GrpcTarPit(), Empty());
  return hdl;
}

}  // namespace

ServerMetadataHandle ExtAuthzFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, ExtAuthzFilter* filter) {
  const auto& config = *filter->filter_config_;
  if (filter->client() == nullptr) {
    return nullptr;
  }
  switch (config.CheckRequestAllowed()) {
    case Config::CheckResult::kSendRequestToExtAuthzService:
      break;
    case Config::CheckResult::kDeny:
      return MalformedRequest("ExtAuthz filter is not enabled",
                              config.status_on_error);
    case Config::CheckResult::kPassThrough:
      return nullptr;
  }
  std::vector<std::pair<std::string, std::string>> metadata_list;
  md.Log([&](absl::string_view key, absl::string_view value) {
    if (config.isHeaderAllowed(key)) {
      metadata_list.emplace_back(std::string(key), std::string(value));
    }
  });
  std::string path_str;
  if (auto* path = md.get_pointer(HttpPathMetadata())) {
    path_str = std::string(path->as_string_view());
  }
  ExtAuthzClient::ExtAuthzRequestParams params;
  params.headers = std::move(metadata_list);
  params.path = std::move(path_str);
  params.is_client_call = true;
  params.include_peer_certificate = config.include_peer_certificate;
  auto channel = filter->client();
  if (channel == nullptr) {
    return MalformedRequest("ExtAuthz channel not found");
  }
  auto result = channel->Check(params);
  if (!result.ok()) {
    if (!config.failure_mode_allow) {
      return MalformedRequest(result.status().message(),
                              config.status_on_error);
    } else if (config.failure_mode_allow_header_add) {
      md.Set(XEnvoyAuthFailureModeAllowedMetadata(),
             Slice::FromStaticString("true"));
    }
    return nullptr;
  }
  const auto& response = *result;
  if (response.status_code != GRPC_STATUS_OK) {
    if (const auto* denied =
            std::get_if<ExtAuthzResponse::DeniedResponse>(&response.response);
        denied != nullptr) {
      response_trailer_to_add = denied->headers;
      return MalformedRequest("ExtAuthz request is denied", denied->status);
    }
    return MalformedRequest(response.status_message.empty()
                                ? "ExtAuthz request is denied"
                                : response.status_message,
                            response.status_code);
  }
  const auto* ok_resp =
      std::get_if<ExtAuthzResponse::OkResponse>(&response.response);
  if (ok_resp == nullptr) {
    return MalformedRequest("ExtAuthz OK response missing payload");
  }
  const HeaderMutationRules* rules =
      config.decoder_header_mutation_rules.has_value()
          ? &*config.decoder_header_mutation_rules
          : nullptr;
  // Apply header removals
  for (const auto& header : ok_resp->header_mutation.remove_headers) {
    auto status = ApplyXdsHeaderMutationsRemoval(header, rules, md);
    if (!status.ok()) {
      return MalformedRequest("ExtAuthz header mutation is not allowed",
                              config.status_on_error);
    }
  }
  // Store response headers to add for server initial metadata
  if (!ok_resp->response_headers_to_add.empty()) {
    response_headers_to_add = ok_resp->response_headers_to_add;
  }
  // Apply header additions / modifications
  for (const auto& header : ok_resp->header_mutation.set_headers) {
    auto status = ApplyXdsHeaderMutationsAddition(header, rules, md);
    if (!status.ok()) {
      return MalformedRequest("ExtAuthz header mutation is not allowed",
                              config.status_on_error);
    }
  }
  return nullptr;
}

absl::Status ExtAuthzFilter::Call::OnServerInitialMetadata(
    ServerMetadata& md, ExtAuthzFilter* filter) {
  if (md.get(GrpcTrailersOnly()).value_or(false)) {
    return absl::OkStatus();
  }
  if (!response_headers_to_add.has_value()) {
    return absl::OkStatus();
  }
  const auto& config = *filter->filter_config_;
  const HeaderMutationRules* rules =
      config.decoder_header_mutation_rules.has_value()
          ? &*config.decoder_header_mutation_rules
          : nullptr;
  for (const auto& header : *response_headers_to_add) {
    auto status = ApplyXdsHeaderMutationsAddition(header, rules, md);
    if (!status.ok()) {
      response_headers_to_add.reset();
      return absl::Status(
          static_cast<absl::StatusCode>(config.status_on_error),
          "ExtAuthz header mutation is not allowed");
    }
  }
  response_headers_to_add.reset();
  return absl::OkStatus();
}

absl::Status ExtAuthzFilter::Call::OnServerTrailingMetadata(
    ServerMetadata& md, ExtAuthzFilter* filter) {
  if (md.get(GrpcTrailersOnly()).value_or(false)) {
    return absl::OkStatus();
  }
  if (!response_trailer_to_add.has_value()) {
    return absl::OkStatus();
  }
  const auto& config = *filter->filter_config_;
  const HeaderMutationRules* rules =
      config.decoder_header_mutation_rules.has_value()
          ? &*config.decoder_header_mutation_rules
          : nullptr;
  for (const auto& header : *response_trailer_to_add) {
    auto status = ApplyXdsHeaderMutationsAddition(header, rules, md);
    if (!status.ok()) {
      response_trailer_to_add.reset();
      return absl::Status(
          static_cast<absl::StatusCode>(config.status_on_error),
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
    const ChannelArgs& /*args*/, ChannelFilter::Args filter_args) {
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
  return std::unique_ptr<ExtAuthzFilter>(new ExtAuthzFilter(std::move(config)));
}

ExtAuthzFilter::ExtAuthzFilter(RefCountedPtr<const Config> filter_config)
    : filter_config_(std::move(filter_config)) {
  if (filter_config_->channel() != nullptr &&
      filter_config_->channel()->transport() != nullptr) {
    client_ = MakeRefCounted<ExtAuthzClient>(
        std::make_unique<GrpcXdsServerTarget>(
            filter_config_->channel()->server()),
        filter_config_->channel()->transport());
  }
}

}  // namespace grpc_core