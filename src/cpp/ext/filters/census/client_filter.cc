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

#include <string>
#include <utility>
#include <vector>

#include "src/cpp/ext/filters/census/client_filter.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "opencensus/stats/stats.h"
#include "opencensus/tags/context_util.h"
#include "opencensus/tags/tag_key.h"
#include "opencensus/tags/tag_map.h"
#include "src/core/lib/surface/call.h"
#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "src/cpp/ext/filters/census/measures.h"

namespace grpc {

constexpr uint32_t
    OpenCensusCallTracer::OpenCensusCallAttemptTracer::kMaxTraceContextLen;
constexpr uint32_t
    OpenCensusCallTracer::OpenCensusCallAttemptTracer::kMaxTagsLen;

namespace {

void FilterTrailingMetadata(grpc_metadata_batch* b, uint64_t* elapsed_time) {
  if (b->idx.named.grpc_server_stats_bin != nullptr) {
    ServerStatsDeserialize(
        reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(
            GRPC_MDVALUE(b->idx.named.grpc_server_stats_bin->md))),
        GRPC_SLICE_LENGTH(GRPC_MDVALUE(b->idx.named.grpc_server_stats_bin->md)),
        elapsed_time);
    grpc_metadata_batch_remove(b, b->idx.named.grpc_server_stats_bin);
  }
}

}  // namespace

grpc_error_handle CensusClientCallData::Init(
    grpc_call_element* /* elem */, const grpc_call_element_args* args) {
  tracer_ = args->arena->New<OpenCensusCallTracer>(args);
  grpc_call_context_set(
      grpc_call_from_top_element(grpc_call_stack_element(args->call_stack, 0)),
      GRPC_CONTEXT_CALL_TRACER, tracer_, [](void* tracer) {
        (static_cast<OpenCensusCallTracer*>(tracer))->~OpenCensusCallTracer();
      });
  return GRPC_ERROR_NONE;
}

//
// OpenCensusCallTracer::OpenCensusCallAttemptTracer
//

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::
    RecordSendInitialMetadata(grpc_metadata_batch* send_initial_metadata,
                              uint32_t /* flags */) {
  GenerateClientContext(
      parent_->qualified_method_, &context_,
      (parent_->context_ == nullptr) ? nullptr : parent_->context_);
  size_t tracing_len = TraceContextSerialize(parent_->context_->Context(),
                                             tracing_buf_, kMaxTraceContextLen);
  if (tracing_len > 0) {
    GRPC_LOG_IF_ERROR(
        "census grpc_filter",
        grpc_metadata_batch_add_tail(
            send_initial_metadata, &tracing_bin_,
            grpc_mdelem_from_slices(
                GRPC_MDSTR_GRPC_TRACE_BIN,
                grpc_core::UnmanagedMemorySlice(tracing_buf_, tracing_len)),
            GRPC_BATCH_GRPC_TRACE_BIN));
  }
  grpc_slice tags = grpc_empty_slice();
  // TODO(unknown): Add in tagging serialization.
  size_t encoded_tags_len = StatsContextSerialize(kMaxTagsLen, &tags);
  if (encoded_tags_len > 0) {
    GRPC_LOG_IF_ERROR(
        "census grpc_filter",
        grpc_metadata_batch_add_tail(
            send_initial_metadata, &stats_bin_,
            grpc_mdelem_from_slices(GRPC_MDSTR_GRPC_TAGS_BIN, tags),
            GRPC_BATCH_GRPC_TAGS_BIN));
  }
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::RecordSendMessage(
    const grpc_core::ByteStream& /* send_message */) {
  ++sent_message_count_;
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::RecordReceivedMessage(
    const grpc_core::ByteStream& /* recv_message */) {
  recv_message_count_++;
}
void OpenCensusCallTracer::OpenCensusCallAttemptTracer::
    RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
        const grpc_transport_stream_stats& transport_stream_stats) {
  FilterTrailingMetadata(recv_trailing_metadata, &elapsed_time_);
  const uint64_t request_size = transport_stream_stats.incoming.data_bytes;
  const uint64_t response_size = transport_stream_stats.outgoing.data_bytes;
  std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
      context_.tags().tags();
  tags.emplace_back(ClientMethodTagKey(), std::string(parent_->method_));
  status_code_ = status.code();
  std::string final_status = absl::StatusCodeToString(status_code_);
  tags.emplace_back(ClientStatusTagKey(), final_status);
  ::opencensus::stats::Record(
      {{RpcClientSentBytesPerRpc(), static_cast<double>(request_size)},
       {RpcClientReceivedBytesPerRpc(), static_cast<double>(response_size)},
       {RpcClientServerLatency(),
        ToDoubleMilliseconds(absl::Nanoseconds(elapsed_time_))},
       {RpcClientSentMessagesPerRpc(), sent_message_count_},
       {RpcClientReceivedMessagesPerRpc(), recv_message_count_}},
      tags);
  if (!status.ok()) {
    context_.Span().SetStatus(opencensus::trace::StatusCode(status.raw_code()),
                              final_status);
  }
}

void OpenCensusCallTracer::OpenCensusCallAttemptTracer::RecordEnd(
    const gpr_timespec& /* latency */) {
  double latency_ms = absl::ToDoubleMilliseconds(absl::Now() - start_time_);
  std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
      context_.tags().tags();
  tags.emplace_back(ClientMethodTagKey(), std::string(parent_->method_));
  tags.emplace_back(ClientStatusTagKey(), StatusCodeToString(status_code_));
  ::opencensus::stats::Record({{RpcClientRoundtripLatency(), latency_ms}},
                              tags);
  context_.EndSpan();
  grpc_core::MutexLock lock(&parent_->mu_);
  if (--parent_->num_active_rpcs_ == 0) {
    parent_->time_at_last_attempt_end_ = grpc_core::ExecCtx::Get()->Now();
  }
  delete this;
}

//
// OpenCensusCallTracer
//

OpenCensusCallTracer::OpenCensusCallTracer(const grpc_call_element_args* args) {
  path_ = grpc_slice_ref_internal(args->path);
  method_ = GetMethod(&path_);
  qualified_method_ = absl::StrCat("Sent.", method_);
  context_ = reinterpret_cast<CensusContext*>(args->context);
  time_at_last_attempt_end_ = grpc_core::ExecCtx::Get()->Now();
}

OpenCensusCallTracer::~OpenCensusCallTracer() {
  std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags =
      opencensus::tags::GetCurrentTagMap().tags();
  tags.emplace_back(ClientMethodTagKey(), std::string(method_));
  ::opencensus::stats::Record(
      {{RpcClientRetriesPerCall(), retries_},
       {RpcClientTransparentRetriesPerCall(), transparent_retries_},
       {RpcClientRetryDelayPerCall(),
        ToDoubleMilliseconds(absl::Milliseconds(retry_delay_))}},
      tags);
  grpc_slice_unref_internal(path_);
}

OpenCensusCallTracer::OpenCensusCallAttemptTracer*
OpenCensusCallTracer::StartNewAttempt(bool is_transparent_retry) {
  // TODO(yashykt): Should this go on the arena? Pros - Avoids an extra
  // allocation on the call. Cons - There can be potentially a large number of
  // retries which might unnecessarily increase the size of the arena.
  {
    grpc_core::MutexLock lock(&mu_);
    if (is_transparent_retry) {
      ++transparent_retries_;
    } else {
      ++retries_;
    }
    if (num_active_rpcs_ == 0) {
      grpc_millis current_time = grpc_core::ExecCtx::Get()->Now();
      // Monotonic clock
      GPR_DEBUG_ASSERT(current_time >= time_at_last_attempt_end_);
      retry_delay_ +=
          grpc_core::ExecCtx::Get()->Now() - time_at_last_attempt_end_;
    }
    ++num_active_rpcs_;
  }
  return new OpenCensusCallAttemptTracer(this);
}

}  // namespace grpc
