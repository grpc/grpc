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

#include "src/cpp/ext/filters/census/client_filter.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "opencensus/stats/stats.h"
#include "opencensus/tags/tag_key.h"
#include "opencensus/tags/tag_map.h"
#include "opencensus/trace/span.h"
#include "opencensus/trace/span_context.h"
#include "opencensus/trace/status_code.h"

#include <grpc/slice.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/client_context.h>
#include <grpcpp/opencensus.h>
#include <grpcpp/support/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/cpp/ext/filters/census/context.h"
#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "src/cpp/ext/filters/census/measures.h"
#include "src/cpp/ext/filters/census/open_census_call_tracer.h"

namespace grpc {
namespace internal {

constexpr uint32_t
    OpenCensusCallTracer::OpenCensusCallAttemptTracer::kMaxTraceContextLen;
constexpr uint32_t
    OpenCensusCallTracer::OpenCensusCallAttemptTracer::kMaxTagsLen;

//
// OpenCensusClientFilter
//

const grpc_channel_filter OpenCensusClientFilter::kFilter =
    grpc_core::MakePromiseBasedFilter<OpenCensusClientFilter,
                                      grpc_core::FilterEndpoint::kClient, 0>(
        "opencensus_client");

absl::StatusOr<OpenCensusClientFilter> OpenCensusClientFilter::Create(
    const grpc_core::ChannelArgs& args, ChannelFilter::Args /*filter_args*/) {
  bool observability_enabled =
      args.GetInt(GRPC_ARG_ENABLE_OBSERVABILITY).value_or(true);
  return OpenCensusClientFilter(/*tracing_enabled=*/observability_enabled);
}

grpc_core::ArenaPromise<grpc_core::ServerMetadataHandle>
OpenCensusClientFilter::MakeCallPromise(
    grpc_core::CallArgs call_args,
    grpc_core::NextPromiseFactory next_promise_factory) {
  auto* path = call_args.client_initial_metadata->get_pointer(
      grpc_core::HttpPathMetadata());
  auto* call_context = grpc_core::GetContext<grpc_call_context_element>();
  auto* tracer =
      grpc_core::GetContext<grpc_core::Arena>()
          ->ManagedNew<OpenCensusCallTracer>(
              call_context, path != nullptr ? path->Ref() : grpc_core::Slice(),
              grpc_core::GetContext<grpc_core::Arena>(),
              OpenCensusTracingEnabled() && tracing_enabled_);
  GPR_DEBUG_ASSERT(
      call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value ==
      nullptr);
  call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value = tracer;
  call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].destroy = nullptr;
  return next_promise_factory(std::move(call_args));
}

//
// OpenCensusCallTracer::OpenCensusCallAttemptTracer
//

OpenCensusCallTracer::OpenCensusCallAttemptTracer::OpenCensusCallAttemptTracer(
    OpenCensusCallTracer* parent, uint64_t attempt_num,
    bool is_transparent_retry, bool arena_allocated)
    : parent_(parent),
      arena_allocated_(arena_allocated),
      context_(parent_->CreateCensusContextForCallAttempt()),
      start_time_(absl::Now()) {
  if (parent_->tracing_enabled_) {
    context_.AddSpanAttribute("previous-rpc-attempts", attempt_num);
    context_.AddSpanAttribute("transparent-retry", is_transparent_retry);
  }
  if (OpenCensusStatsEnabled()) {
    std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
        context_.tags().tags();
    tags.emplace_back(ClientMethodTagKey(), std::string(parent_->method_));
    ::opencensus::stats::Record({{RpcClientStartedRpcs(), 1}}, tags);
  }
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::
    RecordSendInitialMetadata(grpc_metadata_batch* send_initial_metadata) {
  if (parent_->tracing_enabled_) {
    char tracing_buf[kMaxTraceContextLen];
    size_t tracing_len = TraceContextSerialize(context_.Context(), tracing_buf,
                                               kMaxTraceContextLen);
    if (tracing_len > 0) {
      send_initial_metadata->Set(
          grpc_core::GrpcTraceBinMetadata(),
          grpc_core::Slice::FromCopiedBuffer(tracing_buf, tracing_len));
    }
  }
  if (OpenCensusStatsEnabled()) {
    grpc_slice tags = grpc_empty_slice();
    // TODO(unknown): Add in tagging serialization.
    size_t encoded_tags_len = StatsContextSerialize(kMaxTagsLen, &tags);
    if (encoded_tags_len > 0) {
      send_initial_metadata->Set(grpc_core::GrpcTagsBinMetadata(),
                                 grpc_core::Slice(tags));
    }
  }
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::RecordSendMessage(
    const grpc_core::SliceBuffer& send_message) {
  RecordAnnotation(
      absl::StrFormat("Send message: %ld bytes", send_message.Length()));
  ++sent_message_count_;
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::
    RecordSendCompressedMessage(
        const grpc_core::SliceBuffer& send_compressed_message) {
  RecordAnnotation(absl::StrFormat("Send compressed message: %ld bytes",
                                   send_compressed_message.Length()));
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::RecordReceivedMessage(
    const grpc_core::SliceBuffer& recv_message) {
  RecordAnnotation(
      absl::StrFormat("Received message: %ld bytes", recv_message.Length()));
  ++recv_message_count_;
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::
    RecordReceivedDecompressedMessage(
        const grpc_core::SliceBuffer& recv_decompressed_message) {
  RecordAnnotation(absl::StrFormat("Received decompressed message: %ld bytes",
                                   recv_decompressed_message.Length()));
}

namespace {

void FilterTrailingMetadata(grpc_metadata_batch* b, uint64_t* elapsed_time) {
  if (OpenCensusStatsEnabled()) {
    absl::optional<grpc_core::Slice> grpc_server_stats_bin =
        b->Take(grpc_core::GrpcServerStatsBinMetadata());
    if (grpc_server_stats_bin.has_value()) {
      ServerStatsDeserialize(
          reinterpret_cast<const char*>(grpc_server_stats_bin->data()),
          grpc_server_stats_bin->size(), elapsed_time);
    }
  }
}

}  // namespace

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::
    RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
        const grpc_transport_stream_stats* transport_stream_stats) {
  status_code_ = status.code();
  if (OpenCensusStatsEnabled()) {
    uint64_t elapsed_time = 0;
    if (recv_trailing_metadata != nullptr) {
      FilterTrailingMetadata(recv_trailing_metadata, &elapsed_time);
    }
    std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
        context_.tags().tags();
    tags.emplace_back(ClientMethodTagKey(), std::string(parent_->method_));
    tags.emplace_back(ClientStatusTagKey(),
                      absl::StatusCodeToString(status_code_));
    ::opencensus::stats::Record(
        // TODO(yashykt): Recording zeros here when transport_stream_stats is
        // nullptr is unfortunate and should be fixed.
        {{RpcClientSentBytesPerRpc(),
          static_cast<double>(transport_stream_stats != nullptr
                                  ? transport_stream_stats->outgoing.data_bytes
                                  : 0)},
         {RpcClientReceivedBytesPerRpc(),
          static_cast<double>(transport_stream_stats != nullptr
                                  ? transport_stream_stats->incoming.data_bytes
                                  : 0)},
         {RpcClientServerLatency(),
          ToDoubleMilliseconds(absl::Nanoseconds(elapsed_time))},
         {RpcClientRoundtripLatency(),
          absl::ToDoubleMilliseconds(absl::Now() - start_time_)}},
        tags);
    if (grpc_core::IsTransportSuppliesClientLatencyEnabled()) {
      if (transport_stream_stats != nullptr &&
          gpr_time_cmp(transport_stream_stats->latency,
                       gpr_inf_future(GPR_TIMESPAN)) != 0) {
        double latency_ms = absl::ToDoubleMilliseconds(absl::Microseconds(
            gpr_timespec_to_micros(transport_stream_stats->latency)));
        ::opencensus::stats::Record({{RpcClientTransportLatency(), latency_ms}},
                                    tags);
      }
    }
  }
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::RecordCancel(
    absl::Status /*cancel_error*/) {}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::RecordEnd(
    const gpr_timespec& /*latency*/) {
  if (OpenCensusStatsEnabled()) {
    std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
        context_.tags().tags();
    tags.emplace_back(ClientMethodTagKey(), std::string(parent_->method_));
    tags.emplace_back(ClientStatusTagKey(), StatusCodeToString(status_code_));
    ::opencensus::stats::Record(
        {{RpcClientSentMessagesPerRpc(), sent_message_count_},
         {RpcClientReceivedMessagesPerRpc(), recv_message_count_}},
        tags);
    grpc_core::MutexLock lock(&parent_->mu_);
    if (--parent_->num_active_rpcs_ == 0) {
      parent_->time_at_last_attempt_end_ = absl::Now();
    }
  }
  if (parent_->tracing_enabled_) {
    if (status_code_ != absl::StatusCode::kOk) {
      context_.Span().SetStatus(
          static_cast<opencensus::trace::StatusCode>(status_code_),
          StatusCodeToString(status_code_));
    }
    context_.EndSpan();
  }
  if (arena_allocated_) {
    this->~OpenCensusCallAttemptTracer();
  } else {
    delete this;
  }
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::RecordAnnotation(
    absl::string_view annotation) {
  if (!context_.Span().IsRecording()) {
    return;
  }
  context_.AddSpanAnnotation(annotation, {});
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::RecordAnnotation(
    const Annotation& annotation) {
  if (!context_.Span().IsRecording()) {
    return;
  }

  switch (annotation.type()) {
    default:
      context_.AddSpanAnnotation(annotation.ToString(), {});
  }
}

//
// OpenCensusCallTracer
//

OpenCensusCallTracer::OpenCensusCallTracer(
    grpc_call_context_element* call_context, grpc_core::Slice path,
    grpc_core::Arena* arena, bool tracing_enabled)
    : call_context_(call_context),
      path_(std::move(path)),
      method_(GetMethod(path_)),
      arena_(arena),
      tracing_enabled_(tracing_enabled) {
  auto* parent_context = reinterpret_cast<CensusContext*>(
      call_context_[GRPC_CONTEXT_TRACING].value);
  GenerateClientContext(tracing_enabled_ ? absl::StrCat("Sent.", method_) : "",
                        &context_,
                        (parent_context == nullptr) ? nullptr : parent_context);
}

OpenCensusCallTracer::~OpenCensusCallTracer() {
  if (OpenCensusStatsEnabled()) {
    std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
        context_.tags().tags();
    tags.emplace_back(ClientMethodTagKey(), std::string(method_));
    ::opencensus::stats::Record(
        {{RpcClientRetriesPerCall(), retries_ - 1},  // exclude first attempt
         {RpcClientTransparentRetriesPerCall(), transparent_retries_},
         {RpcClientRetryDelayPerCall(), ToDoubleMilliseconds(retry_delay_)}},
        tags);
  }
  if (tracing_enabled_) {
    context_.EndSpan();
  }
}

OpenCensusCallTracer::OpenCensusCallAttemptTracer*
OpenCensusCallTracer::StartNewAttempt(bool is_transparent_retry) {
  // We allocate the first attempt on the arena and all subsequent attempts
  // on the heap, so that in the common case we don't require a heap
  // allocation, nor do we unnecessarily grow the arena.
  bool is_first_attempt = true;
  uint64_t attempt_num;
  {
    grpc_core::MutexLock lock(&mu_);
    if (transparent_retries_ != 0 || retries_ != 0) {
      is_first_attempt = false;
      if (OpenCensusStatsEnabled() && num_active_rpcs_ == 0) {
        retry_delay_ += absl::Now() - time_at_last_attempt_end_;
      }
    }
    attempt_num = retries_;
    if (is_transparent_retry) {
      ++transparent_retries_;
    } else {
      ++retries_;
    }
    ++num_active_rpcs_;
  }
  if (is_first_attempt) {
    return arena_->New<OpenCensusCallAttemptTracer>(
        this, attempt_num, is_transparent_retry, true /* arena_allocated */);
  }
  return new OpenCensusCallAttemptTracer(
      this, attempt_num, is_transparent_retry, false /* arena_allocated */);
}

void OpenCensusCallTracer::RecordAnnotation(absl::string_view annotation) {
  if (!context_.Span().IsRecording()) {
    return;
  }
  context_.AddSpanAnnotation(annotation, {});
}

void OpenCensusCallTracer::RecordAnnotation(const Annotation& annotation) {
  if (!context_.Span().IsRecording()) {
    return;
  }

  switch (annotation.type()) {
    default:
      context_.AddSpanAnnotation(annotation.ToString(), {});
  }
}

void OpenCensusCallTracer::RecordApiLatency(absl::Duration api_latency,
                                            absl::StatusCode status_code) {
  if (OpenCensusStatsEnabled()) {
    std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
        context_.tags().tags();
    tags.emplace_back(ClientMethodTagKey(), std::string(method_));
    tags.emplace_back(ClientStatusTagKey(),
                      absl::StatusCodeToString(status_code));
    ::opencensus::stats::Record(
        {{RpcClientApiLatency(), absl::ToDoubleMilliseconds(api_latency)}},
        tags);
  }
}

CensusContext OpenCensusCallTracer::CreateCensusContextForCallAttempt() {
  if (!tracing_enabled_) return CensusContext(context_.tags());
  GPR_DEBUG_ASSERT(context_.Context().IsValid());
  auto context = CensusContext(absl::StrCat("Attempt.", method_),
                               &(context_.Span()), context_.tags());
  grpc::internal::OpenCensusRegistry::Get()
      .PopulateCensusContextWithConstantAttributes(&context);
  return context;
}

class OpenCensusClientInterceptor : public grpc::experimental::Interceptor {
 public:
  explicit OpenCensusClientInterceptor(grpc::experimental::ClientRpcInfo* info)
      : info_(info), start_time_(absl::Now()) {}

  void Intercept(
      grpc::experimental::InterceptorBatchMethods* methods) override {
    if (methods->QueryInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::POST_RECV_STATUS)) {
      auto* tracer = static_cast<OpenCensusCallTracer*>(
          grpc_call_context_get(info_->client_context()->c_call(),
                                GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE));
      if (tracer != nullptr) {
        tracer->RecordApiLatency(absl::Now() - start_time_,
                                 static_cast<absl::StatusCode>(
                                     methods->GetRecvStatus()->error_code()));
      }
    }
    methods->Proceed();
  }

 private:
  grpc::experimental::ClientRpcInfo* info_;
  // Start time for measuring end-to-end API latency
  absl::Time start_time_;
};

//
// OpenCensusClientInterceptorFactory
//

grpc::experimental::Interceptor*
OpenCensusClientInterceptorFactory::CreateClientInterceptor(
    grpc::experimental::ClientRpcInfo* info) {
  return new OpenCensusClientInterceptor(info);
}

}  // namespace internal
}  // namespace grpc
