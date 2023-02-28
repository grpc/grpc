//
//
// Copyright 2018 gRPC authors.
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

#include "src/cpp/ext/filters/census/server_filter.h"

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/meta/type_traits.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "opencensus/stats/stats.h"
#include "opencensus/tags/tag_key.h"
#include "opencensus/tags/tag_map.h"

#include <grpcpp/opencensus.h>

#include "src/core/lib/channel/call_finalization.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/cpp/ext/filters/census/context.h"
#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "src/cpp/ext/filters/census/measures.h"
#include "src/cpp/ext/filters/census/promise_notification.h"

namespace grpc {
namespace internal {

namespace {

// server metadata elements
struct ServerMetadataElements {
  grpc_core::Slice path;
  grpc_core::Slice tracing_slice;
  grpc_core::Slice census_proto;
};

void FilterInitialMetadata(grpc_metadata_batch* b,
                           ServerMetadataElements* sml) {
  const auto* path = b->get_pointer(grpc_core::HttpPathMetadata());
  if (path != nullptr) {
    sml->path = path->Ref();
  }
  if (OpenCensusTracingEnabled()) {
    auto grpc_trace_bin = b->Take(grpc_core::GrpcTraceBinMetadata());
    if (grpc_trace_bin.has_value()) {
      sml->tracing_slice = std::move(*grpc_trace_bin);
    }
  }
  if (OpenCensusStatsEnabled()) {
    auto grpc_tags_bin = b->Take(grpc_core::GrpcTagsBinMetadata());
    if (grpc_tags_bin.has_value()) {
      sml->census_proto = std::move(*grpc_tags_bin);
    }
  }
}

}  // namespace

// An OpenCensusServerCallData class will be created for every grpc call within
// a channel. It is used to store data and methods specific to that call.
// OpenCensusServerCallData is thread-compatible, however typically only 1
// thread should be interacting with a call at a time.
class OpenCensusServerCallData {
 public:
  // Maximum size of server stats that are sent on the wire.
  static constexpr uint32_t kMaxServerStatsLen = 16;

  explicit OpenCensusServerCallData(
      grpc_metadata_batch* client_initial_metadata);

  void OnSendMessage() { ++sent_message_count_; }

  void OnRecvMessage() { ++recv_message_count_; }

  void OnServerTrailingMetadata(grpc_metadata_batch* server_trailing_metadata);

  void OnCancel() { elapsed_time_ = absl::Now() - start_time_; }

  void Finalize(const grpc_call_final_info* final_info);

 private:
  experimental::CensusContext context_;
  // server method
  grpc_core::Slice path_;
  absl::string_view method_;
  // recv message
  absl::Time start_time_;
  absl::Duration elapsed_time_;
  uint64_t recv_message_count_;
  uint64_t sent_message_count_;
  // Buffer needed for grpc_slice to reference it when adding metatdata to
  // response.
  char stats_buf_[kMaxServerStatsLen];
};

constexpr uint32_t OpenCensusServerCallData::kMaxServerStatsLen;

OpenCensusServerCallData::OpenCensusServerCallData(
    grpc_metadata_batch* client_initial_metadata)
    : start_time_(absl::Now()), recv_message_count_(0), sent_message_count_(0) {
  ServerMetadataElements sml;
  FilterInitialMetadata(client_initial_metadata, &sml);
  path_ = std::move(sml.path);
  method_ = GetMethod(path_);
  auto tracing_enabled = OpenCensusTracingEnabled();
  GenerateServerContext(
      tracing_enabled ? sml.tracing_slice.as_string_view() : "",
      absl::StrCat("Recv.", method_), &context_);
  if (tracing_enabled) {
    auto* call_context = grpc_core::GetContext<grpc_call_context_element>();
    call_context[GRPC_CONTEXT_TRACING].value = &context_;
  }
  if (OpenCensusStatsEnabled()) {
    std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
        context_.tags().tags();
    tags.emplace_back(ServerMethodTagKey(), std::string(method_));
    ::opencensus::stats::Record({{RpcServerStartedRpcs(), 1}}, tags);
  }
}

void OpenCensusServerCallData::Finalize(
    const grpc_call_final_info* final_info) {
  if (OpenCensusStatsEnabled()) {
    const uint64_t request_size = GetOutgoingDataSize(final_info);
    const uint64_t response_size = GetIncomingDataSize(final_info);
    double elapsed_time_ms = absl::ToDoubleMilliseconds(elapsed_time_);
    std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
        context_.tags().tags();
    tags.emplace_back(ServerMethodTagKey(), std::string(method_));
    tags.emplace_back(
        ServerStatusTagKey(),
        std::string(StatusCodeToString(final_info->final_status)));
    ::opencensus::stats::Record(
        {{RpcServerSentBytesPerRpc(), static_cast<double>(response_size)},
         {RpcServerReceivedBytesPerRpc(), static_cast<double>(request_size)},
         {RpcServerServerLatency(), elapsed_time_ms},
         {RpcServerSentMessagesPerRpc(), sent_message_count_},
         {RpcServerReceivedMessagesPerRpc(), recv_message_count_}},
        tags);
  }
  if (OpenCensusTracingEnabled()) {
    context_.EndSpan();
  }
}

void OpenCensusServerCallData::OnServerTrailingMetadata(
    grpc_metadata_batch* server_trailing_metadata) {
  // We need to record the time when the trailing metadata was sent to
  // mark the completeness of the request.
  elapsed_time_ = absl::Now() - start_time_;
  if (OpenCensusStatsEnabled() && server_trailing_metadata != nullptr) {
    size_t len = ServerStatsSerialize(absl::ToInt64Nanoseconds(elapsed_time_),
                                      stats_buf_, kMaxServerStatsLen);
    if (len > 0) {
      server_trailing_metadata->Set(
          grpc_core::GrpcServerStatsBinMetadata(),
          grpc_core::Slice::FromCopiedBuffer(stats_buf_, len));
    }
  }
}

//
// OpenCensusServerFilter
//

const grpc_channel_filter OpenCensusServerFilter::kFilter =
    grpc_core::MakePromiseBasedFilter<
        OpenCensusServerFilter, grpc_core::FilterEndpoint::kServer,
        grpc_core::kFilterExaminesServerInitialMetadata |
            grpc_core::kFilterExaminesInboundMessages |
            grpc_core::kFilterExaminesOutboundMessages>("opencensus_server");

absl::StatusOr<OpenCensusServerFilter> OpenCensusServerFilter::Create(
    const grpc_core::ChannelArgs& /*args*/,
    grpc_core::ChannelFilter::Args /*filter_args*/) {
  OpenCensusRegistry::Get().RunFunctionsPostInit();
  return OpenCensusServerFilter();
}

grpc_core::ArenaPromise<grpc_core::ServerMetadataHandle>
OpenCensusServerFilter::MakeCallPromise(
    grpc_core::CallArgs call_args,
    grpc_core::NextPromiseFactory next_promise_factory) {
  auto continue_making_call_promise = [next_promise_factory =
                                           std::move(next_promise_factory),
                                       call_args =
                                           std::move(call_args)]() mutable {
    auto* calld = grpc_core::GetContext<grpc_core::Arena>()
                      ->ManagedNew<OpenCensusServerCallData>(
                          call_args.client_initial_metadata.get());
    call_args.client_to_server_messages->InterceptAndMap(
        [calld](grpc_core::MessageHandle message) {
          calld->OnRecvMessage();
          return message;
        });
    call_args.server_to_client_messages->InterceptAndMap(
        [calld](grpc_core::MessageHandle message) {
          calld->OnSendMessage();
          return message;
        });
    grpc_core::GetContext<grpc_core::CallFinalization>()->Add(
        [calld](const grpc_call_final_info* final_info) {
          calld->Finalize(final_info);
        });
    return grpc_core::OnCancel(Map(next_promise_factory(std::move(call_args)),
                                   [calld](grpc_core::ServerMetadataHandle md) {
                                     calld->OnServerTrailingMetadata(md.get());
                                     return md;
                                   }),
                               [calld]() { calld->OnCancel(); });
  };
  // If the OpenCensus plugin is not yet ready, then wait for it to be ready.
  if (!grpc::internal::OpenCensusRegistry::Get().Ready()) {
    auto notification = std::make_shared<PromiseNotification>();
    grpc::internal::OpenCensusRegistry::Get().NotifyOnReady(
        [notification]() { notification->Notify(); });
    return grpc_core::Seq([notification]() { return notification->Wait(); },
                          std::move(continue_making_call_promise));
  }
  return continue_making_call_promise();
}

}  // namespace internal
}  // namespace grpc
