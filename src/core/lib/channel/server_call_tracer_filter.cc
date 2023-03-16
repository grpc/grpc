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
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/surface/channel_init.h"
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

  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;
};

const grpc_channel_filter ServerCallTracerFilter::kFilter =
    MakePromiseBasedFilter<ServerCallTracerFilter, FilterEndpoint::kServer,
                           kFilterExaminesServerInitialMetadata>(
        "server_call_tracer");

absl::StatusOr<ServerCallTracerFilter> ServerCallTracerFilter::Create(
    const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/) {
  return ServerCallTracerFilter();
}

ArenaPromise<ServerMetadataHandle> ServerCallTracerFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  auto* call_context = GetContext<grpc_call_context_element>();
  auto* call_tracer = static_cast<ServerCallTracer*>(
      call_context[GRPC_CONTEXT_CALL_TRACER].value);
  if (call_tracer == nullptr) {
    return next_promise_factory(std::move(call_args));
  }
  call_tracer->RecordReceivedInitialMetadata(
      call_args.client_initial_metadata.get());
  call_args.server_initial_metadata->InterceptAndMap(
      [call_tracer](ServerMetadataHandle metadata) {
        call_tracer->RecordSendInitialMetadata(metadata.get());
        return metadata;
      });
  GetContext<CallFinalization>()->Add(
      [call_tracer](const grpc_call_final_info* final_info) {
        call_tracer->RecordEnd(final_info);
      });
  return OnCancel(
      Map(next_promise_factory(std::move(call_args)),
          [call_tracer](ServerMetadataHandle md) {
            call_tracer->RecordSendTrailingMetadata(md.get());
            return md;
          }),
      [call_tracer]() { call_tracer->RecordCancel(absl::CancelledError()); });
}

}  // namespace

void RegisterServerCallTracerFilter(CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterStage(
      GRPC_SERVER_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      [](ChannelStackBuilder* builder) {
        builder->AppendFilter(&ServerCallTracerFilter::kFilter);
        return true;
      });
}

}  // namespace grpc_core
