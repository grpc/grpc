/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/cpp/ext/filters/census/client_filter.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
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

#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>
#include <grpcpp/support/config.h>

#include "src/core/lib/channel/context.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_refcount.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/cpp/ext/filters/census/context.h"
#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "src/cpp/ext/filters/census/measures.h"

namespace grpc {

constexpr uint32_t
    OpenCensusCallTracer::OpenCensusCallAttemptTracer::kMaxTraceContextLen;
constexpr uint32_t
    OpenCensusCallTracer::OpenCensusCallAttemptTracer::kMaxTagsLen;

grpc_error_handle CensusClientCallData::Init(
    grpc_call_element* /* elem */, const grpc_call_element_args* args) {
  tracer_ = args->arena->New<OpenCensusCallTracer>(args);
  GPR_DEBUG_ASSERT(args->context[GRPC_CONTEXT_CALL_TRACER].value == nullptr);
  args->context[GRPC_CONTEXT_CALL_TRACER].value = tracer_;
  args->context[GRPC_CONTEXT_CALL_TRACER].destroy = [](void* tracer) {
    (static_cast<OpenCensusCallTracer*>(tracer))->~OpenCensusCallTracer();
  };
  return GRPC_ERROR_NONE;
}

void CensusClientCallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, TransportStreamOpBatch* op) {
  // Note that we are generating the overall call context here instead of in
  // the constructor of `OpenCensusCallTracer` due to the semantics of
  // `grpc_census_call_set_context` which allows the application to set the
  // census context for a call anytime before the first call to
  // `grpc_call_start_batch`.
  if (op->op()->send_initial_metadata) {
    tracer_->GenerateContext();
  }
  grpc_call_next_op(elem, op->op());
}

//
// OpenCensusCallTracer::OpenCensusCallAttemptTracer
//

namespace {

CensusContext CreateCensusContextForCallAttempt(
    absl::string_view method, const CensusContext& parent_context) {
  GPR_DEBUG_ASSERT(parent_context.Context().IsValid());
  return CensusContext(absl::StrCat("Attempt.", method), &parent_context.Span(),
                       parent_context.tags());
}

}  // namespace

OpenCensusCallTracer::OpenCensusCallAttemptTracer::OpenCensusCallAttemptTracer(
    OpenCensusCallTracer* parent, uint64_t attempt_num,
    bool is_transparent_retry, bool arena_allocated)
    : parent_(parent),
      arena_allocated_(arena_allocated),
      context_(CreateCensusContextForCallAttempt(parent_->method_,
                                                 parent_->context_)),
      start_time_(absl::Now()) {
  context_.AddSpanAttribute("previous-rpc-attempts", attempt_num);
  context_.AddSpanAttribute("transparent-retry", is_transparent_retry);
  std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
      context_.tags().tags();
  tags.emplace_back(ClientMethodTagKey(), std::string(parent_->method_));
  ::opencensus::stats::Record({{RpcClientStartedRpcs(), 1}}, tags);
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::
    RecordSendInitialMetadata(grpc_metadata_batch* send_initial_metadata) {
  char tracing_buf[kMaxTraceContextLen];
  size_t tracing_len = TraceContextSerialize(context_.Context(), tracing_buf,
                                             kMaxTraceContextLen);
  if (tracing_len > 0) {
    send_initial_metadata->Set(
        grpc_core::GrpcTraceBinMetadata(),
        grpc_core::Slice::FromCopiedBuffer(tracing_buf, tracing_len));
  }
  grpc_slice tags = grpc_empty_slice();
  // TODO(unknown): Add in tagging serialization.
  size_t encoded_tags_len = StatsContextSerialize(kMaxTagsLen, &tags);
  if (encoded_tags_len > 0) {
    send_initial_metadata->Set(grpc_core::GrpcTagsBinMetadata(),
                               grpc_core::Slice(tags));
  }
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::RecordSendMessage(
    const grpc_core::SliceBuffer& /*send_message*/) {
  ++sent_message_count_;
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::RecordReceivedMessage(
    const grpc_core::SliceBuffer& /*recv_message*/) {
  ++recv_message_count_;
}

namespace {

void FilterTrailingMetadata(grpc_metadata_batch* b, uint64_t* elapsed_time) {
  absl::optional<grpc_core::Slice> grpc_server_stats_bin =
      b->Take(grpc_core::GrpcServerStatsBinMetadata());
  if (grpc_server_stats_bin.has_value()) {
    ServerStatsDeserialize(
        reinterpret_cast<const char*>(grpc_server_stats_bin->data()),
        grpc_server_stats_bin->size(), elapsed_time);
  }
}

}  // namespace

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::
    RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
        const grpc_transport_stream_stats* transport_stream_stats) {
  status_code_ = status.code();
  if (recv_trailing_metadata == nullptr || transport_stream_stats == nullptr) {
    return;
  }
  uint64_t elapsed_time = 0;
  FilterTrailingMetadata(recv_trailing_metadata, &elapsed_time);
  std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
      context_.tags().tags();
  tags.emplace_back(ClientMethodTagKey(), std::string(parent_->method_));
  std::string final_status = absl::StatusCodeToString(status_code_);
  tags.emplace_back(ClientStatusTagKey(), final_status);
  ::opencensus::stats::Record(
      {{RpcClientSentBytesPerRpc(),
        static_cast<double>(transport_stream_stats->outgoing.data_bytes)},
       {RpcClientReceivedBytesPerRpc(),
        static_cast<double>(transport_stream_stats->incoming.data_bytes)},
       {RpcClientServerLatency(),
        ToDoubleMilliseconds(absl::Nanoseconds(elapsed_time))}},
      tags);
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::RecordCancel(
    grpc_error_handle cancel_error) {
  status_code_ = absl::StatusCode::kCancelled;
  GRPC_ERROR_UNREF(cancel_error);
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::RecordEnd(
    const gpr_timespec& /*latency*/) {
  double latency_ms = absl::ToDoubleMilliseconds(absl::Now() - start_time_);
  std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
      context_.tags().tags();
  tags.emplace_back(ClientMethodTagKey(), std::string(parent_->method_));
  tags.emplace_back(ClientStatusTagKey(), StatusCodeToString(status_code_));
  ::opencensus::stats::Record(
      {{RpcClientRoundtripLatency(), latency_ms},
       {RpcClientSentMessagesPerRpc(), sent_message_count_},
       {RpcClientReceivedMessagesPerRpc(), recv_message_count_}},
      tags);
  if (status_code_ != absl::StatusCode::kOk) {
    context_.Span().SetStatus(opencensus::trace::StatusCode(status_code_),
                              StatusCodeToString(status_code_));
  }
  context_.EndSpan();
  grpc_core::MutexLock lock(&parent_->mu_);
  if (--parent_->num_active_rpcs_ == 0) {
    parent_->time_at_last_attempt_end_ = absl::Now();
  }
  if (arena_allocated_) {
    this->~OpenCensusCallAttemptTracer();
  } else {
    delete this;
  }
}

//
// OpenCensusCallTracer
//

OpenCensusCallTracer::OpenCensusCallTracer(const grpc_call_element_args* args)
    : call_context_(args->context),
      path_(grpc_slice_ref_internal(args->path)),
      method_(GetMethod(path_)),
      arena_(args->arena) {}

OpenCensusCallTracer::~OpenCensusCallTracer() {
  std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
      context_.tags().tags();
  tags.emplace_back(ClientMethodTagKey(), std::string(method_));
  ::opencensus::stats::Record(
      {{RpcClientRetriesPerCall(), retries_ - 1},  // exclude first attempt
       {RpcClientTransparentRetriesPerCall(), transparent_retries_},
       {RpcClientRetryDelayPerCall(), ToDoubleMilliseconds(retry_delay_)}},
      tags);
  context_.EndSpan();
}

void OpenCensusCallTracer::GenerateContext() {
  auto* parent_context = reinterpret_cast<CensusContext*>(
      call_context_[GRPC_CONTEXT_TRACING].value);
  GenerateClientContext(absl::StrCat("Sent.", method_), &context_,
                        (parent_context == nullptr) ? nullptr : parent_context);
}

OpenCensusCallTracer::OpenCensusCallAttemptTracer*
OpenCensusCallTracer::StartNewAttempt(bool is_transparent_retry) {
  // We allocate the first attempt on the arena and all subsequent attempts on
  // the heap, so that in the common case we don't require a heap allocation,
  // nor do we unnecessarily grow the arena.
  bool is_first_attempt = true;
  uint64_t attempt_num;
  {
    grpc_core::MutexLock lock(&mu_);
    if (transparent_retries_ != 0 || retries_ != 0) {
      is_first_attempt = false;
      if (num_active_rpcs_ == 0) {
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

}  // namespace grpc
