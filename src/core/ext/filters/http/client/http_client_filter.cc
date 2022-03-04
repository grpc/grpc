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

#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/b64.h"
#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/lib/transport/transport_impl.h"

namespace grpc_core {

namespace {
static absl::Status CheckServerMetadata(const ServerMetadata& b) {
  if (auto* status = b->get_pointer(grpc_core::HttpStatusMetadata())) {
    /* If both gRPC status and HTTP status are provided in the response, we
     * should prefer the gRPC status code, as mentioned in
     * https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
     */
    const grpc_status_code* grpc_status =
        b->get_pointer(grpc_core::GrpcStatusMetadata());
    if (grpc_status != nullptr || *status == 200) {
      b->Remove(grpc_core::HttpStatusMetadata());
    } else {
      return absl::Status(static_cast<absl::StatusCode>(
                              grpc_http2_status_to_grpc_status(*status)),
                          absl::StrCat(absl::StrCat("Received http2 header with status: ", *status));
    }
  }

  if (grpc_core::Slice* grpc_message =
          b->get_pointer(grpc_core::GrpcMessageMetadata())) {
    *grpc_message =
        grpc_core::PermissivePercentDecodeSlice(std::move(*grpc_message));
  }

  b->Remove(grpc_core::ContentTypeMetadata());
  return absl::OkStatus();
}

static grpc_core::HttpSchemeMetadata::ValueType SchemeFromArgs(
    const grpc_channel_args* args) {
  if (args != nullptr) {
    for (size_t i = 0; i < args->num_args; ++i) {
      if (args->args[i].type == GRPC_ARG_STRING &&
          0 == strcmp(args->args[i].key, GRPC_ARG_HTTP2_SCHEME)) {
        grpc_core::HttpSchemeMetadata::ValueType scheme =
            grpc_core::HttpSchemeMetadata::Parse(
                args->args[i].value.string,
                [](absl::string_view, const grpc_core::Slice&) {});
        if (scheme != grpc_core::HttpSchemeMetadata::kInvalid) return scheme;
      }
    }
  }
  return grpc_core::HttpSchemeMetadata::kHttp;
}

static grpc_core::Slice UserAgentFromArgs(const grpc_channel_args* args,
                                          const char* transport_name) {
  std::vector<std::string> user_agent_fields;

  for (size_t i = 0; args && i < args->num_args; i++) {
    if (0 == strcmp(args->args[i].key, GRPC_ARG_PRIMARY_USER_AGENT_STRING)) {
      if (args->args[i].type != GRPC_ARG_STRING) {
        gpr_log(GPR_ERROR, "Channel argument '%s' should be a string",
                GRPC_ARG_PRIMARY_USER_AGENT_STRING);
      } else {
        user_agent_fields.push_back(args->args[i].value.string);
      }
    }
  }

  user_agent_fields.push_back(
      absl::StrFormat("grpc-c/%s (%s; %s)", grpc_version_string(),
                      GPR_PLATFORM_STRING, transport_name));

  for (size_t i = 0; args && i < args->num_args; i++) {
    if (0 == strcmp(args->args[i].key, GRPC_ARG_SECONDARY_USER_AGENT_STRING)) {
      if (args->args[i].type != GRPC_ARG_STRING) {
        gpr_log(GPR_ERROR, "Channel argument '%s' should be a string",
                GRPC_ARG_SECONDARY_USER_AGENT_STRING);
      } else {
        user_agent_fields.push_back(args->args[i].value.string);
      }
    }
  }

  std::string user_agent_string = absl::StrJoin(user_agent_fields, " ");
  return grpc_core::Slice::FromCopiedString(user_agent_string.c_str());
}
}  // namespace

ArenaPromise<ServerMetadata> HttpClientFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  auto& md = call_args.client_initial_metadata;
  md->Set(grpc_core::HttpMethodMetadata(),
          grpc_core::HttpMethodMetadata::kPost);
  md->Set(grpc_core::HttpSchemeMetadata(), scheme_);
  md->Set(grpc_core::TeMetadata(), grpc_core::TeMetadata::kTrailers);
  md->Set(grpc_core::ContentTypeMetadata(),
          grpc_core::ContentTypeMetadata::kApplicationGrpc);
  md->Set(grpc_core::UserAgentMetadata(), user_agent_.Ref());

  auto* read_latch = GetContext<Arena>()->New<Latch<ServerMetadata*>>();
  auto* write_latch =
      absl::exchange(call_args.server_initial_metadata, read_latch);

  return WithIO(Seq(next_promise_factory(std::move(call_args)),
                    [](ServerMetadata md) -> ServerMetadata {
                      auto r = CheckServerMetadata(md);
                      if (!r.ok()) return ServerMetadata(r);
                      return md;
                    }))
      .Read(Seq(read_latch->Wait(), [write_latch](ServerInitialMetadata* md) {
        auto r = CheckServerMetadata(*md);
        write_latch->Set(md);
        return r;
      }));
}

absl::StatusOr<HttpClientFilter> HttpClientFilter::Create(
    const grpc_channel_args* args, ChannelFilter::Args filter_args) {
  auto* transport =
      grpc_channel_args_find_pointer<grpc_transport>(args, GRPC_ARG_TRANSPORT);
  GPR_ASSERT(transport != nullptr);
  return HttpClientFilter(SchemeFromArgs(args),
                          UserAgentFromArgs(args, transport->vtable->name));
}

}  // namespace grpc_core
