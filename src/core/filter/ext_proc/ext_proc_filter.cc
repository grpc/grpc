//
// Copyright 2025 gRPC authors.
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

#include "src/core/filter/ext_proc/ext_proc_filter.h"

#include "src/core/call/interception_chain.h"
#include "src/core/filter/filter_chain.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "src/core/xds/grpc/xds_matcher_context.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

std::string ExtProcFilter::ProcessingMode::ToString() const {
  std::vector<std::string> parts;
  if (send_request_headers.has_value()) {
    parts.push_back(absl::StrCat("send_request_headers=",
                                 *send_request_headers ? "true" : "false"));
  }
  if (send_response_headers.has_value()) {
    parts.push_back(absl::StrCat("send_response_headers=",
                                 *send_response_headers ? "true" : "false"));
  }
  if (send_response_trailers.has_value()) {
    parts.push_back(absl::StrCat("send_response_trailers=",
                                 *send_response_trailers ? "true" : "false"));
  }
  if (send_request_body) parts.push_back("send_request_body=true");
  if (send_response_body) parts.push_back("send_response_body=true");
  return absl::StrCat("{", absl::StrJoin(parts, ", "), "}");
}

std::string ExtProcFilter::Config::ToString() const {
  std::vector<std::string> parts;
  if (grpc_service != nullptr) {
    parts.push_back(absl::StrCat("grpc_service=", grpc_service->ToString()));
  }
  if (failure_mode_allow) parts.push_back("failure_mode_allow=true");
  parts.push_back(absl::StrCat("processing_mode=", processing_mode.ToString()));
  if (allow_mode_override) parts.push_back("allow_mode_override=true");
  if (!allowed_override_modes.empty()) {
    std::vector<std::string> modes;
    for (const auto& mode : allowed_override_modes) {
      modes.push_back(mode.ToString());
    }
    parts.push_back(absl::StrCat("allowed_override_modes=[",
                                 absl::StrJoin(modes, ", "), "]"));
  }
  if (!request_attributes.empty()) {
    parts.push_back(absl::StrCat("request_attributes=[",
                                 absl::StrJoin(request_attributes, ", "), "]"));
  }
  if (!response_attributes.empty()) {
    parts.push_back(absl::StrCat("response_attributes=[",
                                 absl::StrJoin(response_attributes, ", "),
                                 "]"));
  }
  if (mutation_rules.has_value()) {
    parts.push_back(absl::StrCat("mutation_rules=",
                                 mutation_rules->ToString()));
  }
  if (!forwarding_allowed_headers.empty()) {
    std::vector<std::string> matchers;
    for (const auto& matcher : forwarding_allowed_headers) {
      matchers.push_back(matcher.ToString());
    }
    parts.push_back(absl::StrCat("forwarding_allowed_headers=[",
                                 absl::StrJoin(matchers, ", "), "]"));
  }
  if (!forwarding_disallowed_headers.empty()) {
    std::vector<std::string> matchers;
    for (const auto& matcher : forwarding_disallowed_headers) {
      matchers.push_back(matcher.ToString());
    }
    parts.push_back(absl::StrCat("forwarding_disallowed_headers=[",
                                 absl::StrJoin(matchers, ", "), "]"));
  }
  if (disable_immediate_response) {
    parts.push_back("disable_immediate_response=true");
  }
  if (observability_mode) parts.push_back("observability_mode=true");
  if (deferred_close_timeout != Duration::Zero()) {
    parts.push_back(absl::StrCat("deferred_close_timeout=",
                                 deferred_close_timeout.ToString()));
  }
  return absl::StrCat("{", absl::StrJoin(parts, ", "), "}");
}

const grpc_channel_filter ExtProcFilter::kFilterVtable =
    MakePromiseBasedFilter<
        ExtProcFilter, FilterEndpoint::kClient,
        kFilterExaminesServerInitialMetadata | kFilterExaminesOutboundMessages |
            kFilterExaminesInboundMessages | kFilterExaminesCallContext>();

absl::StatusOr<RefCountedPtr<ExtProcFilter>> ExtProcFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args filter_args) {
  if (filter_args.config()->type() != Config::Type()) {
    return absl::InternalError("ext_proc filter config has wrong type");
  }
  auto config = filter_args.config().TakeAsSubclass<const Config>();
  return MakeRefCounted<ExtProcFilter>(args, std::move(config),
                                         std::move(filter_args));
}

ExtProcFilter::ExtProcFilter(const ChannelArgs& args,
                             RefCountedPtr<const Config> config,
                             ChannelFilter::Args filter_args)
    : config_(std::move(config)) {}

void ExtProcFilter::InterceptCall(
    UnstartedCallHandler unstarted_call_handler) {
  // Consume the call coming to us from the client side.
  CallHandler handler = Consume(std::move(unstarted_call_handler));
  // FIXME: implement
}

}  // namespace grpc_core
