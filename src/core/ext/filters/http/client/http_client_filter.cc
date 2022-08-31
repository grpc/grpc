/*
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/http/client/http_client_filter.h"

#include <algorithm>
#include <functional>
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
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/promise/call_push_pull.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/detail/basic_seq.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/lib/transport/transport_fwd.h"
#include "src/core/lib/transport/transport_impl.h"

namespace grpc_core {

const grpc_channel_filter HttpClientFilter::kFilter =
    MakePromiseBasedFilter<HttpClientFilter, FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata>("http-client");

namespace {
absl::Status CheckServerMetadata(ServerMetadata* b) {
  if (auto* status = b->get_pointer(HttpStatusMetadata())) {
    /* If both gRPC status and HTTP status are provided in the response, we
     * should prefer the gRPC status code, as mentioned in
     * https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
     */
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

Slice UserAgentFromArgs(const ChannelArgs& args, const char* transport_name) {
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

ArenaPromise<ServerMetadataHandle> HttpClientFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  auto& md = call_args.client_initial_metadata;
  if (test_only_use_put_requests_) {
    md->Set(HttpMethodMetadata(), HttpMethodMetadata::kPut);
  } else {
    md->Set(HttpMethodMetadata(), HttpMethodMetadata::kPost);
  }
  md->Set(HttpSchemeMetadata(), scheme_);
  md->Set(TeMetadata(), TeMetadata::kTrailers);
  md->Set(ContentTypeMetadata(), ContentTypeMetadata::kApplicationGrpc);
  md->Set(UserAgentMetadata(), user_agent_.Ref());

  auto* read_latch = GetContext<Arena>()->New<Latch<ServerMetadata*>>();
  auto* write_latch =
      std::exchange(call_args.server_initial_metadata, read_latch);

  return CallPushPull(
      Seq(next_promise_factory(std::move(call_args)),
          [](ServerMetadataHandle md) -> ServerMetadataHandle {
            auto r = CheckServerMetadata(md.get());
            if (!r.ok()) return ServerMetadataHandle(r);
            return md;
          }),
      []() { return absl::OkStatus(); },
      Seq(read_latch->Wait(),
          [write_latch](ServerMetadata** md) -> absl::Status {
            auto r =
                *md == nullptr ? absl::OkStatus() : CheckServerMetadata(*md);
            write_latch->Set(*md);
            return r;
          }));
}

HttpClientFilter::HttpClientFilter(HttpSchemeMetadata::ValueType scheme,
                                   Slice user_agent,
                                   bool test_only_use_put_requests)
    : scheme_(scheme),
      user_agent_(std::move(user_agent)),
      test_only_use_put_requests_(test_only_use_put_requests) {}

absl::StatusOr<HttpClientFilter> HttpClientFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  auto* transport = args.GetObject<grpc_transport>();
  if (transport == nullptr) {
    return absl::InvalidArgumentError("HttpClientFilter needs a transport");
  }
  return HttpClientFilter(
      SchemeFromArgs(args), UserAgentFromArgs(args, transport->vtable->name),
      args.GetInt(GRPC_ARG_TEST_ONLY_USE_PUT_REQUESTS).value_or(false));
}

}  // namespace grpc_core
