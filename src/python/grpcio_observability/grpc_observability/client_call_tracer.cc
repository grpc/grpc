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

#include "client_call_tracer.h"

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"

#include <limits.h>

#include <atomic>


namespace grpc_observability {

constexpr uint32_t
    PythonOpenCensusCallTracer::PythonOpenCensusCallAttemptTracer::kMaxTraceContextLen;
constexpr uint32_t
    PythonOpenCensusCallTracer::PythonOpenCensusCallAttemptTracer::kMaxTagsLen;

//
// OpenCensusCallTracer
//

PythonOpenCensusCallTracer::PythonOpenCensusCallTracer(char* method, char* trace_id,
                                                       char* parent_span_id, bool tracing_enabled):
      method_(GetMethod(method)),
      tracing_enabled_(tracing_enabled) {
  GenerateClientContext(absl::StrCat("Sent.", method_), absl::string_view(trace_id),
                        absl::string_view(parent_span_id), &context_);
}

void PythonOpenCensusCallTracer::GenerateContext() {}

void PythonOpenCensusCallTracer::RecordAnnotation(absl::string_view annotation) {
  // If tracing is disabled, the following will be a no-op.
  context_.AddSpanAnnotation(annotation);
}


PythonOpenCensusCallTracer::~PythonOpenCensusCallTracer() {
  if (PythonOpenCensusStatsEnabled()) {
    std::vector<Label> labels = context_.Labels();
    labels.emplace_back(Label{kClientMethod, std::string(method_)});
    RecordIntMetric(kRpcClientRetriesPerCallMeasureName, retries_ - 1, labels); // exclude first attempt
    RecordIntMetric(kRpcClientTransparentRetriesPerCallMeasureName, transparent_retries_, labels);
    RecordDoubleMetric(kRpcClientRetryDelayPerCallMeasureName, ToDoubleMilliseconds(retry_delay_), labels);
  }

  if (tracing_enabled_) {
    context_.EndSpan();
    if (IsSampled()) {
      RecordSpan(context_.Span().ToCensusData());
    }
  }
  // Export span data.
}


PythonCensusContext PythonOpenCensusCallTracer::CreateCensusContextForCallAttempt() {
  auto context = PythonCensusContext(absl::StrCat("Attempt.", method_), &(context_.Span()), context_.Labels());
  return context;
}


PythonOpenCensusCallTracer::PythonOpenCensusCallAttemptTracer*
PythonOpenCensusCallTracer::StartNewAttempt(bool is_transparent_retry) {
  uint64_t attempt_num;
  {
    grpc_core::MutexLock lock(&mu_);
    if (transparent_retries_ != 0 || retries_ != 0) {
      if (PythonOpenCensusStatsEnabled() && num_active_rpcs_ == 0) {
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
  context_.AddChildSpan();
  return new PythonOpenCensusCallAttemptTracer(
      this, attempt_num, is_transparent_retry, false /* arena_allocated */);
}

//
// PythonOpenCensusCallTracer::PythonOpenCensusCallAttemptTracer
//

PythonOpenCensusCallTracer::PythonOpenCensusCallAttemptTracer::PythonOpenCensusCallAttemptTracer(
    PythonOpenCensusCallTracer* parent, uint64_t attempt_num,
    bool is_transparent_retry, bool arena_allocated)
    : parent_(parent),
      arena_allocated_(arena_allocated),
      context_(parent_->CreateCensusContextForCallAttempt()),
      start_time_(absl::Now()) {
  if (parent_->tracing_enabled_) {
    context_.AddSpanAttribute("previous-rpc-attempts", std::to_string(attempt_num));
    context_.AddSpanAttribute("transparent-retry", std::to_string(is_transparent_retry));
  }
  if (PythonOpenCensusStatsEnabled()) {
    std::vector<Label> labels = context_.Labels();
    labels.emplace_back(Label{kClientMethod, std::string(parent_->method_)});
    RecordIntMetric(kRpcClientStartedRpcsMeasureName, 1, labels);
  }
}


void PythonOpenCensusCallTracer::PythonOpenCensusCallAttemptTracer::
    RecordSendInitialMetadata(grpc_metadata_batch* send_initial_metadata) {
  if (parent_->tracing_enabled_) {
    char tracing_buf[kMaxTraceContextLen];
    size_t tracing_len = TraceContextSerialize(context_, tracing_buf,
                                               kMaxTraceContextLen);
    if (tracing_len > 0) {
      send_initial_metadata->Set(
          grpc_core::GrpcTraceBinMetadata(),
          grpc_core::Slice::FromCopiedBuffer(tracing_buf, tracing_len));
    }
  }
  if (PythonOpenCensusStatsEnabled()) {
    grpc_slice tags = grpc_empty_slice();
    // TODO(unknown): Add in tagging serialization.
    size_t encoded_tags_len = StatsContextSerialize(kMaxTagsLen, &tags);
    if (encoded_tags_len > 0) {
      send_initial_metadata->Set(grpc_core::GrpcTagsBinMetadata(),
                                 grpc_core::Slice(tags));
    }
  }
}


void PythonOpenCensusCallTracer::PythonOpenCensusCallAttemptTracer::RecordSendMessage(
    const grpc_core::SliceBuffer& /*send_message*/) {
  ++sent_message_count_;
}


void PythonOpenCensusCallTracer::PythonOpenCensusCallAttemptTracer::RecordReceivedMessage(
    const grpc_core::SliceBuffer& /*recv_message*/) {
  ++recv_message_count_;
}


namespace {

void FilterTrailingMetadata(grpc_metadata_batch* b, uint64_t* elapsed_time) {
  if (PythonOpenCensusStatsEnabled()) {
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


void PythonOpenCensusCallTracer::PythonOpenCensusCallAttemptTracer::
    RecordReceivedTrailingMetadata(
        absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
        const grpc_transport_stream_stats* transport_stream_stats) {
  status_code_ = status.code();
  if (PythonOpenCensusStatsEnabled()) {
    uint64_t elapsed_time = 0;
    if (recv_trailing_metadata != nullptr) {
      FilterTrailingMetadata(recv_trailing_metadata, &elapsed_time);
    }

    std::vector<Label> labels = context_.Labels();
    labels.emplace_back(Label{kClientMethod, std::string(parent_->method_)});
    std::string final_status = absl::StatusCodeToString(status_code_);
    labels.emplace_back(Label{kClientStatus, final_status});
    RecordDoubleMetric(kRpcClientSentBytesPerRpcMeasureName, static_cast<double>(transport_stream_stats != nullptr ? transport_stream_stats->outgoing.data_bytes : 0), labels);
    RecordDoubleMetric(kRpcClientReceivedBytesPerRpcMeasureName, static_cast<double>(transport_stream_stats != nullptr ? transport_stream_stats->incoming.data_bytes : 0), labels);
    RecordDoubleMetric(kRpcClientServerLatencyMeasureName, absl::ToDoubleMilliseconds(absl::Nanoseconds(elapsed_time)), labels);
    RecordDoubleMetric(kRpcClientRoundtripLatencyMeasureName, absl::ToDoubleMilliseconds(absl::Now() - start_time_), labels);
    if (grpc_core::IsTransportSuppliesClientLatencyEnabled()) {
      if (transport_stream_stats != nullptr && gpr_time_cmp(transport_stream_stats->latency,
                        gpr_inf_future(GPR_TIMESPAN)) != 0) {
        double latency_ms = absl::ToDoubleMilliseconds(absl::Microseconds(
            gpr_timespec_to_micros(transport_stream_stats->latency)));
        RecordDoubleMetric(kRpcClientTransportLatencyMeasureName, latency_ms, labels);
      }
    }
  }
}


void PythonOpenCensusCallTracer::PythonOpenCensusCallAttemptTracer::RecordCancel(
    absl::Status /*cancel_error*/) {
}


void PythonOpenCensusCallTracer::PythonOpenCensusCallAttemptTracer::RecordEnd(
    const gpr_timespec& /*latency*/) {
  if (PythonOpenCensusStatsEnabled()) {
    std::vector<Label> labels = context_.Labels();
    labels.emplace_back(Label{kClientMethod, std::string(parent_->method_)});
    labels.emplace_back(Label{kClientStatus, StatusCodeToString(status_code_)});
    RecordIntMetric(kRpcClientSentMessagesPerRpcMeasureName, sent_message_count_, labels);
    RecordIntMetric(kRpcClientReceivedMessagesPerRpcMeasureName, recv_message_count_, labels);

    grpc_core::MutexLock lock(&parent_->mu_);
    if (--parent_->num_active_rpcs_ == 0) {
      parent_->time_at_last_attempt_end_ = absl::Now();
    }
  }

  if (parent_->tracing_enabled_) {
    if (status_code_ != absl::StatusCode::kOk) {
      context_.Span().SetStatus(StatusCodeToString(status_code_));
    }
    context_.EndSpan();
    if (IsSampled()) {
      RecordSpan(context_.Span().ToCensusData());
    }
  }

  if (arena_allocated_) {
    this->~PythonOpenCensusCallAttemptTracer();
  } else {
    delete this;
  }
}


void PythonOpenCensusCallTracer::PythonOpenCensusCallAttemptTracer::RecordAnnotation(
    absl::string_view annotation) {
  // If tracing is disabled, the following will be a no-op.
  context_.AddSpanAnnotation(annotation);
}

}  // namespace grpc_observability