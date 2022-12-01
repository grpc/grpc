//
// Copyright 2022 gRPC authors.
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

#include "src/core/ext/filters/stateful_session_affinity/stateful_session_affinity_filter.h"

#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/stateful_session_affinity/stateful_session_affinity_service_config_parser.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_concurrently.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

TraceFlag grpc_stateful_session_affinity_filter_trace(
    false, "stateful_session_affinity_filter");

UniqueTypeName XdsHostOverrideTypeName() {
  static UniqueTypeName::Factory kFactory("xds_host_override");
  return kFactory.Create();
}

const grpc_channel_filter StatefulSessionAffinityFilter::kFilter =
    MakePromiseBasedFilter<StatefulSessionAffinityFilter,
                           FilterEndpoint::kClient>(
        "stateful_session_affinity_filter");

absl::StatusOr<StatefulSessionAffinityFilter>
StatefulSessionAffinityFilter::Create(const ChannelArgs&,
                                      ChannelFilter::Args filter_args) {
  return StatefulSessionAffinityFilter(filter_args);
}

StatefulSessionAffinityFilter::StatefulSessionAffinityFilter(
    ChannelFilter::Args filter_args)
    : index_(grpc_channel_stack_filter_instance_number(
          filter_args.channel_stack(),
          filter_args.uninitialized_channel_element())),
      service_config_parser_index_(
          StatefulSessionAffinityServiceConfigParser::ParserIndex()) {}

namespace {

// Adds the set-cookie header to the server initial metadata if needed.
void MaybeUpdateServerInitialMetadata(
    const StatefulSessionAffinityMethodParsedConfig::CookieConfig*
        cookie_config,
    absl::optional<absl::string_view> cookie_value,
    ServerMetadata* server_initial_metadata) {
  // Get peer string.
  auto peer_string = server_initial_metadata->get(PeerString());
  if (!peer_string.has_value()) return;  // Nothing we can do.
  // If there was no cookie or if the address changed, set the cookie.
  if (!cookie_value.has_value() || *peer_string != *cookie_value) {
    std::vector<std::string> parts = {
        absl::StrCat(*cookie_config->name, "=",
                     absl::Base64Escape(*peer_string), "; HttpOnly")};
    if (cookie_config->path.has_value()) {
      parts.emplace_back(absl::StrCat("Path=", *cookie_config->path));
    }
    if (cookie_config->ttl > Duration::Zero()) {
      parts.emplace_back(
          absl::StrCat("Max-Age=", cookie_config->ttl.as_timespec().tv_sec));
    }
    server_initial_metadata->Append(
        "set-cookie", Slice::FromCopiedString(absl::StrJoin(parts, "; ")),
        [](absl::string_view error, const Slice&) {
          gpr_log(GPR_ERROR, "ERROR ADDING set-cookie METADATA: %s",
                  std::string(error).c_str());
          GPR_ASSERT(false);
        });
  }
}

}  // namespace

// Construct a promise for one call.
ArenaPromise<ServerMetadataHandle>
StatefulSessionAffinityFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  // Get config.
  auto* service_config_call_data = static_cast<ServiceConfigCallData*>(
      GetContext<
          grpc_call_context_element>()[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA]
          .value);
  GPR_ASSERT(service_config_call_data != nullptr);
  auto* method_params = static_cast<StatefulSessionAffinityMethodParsedConfig*>(
      service_config_call_data->GetMethodParsedConfig(
          service_config_parser_index_));
  GPR_ASSERT(method_params != nullptr);
  auto* cookie_config = method_params->GetConfig(index_);
  GPR_ASSERT(cookie_config != nullptr);
  if (!cookie_config->name.has_value()) {
    return next_promise_factory(std::move(call_args));
  }
  // We have a config.
  // If the config has a path, check to see if it matches the request path.
  if (cookie_config->path.has_value()) {
    Slice* path_slice =
        call_args.client_initial_metadata->get_pointer(HttpPathMetadata());
    GPR_ASSERT(path_slice != nullptr);
    absl::string_view path = path_slice->as_string_view();
    // Matching criteria from
    // https://www.rfc-editor.org/rfc/rfc6265#section-5.1.4.
    if (!absl::StartsWith(path, *cookie_config->path) ||
        (path.size() != cookie_config->path->size() &&
         cookie_config->path->back() != '/' &&
         path[cookie_config->path->size() + 1] != '/')) {
      return next_promise_factory(std::move(call_args));
    }
  }
  // Check to see if we have a host override cookie.
  auto cookie_value = GetHostOverrideFromCookie(
      call_args.client_initial_metadata, *cookie_config->name);
  if (cookie_value.has_value()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_stateful_session_affinity_filter_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p: stateful session affinity found cookie %s value %s",
              this, cookie_config->name->c_str(),
              std::string(*cookie_value).c_str());
    }
    // We have a valid cookie, so add the call attribute to be used by the
    // xds_override_host LB policy.
    service_config_call_data->SetCallAttribute(XdsHostOverrideTypeName(),
                                               *cookie_value);
  }
  // Intercept server initial metadata.
  auto* read_latch = GetContext<Arena>()->New<Latch<ServerMetadata*>>();
  auto* write_latch =
      std::exchange(call_args.server_initial_metadata, read_latch);
  return TryConcurrently(
             Seq(next_promise_factory(std::move(call_args)),
                 [cookie_config, cookie_value](ServerMetadataHandle md) {
                   MaybeUpdateServerInitialMetadata(cookie_config, cookie_value,
                                                    md.get());
                   return md;
                 }))
      .NecessaryPull(Seq(read_latch->Wait(),
                         [write_latch, cookie_config,
                          cookie_value](ServerMetadata** md) -> absl::Status {
                           if (*md != nullptr) {
                             MaybeUpdateServerInitialMetadata(
                                 cookie_config, cookie_value, *md);
                           }
                           write_latch->Set(*md);
                           return absl::OkStatus();
                         }));
}

absl::optional<absl::string_view>
StatefulSessionAffinityFilter::GetHostOverrideFromCookie(
    const ClientMetadataHandle& client_initial_metadata,
    absl::string_view cookie_name) {
  // Check to see if the cookie header is present.
  std::string buffer;
  auto header_value =
      client_initial_metadata->GetStringValue("cookie", &buffer);
  if (!header_value.has_value()) return absl::nullopt;
  // Parse cookie header.
  std::vector<absl::string_view> values;
  for (absl::string_view cookie : absl::StrSplit(*header_value, "; ")) {
    std::pair<absl::string_view, absl::string_view> kv =
        absl::StrSplit(cookie, absl::MaxSplits('=', 1));
    if (kv.first == cookie_name) values.push_back(kv.second);
  }
  if (values.empty()) return absl::nullopt;
  // TODO(roth): Figure out the right behavior for multiple cookies.
  // For now, just choose the first value.
  absl::string_view value = values.front();
  // Base64-decode it.
  std::string decoded_value;
  if (!absl::Base64Unescape(value, &decoded_value)) return absl::nullopt;
  // Copy it into the arena, since it will need to persist until the LB pick.
  char* arena_value =
      static_cast<char*>(GetContext<Arena>()->Alloc(decoded_value.size()));
  memcpy(arena_value, decoded_value.c_str(), decoded_value.size());
  return absl::string_view(arena_value, decoded_value.size());
}

void StatefulSessionAffinityFilterRegister(
    CoreConfiguration::Builder* builder) {
  StatefulSessionAffinityServiceConfigParser::Register(builder);
}

}  // namespace grpc_core
