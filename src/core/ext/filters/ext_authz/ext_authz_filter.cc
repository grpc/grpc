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
#include <vector>

#include "src/core/ext/filters/ext_authz/ext_authz_client.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

typedef HeaderValueOption::AppendAction AppendAction;
//
// ExtAuthz
//

bool ExtAuthz::isHeaderAllowed(std::string key) const {
  for (auto& disallow : disallowed_headers) {
    if (disallow.Match(key)) {
      return false;
    }
  }
  if (allowed_headers.size() == 0) {
    return true;
  }
  for (auto& allow : allowed_headers) {
    if (allow.Match(key)) {
      return true;
    }
  }
  return false;
}

ExtAuthz::CheckResult ExtAuthz::CheckRequestAllowed() const {
  if (!filter_enabled.has_value()) {
    return CheckResult::kSendRequestToExtAuthzService;
  }
  const auto& enabled = *filter_enabled;
  // Logic: if filter_enabled < 100% (numerator < denominator)
  if (enabled.numerator < enabled.denominator) {
    // random_number = generate_random_number(0, denominator);
    // We use [0, denominator) range for simple < numerator check.
    // If user wanted 1-based [1, denominator], logic would be different.
    // But standard fractional percent implies P = numerator/denominator.
    // Uniform<uint32_t> produces [min, max).
    grpc_core::SharedBitGen g;
    uint32_t random_number =
        absl::Uniform<uint32_t>(absl::BitGenRef(g), 0, enabled.denominator);
    if (random_number >= enabled.numerator) {
      if (deny_at_disable.has_value() && deny_at_disable.value()) {
        return CheckResult::kDeny;
      } else {
        return CheckResult::kPassThrough;
      }
    }
  }
  return CheckResult::kSendRequestToExtAuthzService;
}

//
// ExtAuthzFilter::Config
//

bool ExtAuthzFilter::Config::Equals(const FilterConfig& other) const {
  //   const auto& o = DownCast<const Config&>(other);
  // TODO(rishesh): fix this
  return true;
}

std::string ExtAuthzFilter::Config::ToString() const {
  // TODO(rishesh): fix this
  return absl::StrCat("{instance_name=\"", instance_name, "\"}");
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

std::string GetHeaderValue(const std::string& header, grpc_metadata_batch& md) {
  std::string buffer;
  return md.GetStringValue(header, &buffer).has_value() ? buffer : "";
};

bool isHeaderMutationPossibleForHeaderValueOptions(
    const HeaderValueOption& header, grpc_metadata_batch& md, bool allowed,
    bool disallow_is_error) {
  auto header_value = GetHeaderValue(header.header.key, md);
  switch (header.append_action) {
    case AppendAction::kAppendIfExistsOrAdd: {
      if (!allowed && disallow_is_error) {
        return false;
      } else if (allowed) {
        md.Remove(absl::string_view(header.header.key));
        md.Append(
            header.header.key,
            Slice::FromCopiedString(header_value.append(header.header.value)),
            [](absl::string_view, const Slice&) {});
      }
    } break;
    case AppendAction::kAddIfAbsent: {
      if (header_value.empty() && !allowed && disallow_is_error) {
        return false;
      } else if (header_value.empty() && allowed) {
        md.Append(header.header.key,
                  Slice::FromCopiedString(header.header.value),
                  [](absl::string_view, const Slice&) {});
      }
    } break;
    case AppendAction::kOverwriteIfExists: {
      if (!header_value.empty() && !allowed && disallow_is_error) {
        return false;
      } else if (!header_value.empty() && allowed) {
        md.Remove(absl::string_view(header.header.key));
        md.Append(header.header.key,
                  Slice::FromCopiedString(header.header.value),
                  [](absl::string_view, const Slice&) {});
      }
    } break;
    case AppendAction::kOverwriteIfExistsOrAdd: {
      if (!allowed && disallow_is_error) {
        return false;
      } else if (allowed) {
        md.Remove(absl::string_view(header.header.key));
        md.Append(header.header.key,
                  Slice::FromCopiedString(header.header.value),
                  [](absl::string_view, const Slice&) {});
      }
    } break;
  }
  return true;
}

}  // namespace

ServerMetadataHandle ExtAuthzFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, ExtAuthzFilter* filter) {
  // check if the rpc is allowed based on whether ext_authz_filter is enabled or
  // not
  switch (filter->filter_config_->ext_authz.CheckRequestAllowed()) {
    case ExtAuthz::CheckResult::kSendRequestToExtAuthzService: {
      // continue with ext authz filter
    } break;
    case ExtAuthz::CheckResult::kDeny: {
      return MalformedRequest("ExtAuthz filter is not enabled",
                              filter->filter_config_->ext_authz.status_on_error);
    } break;
    case ExtAuthz::CheckResult::kPassThrough: {
      return nullptr;
    } break;
  }
  std::vector<std::pair<std::string, std::string>> metadata_list;
  md.Log([&](absl::string_view key, absl::string_view value) {
    //  if the header is matched by the disallowed_headers config field, it will
    //  not be added to this map
    if (filter->filter_config_->ext_authz.isHeaderAllowed(std::string(key))) {
      metadata_list.emplace_back(std::string(key), std::string(value));
    }
  });
  std::string path_str;
  if (auto* path = md.get_pointer(HttpPathMetadata())) {
    path_str = std::string(path->as_string_view());
  }
  ExtAuthzClient::ExtAuthzRequestParams params;
  params.headers = std::move(metadata_list);
  // params.headers is vector of pairs of strings.
  params.path = std::move(path_str);
  params.is_client_call = true;
  auto key = filter->filter_config_->ext_authz.server_uri;
  auto channel = filter->channel_cache_->Get(key);
  if (channel == nullptr) {
    // If we can't get a channel, we probably can't auth.
    return MalformedRequest("ExtAuthz channel not found");
  }
  auto result = channel->Check(params);
  if (!result.ok()) {
    // Check failure_mode_allow
    if (!filter->filter_config_->ext_authz.failure_mode_allow) {
      return MalformedRequest(
          result.status().message(),
          filter->filter_config_->ext_authz.status_on_error);
    } else if (filter->filter_config_->ext_authz
                   .failure_mode_allow_header_add) {
      md.Set(XEnvoyAuthFailureModeAllowedMetadata(),
             Slice::FromStaticString("true"));
    }
    return nullptr;
  }
  const auto& response = *result;
  if (response.status_code != GRPC_STATUS_OK) {
    filter->response_trailer_to_add = response.denied_response.headers;
    // Check with Mark whether this is correct or not
    return MalformedRequest("ExtAuthz request is denied",
                            response.denied_response.status);
  }
  auto& decoder_header_mutation_rules =
      filter->filter_config_->ext_authz.decoder_header_mutation_rules.value();
  // header_to_remove
  for (auto& header : response.ok_response.headers_to_remove) {
    auto allowed =
        decoder_header_mutation_rules.IsHeaderMutationAllowed(header);
    if (GetHeaderValue(header, md).empty() && !allowed &&
        decoder_header_mutation_rules.disallow_is_error) {
      return MalformedRequest(
          "ExtAuthz header mutation is not allowed",
          filter->filter_config_->ext_authz.status_on_error);
    } else if (allowed) {
      md.Remove(absl::string_view(header));
    }
  }
  // response_headers_to_add
  filter->response_headers_to_add =
      response.ok_response.response_headers_to_add;
  // adding or modification of headers
  for (auto& header : response.ok_response.headers) {
    auto allowed = decoder_header_mutation_rules.IsHeaderMutationAllowed(
        header.header.key);
    if (!isHeaderMutationPossibleForHeaderValueOptions(
            header, md, allowed,
            decoder_header_mutation_rules.disallow_is_error)) {
      return MalformedRequest(
          "ExtAuthz header mutation is not allowed",
          filter->filter_config_->ext_authz.status_on_error);
    }
  }
  return nullptr;
}

absl::Status ExtAuthzFilter::Call::OnServerInitialMetadata(
    ServerMetadata& md, ExtAuthzFilter* filter) {
  if (!filter->response_headers_to_add.has_value()) {
    return absl::OkStatus();
  }
  auto& decoder_header_mutation_rules =
      filter->filter_config_->ext_authz.decoder_header_mutation_rules.value();
  for (auto& header : filter->response_headers_to_add.value()) {
    auto allowed = decoder_header_mutation_rules.IsHeaderMutationAllowed(
        header.header.key);
    if (!isHeaderMutationPossibleForHeaderValueOptions(
            header, md, allowed,
            decoder_header_mutation_rules.disallow_is_error)) {
      filter->response_headers_to_add.reset();
      return absl::Status(static_cast<absl::StatusCode>(
                              filter->filter_config_->ext_authz.status_on_error),
                          "ExtAuthz header mutation is not allowed");
    }
  }
  filter->response_headers_to_add.reset();
  return absl::OkStatus();
}

absl::Status ExtAuthzFilter::Call::OnServerTrailingMetadata(
    ServerMetadata& md, ExtAuthzFilter* filter) {
  if (!filter->response_trailer_to_add.has_value()) {
    return absl::OkStatus();
  }
  auto& decoder_header_mutation_rules =
      filter->filter_config_->ext_authz.decoder_header_mutation_rules.value();
  for (auto& header : filter->response_trailer_to_add.value()) {
    auto allowed = decoder_header_mutation_rules.IsHeaderMutationAllowed(
        header.header.key);
    if (!isHeaderMutationPossibleForHeaderValueOptions(
            header, md, allowed,
            decoder_header_mutation_rules.disallow_is_error)) {
      filter->response_trailer_to_add.reset();
      return absl::Status(static_cast<absl::StatusCode>(
                              filter->filter_config_->ext_authz.status_on_error),
                          "ExtAuthz header mutation is not allowed");
    }
  }
  filter->response_trailer_to_add.reset();
  return absl::OkStatus();
}

//
// ExtAuthzFilter::ChannelCache
//

UniqueTypeName ExtAuthzFilter::ChannelCache::Type() {
  static UniqueTypeName::Factory factory("ext_authz_channel_cache");
  return factory.Create();
}

RefCountedPtr<ExtAuthzClient> ExtAuthzFilter::ChannelCache::Get(
    const std::string& key) const {
  MutexLock lock(&mu_);
  auto it = cache_.find(key);
  if (it != cache_.end()) {
    return it->second;
  }
  return nullptr;
}

void ExtAuthzFilter::ChannelCache::CreateAndSet(
    std::shared_ptr<const XdsBootstrap::XdsServerTarget> server) {
  std::string key = server->Key();
  MutexLock lock(&mu_);
  auto it = cache_.find(key);
  if (it != cache_.end()) {
    return;
  }
  auto ext_authz_client =
      MakeRefCounted<ExtAuthzClient>(transport_factory_, std::move(server));
  cache_.emplace(std::move(key), std::move(ext_authz_client));
}

void ExtAuthzFilter::ChannelCache::Remove(const std::string& key) {
  MutexLock lock(&mu_);
  cache_.erase(key);
}

//
// ExtAuthzFilter
//

const grpc_channel_filter ExtAuthzFilter::kFilterVtable =
    MakePromiseBasedFilter<ExtAuthzFilter, FilterEndpoint::kClient, 0>();

absl::StatusOr<std::unique_ptr<ExtAuthzFilter>> ExtAuthzFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args filter_args) {
  if (!IsXdsChannelFilterChainPerRouteEnabled()) {
    return absl::InvalidArgumentError(
        "ext_authz: xds channel filter chain per route is not enabled");
  }
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
  // Get cache from blackboard.  This must have been populated
  // previously by the XdsConfigSelector.
  auto cache = filter_args.GetState<ChannelCache>(config->instance_name);
  // Instantiate filter.
  return std::unique_ptr<ExtAuthzFilter>(
      new ExtAuthzFilter(std::move(config), std::move(cache)));
}

ExtAuthzFilter::ExtAuthzFilter(RefCountedPtr<const Config> filter_config,
                               RefCountedPtr<const ChannelCache> channel_cache)
    : filter_config_(std::move(filter_config)),
      channel_cache_(std::move(channel_cache)) {}

// TODO(rishesh): add the filter in the chain

}  // namespace grpc_core