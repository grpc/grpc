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

#include "src/cpp/ext/filters/census/server_call_tracer.h"

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "absl/meta/type_traits.h"
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
#include "opencensus/trace/span_id.h"
#include "opencensus/trace/trace_id.h"

#include <grpcpp/opencensus.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/cpp/ext/filters/census/context.h"
#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "src/cpp/ext/filters/census/measures.h"

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

// OpenCensusServerCallTracer implementation

class OpenCensusServerCallTracer : public grpc_core::ServerCallTracer {
 public:
  // Maximum size of server stats that are sent on the wire.
  static constexpr uint32_t kMaxServerStatsLen = 16;

  OpenCensusServerCallTracer()
      : start_time_(absl::Now()),
        recv_message_count_(0),
        sent_message_count_(0) {}

  std::string TraceId() override {
    return context_.Context().trace_id().ToHex();
  }

  std::string SpanId() override { return context_.Context().span_id().ToHex(); }

  bool IsSampled() override { return context_.Span().IsSampled(); }

  // Please refer to `grpc_transport_stream_op_batch_payload` for details on
  // arguments.
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
    if (!context_.Span().IsRecording()) {
      return;
    }
    context_.AddSpanAnnotation(annotation, {});
  }

  void RecordAnnotation(const Annotation& annotation) override {
    if (!context_.Span().IsRecording()) {
      return;
    }

    switch (annotation.type()) {
      default:
        context_.AddSpanAnnotation(annotation.ToString(), {});
    }
  }

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

void OpenCensusServerCallTracer::RecordReceivedInitialMetadata(
    grpc_metadata_batch* recv_initial_metadata) {
  ServerMetadataElements sml;
  FilterInitialMetadata(recv_initial_metadata, &sml);
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

void OpenCensusServerCallTracer::RecordSendTrailingMetadata(
    grpc_metadata_batch* send_trailing_metadata) {
  // We need to record the time when the trailing metadata was sent to
  // mark the completeness of the request.
  elapsed_time_ = absl::Now() - start_time_;
  if (OpenCensusStatsEnabled() && send_trailing_metadata != nullptr) {
    size_t len = ServerStatsSerialize(absl::ToInt64Nanoseconds(elapsed_time_),
                                      stats_buf_, kMaxServerStatsLen);
    if (len > 0) {
      send_trailing_metadata->Set(
          grpc_core::GrpcServerStatsBinMetadata(),
          grpc_core::Slice::FromCopiedBuffer(stats_buf_, len));
    }
  }
}

void OpenCensusServerCallTracer::RecordEnd(
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

//
// OpenCensusServerCallTracerFactory
//

grpc_core::ServerCallTracer*
OpenCensusServerCallTracerFactory::CreateNewServerCallTracer(
    grpc_core::Arena* arena) {
  return arena->ManagedNew<OpenCensusServerCallTracer>();
}

}  // namespace internal
}  // namespace grpc
