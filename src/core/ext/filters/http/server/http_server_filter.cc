//
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

#include "src/core/ext/filters/http/server/http_server_filter.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/latent_see.h"

namespace grpc_core {

const grpc_channel_filter HttpServerFilter::kFilter =
    MakePromiseBasedFilter<HttpServerFilter, FilterEndpoint::kServer,
                           kFilterExaminesServerInitialMetadata>();

namespace {
void FilterOutgoingMetadata(ServerMetadata* md) {
  if (Slice* grpc_message = md->get_pointer(GrpcMessageMetadata())) {
    *grpc_message = PercentEncodeSlice(std::move(*grpc_message),
                                       PercentEncodingType::Compatible);
  }
}

ServerMetadataHandle MalformedRequest(absl::string_view explanation) {
  auto* arena = GetContext<Arena>();
  auto hdl = arena->MakePooled<ServerMetadata>();
  hdl->Set(GrpcStatusMetadata(), GRPC_STATUS_UNKNOWN);
  hdl->Set(GrpcMessageMetadata(), Slice::FromStaticString(explanation));
  hdl->Set(GrpcTarPit(), Empty());
  return hdl;
}
}  // namespace

ServerMetadataHandle HttpServerFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, HttpServerFilter* filter) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "HttpServerFilter::Call::OnClientInitialMetadata");
  auto method = md.get(HttpMethodMetadata());
  if (method.has_value()) {
    switch (*method) {
      case HttpMethodMetadata::kPost:
        break;
      case HttpMethodMetadata::kPut:
        if (filter->allow_put_requests_) {
          break;
        }
        [[fallthrough]];
      case HttpMethodMetadata::kInvalid:
      case HttpMethodMetadata::kGet:
        return MalformedRequest("Bad method header");
    }
  } else {
    return MalformedRequest("Missing :method header");
  }

  auto te = md.Take(TeMetadata());
  if (te == TeMetadata::kTrailers) {
    // Do nothing, ok.
  } else if (!te.has_value()) {
    return MalformedRequest("Missing :te header");
  } else {
    return MalformedRequest("Bad :te header");
  }

  auto scheme = md.Take(HttpSchemeMetadata());
  if (scheme.has_value()) {
    if (*scheme == HttpSchemeMetadata::kInvalid) {
      return MalformedRequest("Bad :scheme header");
    }
  } else {
    return MalformedRequest("Missing :scheme header");
  }

  md.Remove(ContentTypeMetadata());

  Slice* path_slice = md.get_pointer(HttpPathMetadata());
  if (path_slice == nullptr) {
    return MalformedRequest("Missing :path header");
  }

  if (md.get_pointer(HttpAuthorityMetadata()) == nullptr) {
    std::optional<Slice> host = md.Take(HostMetadata());
    if (host.has_value()) {
      md.Set(HttpAuthorityMetadata(), std::move(*host));
    }
  }

  if (md.get_pointer(HttpAuthorityMetadata()) == nullptr) {
    return MalformedRequest("Missing :authority header");
  }

  if (!filter->surface_user_agent_) {
    md.Remove(UserAgentMetadata());
  }

  return nullptr;
}

void HttpServerFilter::Call::OnServerInitialMetadata(ServerMetadata& md) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "HttpServerFilter::Call::OnServerInitialMetadata");
  GRPC_TRACE_LOG(call, INFO)
      << GetContext<Activity>()->DebugTag() << "[http-server] Write metadata";
  FilterOutgoingMetadata(&md);
  md.Set(HttpStatusMetadata(), 200);
  md.Set(ContentTypeMetadata(), ContentTypeMetadata::kApplicationGrpc);
}

void HttpServerFilter::Call::OnServerTrailingMetadata(ServerMetadata& md) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "HttpServerFilter::Call::OnServerTrailingMetadata");
  FilterOutgoingMetadata(&md);
}

absl::StatusOr<std::unique_ptr<HttpServerFilter>> HttpServerFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  return std::make_unique<HttpServerFilter>(
      args, args.GetBool(GRPC_ARG_SURFACE_USER_AGENT).value_or(true),
      args.GetBool(
              GRPC_ARG_DO_NOT_USE_UNLESS_YOU_HAVE_PERMISSION_FROM_GRPC_TEAM_ALLOW_BROKEN_PUT_REQUESTS)
          .value_or(false));
}

}  // namespace grpc_core
