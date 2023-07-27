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

#include "src/python/grpcio_observability/grpc_observability/server_call_tracer.h"

// TODO(xuanwn): clean up includes
#include <grpc/support/port_platform.h>

#include <constants.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/python/grpcio_observability/grpc_observability/observability_util.h"
#include "src/python/grpcio_observability/grpc_observability/python_census_context.h"

namespace grpc_observability {

namespace {

// server metadata elements
struct ServerO11yMetadata {
  grpc_core::Slice path;
  grpc_core::Slice tracing_slice;
  grpc_core::Slice census_proto;
};

void GetO11yMetadata(const grpc_metadata_batch* b, ServerO11yMetadata* som) {
  const auto* path = b->get_pointer(grpc_core::HttpPathMetadata());
  if (path != nullptr) {
    som->path = path->Ref();
  }
  if (PythonCensusTracingEnabled()) {
    const auto* grpc_trace_bin =
        b->get_pointer(grpc_core::GrpcTraceBinMetadata());
    if (grpc_trace_bin != nullptr) {
      som->tracing_slice = grpc_trace_bin->Ref();
    }
  }
  if (PythonCensusStatsEnabled()) {
    const auto* grpc_tags_bin =
        b->get_pointer(grpc_core::GrpcTagsBinMetadata());
    if (grpc_tags_bin != nullptr) {
      som->census_proto = grpc_tags_bin->Ref();
    }
  }
}

}  // namespace

//
// PythonOpenCensusServerCallTracer
//

class PythonOpenCensusServerCallTracer : public grpc_core::ServerCallTracer {
 public:
  // Maximum size of server stats that are sent on the wire.
  static constexpr uint32_t kMaxServerStatsLen = 16;

  PythonOpenCensusServerCallTracer()
      : start_time_(absl::Now()),
        recv_message_count_(0),
        sent_message_count_(0) {}

  std::string TraceId() override {
    return absl::BytesToHexString(
        absl::string_view(context_.SpanContext().TraceId()));
  }

  std::string SpanId() override {
    return absl::BytesToHexString(
        absl::string_view(context_.SpanContext().SpanId()));
  }

  bool IsSampled() override { return context_.SpanContext().IsSampled(); }

  // Please refer to `grpc_transport_stream_op_batch_payload` for details on
  // arguments.
  // It's not a requirement to have this metric thus left unimplemented.
  void RecordSendInitialMetadata(
      grpc_metadata_batch* /*send_initial_metadata*/) override {}

  void RecordSendTrailingMetadata(
      grpc_metadata_batch* send_trailing_metadata) override;

  void RecordSendMessage(const grpc_core::SliceBuffer& send_message) override {
    RecordAnnotation(
        absl::StrFormat("Send message: %ld bytes", send_message.Length()));
    ++sent_message_count_;
  }

  void RecordSendCompressedMessage(
      const grpc_core::SliceBuffer& send_compressed_message) override {
    RecordAnnotation(absl::StrFormat("Send compressed message: %ld bytes",
                                     send_compressed_message.Length()));
  }

  void RecordReceivedInitialMetadata(
      grpc_metadata_batch* recv_initial_metadata) override;

  void RecordReceivedMessage(
      const grpc_core::SliceBuffer& recv_message) override {
    RecordAnnotation(
        absl::StrFormat("Received message: %ld bytes", recv_message.Length()));
    ++recv_message_count_;
  }
  void RecordReceivedDecompressedMessage(
      const grpc_core::SliceBuffer& recv_decompressed_message) override {
    RecordAnnotation(absl::StrFormat("Received decompressed message: %ld bytes",
                                     recv_decompressed_message.Length()));
  }

  void RecordReceivedTrailingMetadata(
      grpc_metadata_batch* /*recv_trailing_metadata*/) override {}

  void RecordCancel(grpc_error_handle /*cancel_error*/) override {
    elapsed_time_ = absl::Now() - start_time_;
  }

  void RecordEnd(const grpc_call_final_info* final_info) override;

  void RecordAnnotation(absl::string_view annotation) override {
    if (!context_.SpanContext().IsSampled()) {
      return;
    }
    context_.AddSpanAnnotation(annotation);
  }

  void RecordAnnotation(const Annotation& annotation) override {
    if (!context_.SpanContext().IsSampled()) {
      return;
    }

    switch (annotation.type()) {
      default:
        context_.AddSpanAnnotation(annotation.ToString());
    }
  }

 private:
  PythonCensusContext context_;
  // server method
  grpc_core::Slice path_;
  absl::string_view method_;
  absl::Time start_time_;
  absl::Duration elapsed_time_;
  uint64_t recv_message_count_;
  uint64_t sent_message_count_;
  // Buffer needed for grpc_slice to reference it when adding metadata to
  // response.
  char stats_buf_[kMaxServerStatsLen];
};

void PythonOpenCensusServerCallTracer::RecordReceivedInitialMetadata(
    grpc_metadata_batch* recv_initial_metadata) {
  ServerO11yMetadata som;
  GetO11yMetadata(recv_initial_metadata, &som);
  path_ = std::move(som.path);
  method_ = GetMethod(path_);
  auto tracing_enabled = PythonCensusTracingEnabled();
  GenerateServerContext(
      tracing_enabled ? som.tracing_slice.as_string_view() : "",
      absl::StrCat("Recv.", method_), &context_);
  if (PythonCensusStatsEnabled()) {
    context_.Labels().emplace_back(kServerMethod, std::string(method_));
    RecordIntMetric(kRpcServerStartedRpcsMeasureName, 1, context_.Labels());
  }
}

void PythonOpenCensusServerCallTracer::RecordSendTrailingMetadata(
    grpc_metadata_batch* send_trailing_metadata) {
  // We need to record the time when the trailing metadata was sent to
  // mark the completeness of the request.
  elapsed_time_ = absl::Now() - start_time_;
  if (PythonCensusStatsEnabled() && send_trailing_metadata != nullptr) {
    size_t len = ServerStatsSerialize(absl::ToInt64Nanoseconds(elapsed_time_),
                                      stats_buf_, kMaxServerStatsLen);
    if (len > 0) {
      send_trailing_metadata->Set(
          grpc_core::GrpcServerStatsBinMetadata(),
          grpc_core::Slice::FromCopiedBuffer(stats_buf_, len));
    }
  }
}

void PythonOpenCensusServerCallTracer::RecordEnd(
    const grpc_call_final_info* final_info) {
  if (PythonCensusStatsEnabled()) {
    const uint64_t request_size = GetOutgoingDataSize(final_info);
    const uint64_t response_size = GetIncomingDataSize(final_info);
    double elapsed_time_ms = absl::ToDoubleMilliseconds(elapsed_time_);
    context_.Labels().emplace_back(kServerMethod, std::string(method_));
    context_.Labels().emplace_back(
        kServerStatus,
        std::string(StatusCodeToString(final_info->final_status)));
    RecordDoubleMetric(kRpcServerSentBytesPerRpcMeasureName,
                       static_cast<double>(response_size), context_.Labels());
    RecordDoubleMetric(kRpcServerReceivedBytesPerRpcMeasureName,
                       static_cast<double>(request_size), context_.Labels());
    RecordDoubleMetric(kRpcServerServerLatencyMeasureName, elapsed_time_ms,
                       context_.Labels());
    RecordIntMetric(kRpcServerCompletedRpcMeasureName, 1, context_.Labels());
    RecordIntMetric(kRpcServerSentMessagesPerRpcMeasureName,
                    sent_message_count_, context_.Labels());
    RecordIntMetric(kRpcServerReceivedMessagesPerRpcMeasureName,
                    recv_message_count_, context_.Labels());
  }
  if (PythonCensusTracingEnabled()) {
    context_.EndSpan();
    if (IsSampled()) {
      RecordSpan(context_.Span().ToCensusData());
    }
  }

  // After RecordEnd, Core will make no further usage of this ServerCallTracer,
  // so we are free it here.
  delete this;
}

//
// PythonOpenCensusServerCallTracerFactory
//

grpc_core::ServerCallTracer*
PythonOpenCensusServerCallTracerFactory::CreateNewServerCallTracer(
    grpc_core::Arena* arena) {
  // We don't use arena here to to ensure that memory is allocated and freed in
  // the same DLL in Windows.
  (void)arena;
  return new PythonOpenCensusServerCallTracer();
}

}  // namespace grpc_observability
