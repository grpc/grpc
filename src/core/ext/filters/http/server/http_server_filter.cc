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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/http/server/http_server_filter.h"

#include <functional>
#include <memory>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

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
#include "src/core/lib/surface/call_trace.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

const grpc_channel_filter HttpServerFilter::kFilter =
    MakePromiseBasedFilter<HttpServerFilter, FilterEndpoint::kServer,
                           kFilterExaminesServerInitialMetadata>("http-server");

namespace {
void FilterOutgoingMetadata(ServerMetadata* md) {
  if (Slice* grpc_message = md->get_pointer(GrpcMessageMetadata())) {
    *grpc_message = PercentEncodeSlice(std::move(*grpc_message),
                                       PercentEncodingType::Compatible);
  }
}

ServerMetadataHandle MalformedRequest(absl::string_view explanation) {
  auto* arena = GetContext<Arena>();
  auto hdl = arena->MakePooled<ServerMetadata>(arena);
  hdl->Set(GrpcStatusMetadata(), GRPC_STATUS_UNKNOWN);
  hdl->Set(GrpcMessageMetadata(), Slice::FromStaticString(explanation));
  hdl->Set(GrpcTarPit(), Empty());
  return hdl;
}
}  // namespace

void HttpServerFilter::InitCall(const CallArgs& call_args) {
  call_args.client_initial_metadata->InterceptAndMap(
      [this, cancel_latch = call_args.cancel_latch](
          ClientMetadataHandle md) -> absl::optional<ClientMetadataHandle> {
        auto method = md->get(HttpMethodMetadata());
        if (method.has_value()) {
          switch (*method) {
            case HttpMethodMetadata::kPost:
              break;
            case HttpMethodMetadata::kPut:
              if (allow_put_requests_) {
                break;
              }
              ABSL_FALLTHROUGH_INTENDED;
            case HttpMethodMetadata::kInvalid:
            case HttpMethodMetadata::kGet:
              cancel_latch->SetIfUnset(MalformedRequest("Bad method header"));
              return absl::nullopt;
          }
        } else {
          cancel_latch->SetIfUnset(MalformedRequest("Missing :method header"));
          return absl::nullopt;
        }

        auto te = md->Take(TeMetadata());
        if (te == TeMetadata::kTrailers) {
          // Do nothing, ok.
        } else if (!te.has_value()) {
          cancel_latch->SetIfUnset(MalformedRequest("Missing :te header"));
          return absl::nullopt;
        } else {
          cancel_latch->SetIfUnset(MalformedRequest("Bad :te header"));
          return absl::nullopt;
        }

        auto scheme = md->Take(HttpSchemeMetadata());
        if (scheme.has_value()) {
          if (*scheme == HttpSchemeMetadata::kInvalid) {
            cancel_latch->SetIfUnset(MalformedRequest("Bad :scheme header"));
            return absl::nullopt;
          }
        } else {
          cancel_latch->SetIfUnset(MalformedRequest("Missing :scheme header"));
          return absl::nullopt;
        }

        md->Remove(ContentTypeMetadata());

        Slice* path_slice = md->get_pointer(HttpPathMetadata());
        if (path_slice == nullptr) {
          cancel_latch->SetIfUnset(MalformedRequest("Missing :path header"));
        }

        if (md->get_pointer(HttpAuthorityMetadata()) == nullptr) {
          absl::optional<Slice> host = md->Take(HostMetadata());
          if (host.has_value()) {
            md->Set(HttpAuthorityMetadata(), std::move(*host));
          }
        }

        if (md->get_pointer(HttpAuthorityMetadata()) == nullptr) {
          cancel_latch->SetIfUnset(
              MalformedRequest("Missing :authority header"));
          return absl::nullopt;
        }

        if (!surface_user_agent_) {
          md->Remove(UserAgentMetadata());
        }

        return md;
      });

  call_args.server_initial_metadata->InterceptAndMap(
      [](ServerMetadataHandle md) {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO, "%s[http-server] Write metadata",
                  Activity::current()->DebugTag().c_str());
        }
        FilterOutgoingMetadata(md.get());
        md->Set(HttpStatusMetadata(), 200);
        md->Set(ContentTypeMetadata(), ContentTypeMetadata::kApplicationGrpc);
        return md;
      });

  call_args.server_trailing_metadata->InterceptAndMap(
      [](ServerMetadataHandle md) {
        FilterOutgoingMetadata(md.get());
        return md;
      });
}

absl::StatusOr<HttpServerFilter> HttpServerFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  return HttpServerFilter(
      args.GetBool(GRPC_ARG_SURFACE_USER_AGENT).value_or(true),
      args.GetBool(
              GRPC_ARG_DO_NOT_USE_UNLESS_YOU_HAVE_PERMISSION_FROM_GRPC_TEAM_ALLOW_BROKEN_PUT_REQUESTS)
          .value_or(false));
}

}  // namespace grpc_core
