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
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/http/client/http_client_filter.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

const NoInterceptor HttpClientFilter::Call::OnServerToClientMessage;
const NoInterceptor HttpClientFilter::Call::OnClientToServerMessage;
const NoInterceptor HttpClientFilter::Call::OnFinalize;

const grpc_channel_filter HttpClientFilter::kFilter =
    MakePromiseBasedFilter<HttpClientFilter, FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata>("http-client");

namespace {
absl::Status CheckServerMetadata(ServerMetadata* b) {
  if (auto* status = b->get_pointer(HttpStatusMetadata())) {
    // If both gRPC status and HTTP status are provided in the response, we
    // should prefer the gRPC status code, as mentioned in
    // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
    //
    const grpc_status_code* grpc_status = b->get_pointer(GrpcStatusMetadata());
    if (grpc_status != nullptr || *status == 200) {
      b->Remove(HttpStatusMetadata());
    } else {
      return absl::Status(
          static_cast<absl::StatusCode>(
              grpc_http2_status_to_grpc_status(*status)),
          absl::StrCat("Received http2 header with status: ", *status));
    }
  }

  if (Slice* grpc_message = b->get_pointer(GrpcMessageMetadata())) {
    *grpc_message = PermissivePercentDecodeSlice(std::move(*grpc_message));
  }

  b->Remove(ContentTypeMetadata());
  return absl::OkStatus();
}

HttpSchemeMetadata::ValueType SchemeFromArgs(const ChannelArgs& args) {
  HttpSchemeMetadata::ValueType scheme = HttpSchemeMetadata::Parse(
      args.GetString(GRPC_ARG_HTTP2_SCHEME).value_or(""),
      [](absl::string_view, const Slice&) {});
  if (scheme == HttpSchemeMetadata::kInvalid) return HttpSchemeMetadata::kHttp;
  return scheme;
}

Slice UserAgentFromArgs(const ChannelArgs& args,
                        absl::string_view transport_name) {
  std::vector<std::string> fields;
  auto add = [&fields](absl::string_view x) {
    if (!x.empty()) fields.push_back(std::string(x));
  };

  add(args.GetString(GRPC_ARG_PRIMARY_USER_AGENT_STRING).value_or(""));
  add(absl::StrFormat("grpc-c/%s (%s; %s)", grpc_version_string(),
                      GPR_PLATFORM_STRING, transport_name));
  add(args.GetString(GRPC_ARG_SECONDARY_USER_AGENT_STRING).value_or(""));

  return Slice::FromCopiedString(absl::StrJoin(fields, " "));
}
}  // namespace

void HttpClientFilter::Call::OnClientInitialMetadata(ClientMetadata& md,
                                                     HttpClientFilter* filter) {
  if (filter->test_only_use_put_requests_) {
    md.Set(HttpMethodMetadata(), HttpMethodMetadata::kPut);
  } else {
    md.Set(HttpMethodMetadata(), HttpMethodMetadata::kPost);
  }
  md.Set(HttpSchemeMetadata(), filter->scheme_);
  md.Set(TeMetadata(), TeMetadata::kTrailers);
  md.Set(ContentTypeMetadata(), ContentTypeMetadata::kApplicationGrpc);
  md.Set(UserAgentMetadata(), filter->user_agent_.Ref());
}

absl::Status HttpClientFilter::Call::OnServerInitialMetadata(
    ServerMetadata& md) {
  return CheckServerMetadata(&md);
}

absl::Status HttpClientFilter::Call::OnServerTrailingMetadata(
    ServerMetadata& md) {
  return CheckServerMetadata(&md);
}

HttpClientFilter::HttpClientFilter(HttpSchemeMetadata::ValueType scheme,
                                   Slice user_agent,
                                   bool test_only_use_put_requests)
    : scheme_(scheme),
      user_agent_(std::move(user_agent)),
      test_only_use_put_requests_(test_only_use_put_requests) {}

absl::StatusOr<HttpClientFilter> HttpClientFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  auto* transport = args.GetObject<Transport>();
  if (transport == nullptr) {
    return absl::InvalidArgumentError("HttpClientFilter needs a transport");
  }
  return HttpClientFilter(
      SchemeFromArgs(args),
      UserAgentFromArgs(args, transport->GetTransportName()),
      args.GetInt(GRPC_ARG_TEST_ONLY_USE_PUT_REQUESTS).value_or(false));
}

}  // namespace grpc_core
