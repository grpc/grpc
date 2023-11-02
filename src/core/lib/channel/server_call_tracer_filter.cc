//
// Copyright 2023 gRPC authors.
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

#include <functional>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "src/core/lib/channel/call_finalization.h"
#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

namespace {

// TODO(yashykt): This filter is not really needed. We should be able to move
// this to the connected filter.
class ServerCallTracerFilter : public ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ServerCallTracerFilter> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/);

  void InitCall(const CallArgs& call_args) override;
};

const grpc_channel_filter ServerCallTracerFilter::kFilter =
    MakePromiseBasedFilter<ServerCallTracerFilter, FilterEndpoint::kServer,
                           kFilterExaminesServerInitialMetadata>(
        "server_call_tracer");

absl::StatusOr<ServerCallTracerFilter> ServerCallTracerFilter::Create(
    const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/) {
  return ServerCallTracerFilter();
}

void ServerCallTracerFilter::InitCall(const CallArgs& call_args) {
  auto* call_context = GetContext<grpc_call_context_element>();
  auto* call_tracer = static_cast<ServerCallTracer*>(
      call_context[GRPC_CONTEXT_CALL_TRACER].value);
  if (call_tracer == nullptr) return;
  call_args.client_initial_metadata->InterceptAndMap(
      [call_tracer](ClientMetadataHandle metadata) {
        call_tracer->RecordReceivedInitialMetadata(metadata.get());
        return metadata;
      });
  GetContext<CallFinalization>()->Add(
      [call_tracer](const grpc_call_final_info* final_info) {
        call_tracer->RecordEnd(final_info);
      });
  call_args.server_initial_metadata->InterceptAndMap(
      [call_tracer](ServerMetadataHandle metadata) {
        call_tracer->RecordSendInitialMetadata(metadata.get());
        return metadata;
      });
  call_args.server_trailing_metadata->InterceptAndMap(
      [call_tracer](ServerMetadataHandle md) {
        call_tracer->RecordSendTrailingMetadata(md.get());
        return md;
      });
}

}  // namespace

void RegisterServerCallTracerFilter(CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterFilter(GRPC_SERVER_CHANNEL,
                                          &ServerCallTracerFilter::kFilter);
}

}  // namespace grpc_core
