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

#include "src/python/grpcio_observability/grpc_observability/python_census_context.h"

namespace grpc_observability {

void EnablePythonOpenCensusStats(bool enable) {
  g_open_census_stats_enabled = enable;
}


void EnablePythonOpenCensusTracing(bool enable) {
  g_open_census_tracing_enabled = enable;
}


bool PythonOpenCensusStatsEnabled() {
  return g_open_census_stats_enabled.load(std::memory_order_relaxed);
}


bool PythonOpenCensusTracingEnabled() {
  return g_open_census_tracing_enabled.load(std::memory_order_relaxed);
}


void GenerateClientContext(absl::string_view method, absl::string_view trace_id, absl::string_view parent_span_id,
                           PythonCensusContext* ctxt) {
  // Destruct the current CensusContext to free the Span memory before
  // overwriting it below.
  ctxt->~PythonCensusContext();
  if (method.empty()) {
    new (ctxt) PythonCensusContext();
    return;
  }
  if (!parent_span_id.empty()) {
    // Note that parent_span_id exist also means it was marked as sampled at Python OC, we'll respect that decision.
    SpanContext parent_context = SpanContext(std::string(trace_id), std::string(parent_span_id), true);
    new (ctxt) PythonCensusContext(method, parent_context);
    return;
  }
  // Create span without parent.
  new (ctxt) PythonCensusContext(method, trace_id);
}

void GenerateServerContext(absl::string_view header, absl::string_view method,
                           PythonCensusContext* context) {
  // Destruct the current CensusContext to free the Span memory before
  // overwriting it below.
  context->~PythonCensusContext();
  if (method.empty()) {
    new (context) PythonCensusContext();
    return;
  }
  SpanContext parent_ctx = FromGrpcTraceBinHeader(header);
  if (parent_ctx.IsValid()) {
    new (context) PythonCensusContext(method, parent_ctx);
  } else {
    new (context) PythonCensusContext(method);
  }
}


void ToGrpcTraceBinHeader(PythonCensusContext& ctx, uint8_t* out) {
  out[kVersionOfs] = kVersionId;
  out[kTraceIdOfs] = kTraceIdField;

  memcpy(reinterpret_cast<uint8_t*>(&out[kTraceIdOfs + 1]),
         absl::HexStringToBytes(absl::string_view(ctx.Span().Context().TraceId())).c_str(),
         kSizeTraceID);

  out[kSpanIdOfs] = kSpanIdField;
  memcpy(reinterpret_cast<uint8_t*>(&out[kSpanIdOfs + 1]),
         absl::HexStringToBytes(absl::string_view(ctx.Span().Context().SpanId())).c_str(),
         kSizeSpanID);

  out[kTraceOptionsOfs] = kTraceOptionsField;
  uint8_t trace_options_rep_[kSizeTraceOptions];
  trace_options_rep_[0] = ctx.Span().Context().IsSampled() ? 1 : 0;
  memcpy(reinterpret_cast<uint8_t*>(&out[kTraceOptionsOfs + 1]), trace_options_rep_, kSizeTraceOptions);
}


SpanContext FromGrpcTraceBinHeader(absl::string_view header) {
  if (header.size() < kGrpcTraceBinHeaderLen ||
      header[kVersionOfs] != kVersionId ||
      header[kTraceIdOfs] != kTraceIdField ||
      header[kSpanIdOfs] != kSpanIdField ||
      header[kTraceOptionsOfs] != kTraceOptionsField) {
    return SpanContext();  // Invalid.
  }

  uint8_t options = header[kTraceOptionsOfs + 1] & 1;
  constexpr uint8_t kIsSampled = 1;

  uint8_t trace_id_rep_[kTraceIdSize];
  memcpy(trace_id_rep_, reinterpret_cast<const uint8_t*>(&header[kTraceIdOfs + 1]), kTraceIdSize);
  
  uint8_t span_id_rep_[kSpanIdSize];
  memcpy(span_id_rep_, reinterpret_cast<const uint8_t*>(&header[kSpanIdOfs + 1]), kSpanIdSize);

  uint8_t trace_option_rep_[kTraceOptionsLen];
  memcpy(trace_option_rep_, &options, kTraceOptionsLen);

  SpanContext context(absl::BytesToHexString(absl::string_view(reinterpret_cast<const char *>(trace_id_rep_), kTraceIdSize)),
                      absl::BytesToHexString(absl::string_view(reinterpret_cast<const char *>(span_id_rep_), kSpanIdSize)),
                      trace_option_rep_[0] & kIsSampled);
  return context;
}


size_t TraceContextSerialize(PythonCensusContext& context,
                             char* tracing_buf, size_t tracing_buf_size) {
  if (tracing_buf_size < kGrpcTraceBinHeaderLen) {
    return 0;
  }
  ToGrpcTraceBinHeader(context, reinterpret_cast<uint8_t*>(tracing_buf));
  return kGrpcTraceBinHeaderLen;
}


size_t StatsContextSerialize(size_t /*max_tags_len*/, grpc_slice* /*tags*/) {
  // TODO(unknown): Add implementation. Waiting on stats tagging to be added.
  return 0;
}


size_t ServerStatsDeserialize(const char* buf, size_t buf_size,
                              uint64_t* server_elapsed_time) {
  return grpc::internal::RpcServerStatsEncoding::Decode(
      absl::string_view(buf, buf_size), server_elapsed_time);
}


size_t ServerStatsSerialize(uint64_t server_elapsed_time, char* buf,
                            size_t buf_size) {
  return grpc::internal::RpcServerStatsEncoding::Encode(server_elapsed_time, buf,
                                                  buf_size);
}


uint64_t GetIncomingDataSize(const grpc_call_final_info* final_info) {
  return final_info->stats.transport_stream_stats.incoming.data_bytes;
}


uint64_t GetOutgoingDataSize(const grpc_call_final_info* final_info) {
  return final_info->stats.transport_stream_stats.outgoing.data_bytes;
}


namespace {
// span_id is a 16-character hexadecimal encoded string.
std::string generateSpanId() {
  uint64_t span_id = absl::Uniform<uint64_t>(absl::BitGen());
  std::stringstream hex_string;
  hex_string << std::setfill('0') << std::setw(16) << std::hex << span_id;
  return std::string(hex_string.str());
}

// trace_id is a 32-character hexadecimal encoded string
std::string generateTraceId() {
  absl::uint128 trace_id = absl::Uniform<absl::uint128>(absl::BitGen());
  std::stringstream hex_string;
  hex_string << std::setfill('0') << std::setw(32) << std::hex << trace_id;
  return std::string(hex_string.str());
}

} // namespace

//
// Span
//

Span Span::StartSpan(absl::string_view name, Span* parent) {
  SpanContext context;
  std::string span_id = generateSpanId();
  std::string trace_id;
  std::string parent_span_id;
  bool should_sample;
  auto start_time = absl::Now();

  if (parent != nullptr) {
    parent_span_id = parent->Context().SpanId();
    trace_id = parent->Context().TraceId();
    should_sample = parent->Context().IsSampled();
  } else {
    trace_id = generateTraceId();
    should_sample = ShouldSample(trace_id);
  }

  context = SpanContext(trace_id, span_id, should_sample);
  return Span(std::string(name), parent_span_id, start_time, context);
}


Span Span::StartSpan(absl::string_view name, SpanContext parent_context) {
  std::string trace_id = parent_context.TraceId();
  std::string parent_span_id = parent_context.SpanId();
  std::string span_id = generateSpanId();
  bool should_sample = parent_context.IsSampled();
  auto start_time = absl::Now();
  SpanContext context(trace_id, span_id, should_sample);
  return Span(std::string(name), parent_span_id, start_time, context);
}


Span Span::StartSpan(absl::string_view name, absl::string_view trace_id) {
  std::string span_id = generateSpanId();
  auto start_time = absl::Now();
  bool should_sample = ShouldSample(std::string(trace_id));
  SpanContext context(std::string(trace_id), span_id, should_sample);
  return Span(std::string(name), "", start_time, context);
}


void Span::SetStatus(absl::string_view status) {
  status_ = std::string(status);
}


void Span::AddAttribute(absl::string_view key, absl::string_view value) {
  span_labels_.emplace_back(Label{std::string(key), std::string(value)});
}


void Span::AddAnnotation(absl::string_view description) {
  // Need a string format which can be converted to Python datetime.datetime class directly.
  std::string time_stamp = absl::FormatTime("%Y-%m-%d %H:%M:%E3S", absl::Now(), absl::UTCTimeZone());
  span_annotations_.emplace_back(Annotation{time_stamp, std::string(description)});
}


SpanCensusData Span::ToCensusData() {
  SpanCensusData census_data;
  absl::TimeZone utc =  absl::UTCTimeZone();
  census_data.name = name_;
  // Need a string format which can be exported to StackDriver directly.
  // See format details: https://cloud.google.com/trace/docs/reference/v2/rest/v2/projects.traces/batchWrite
  census_data.start_time = absl::FormatTime("%Y-%m-%dT%H:%M:%E6SZ", start_time_, utc);
  census_data.end_time = absl::FormatTime("%Y-%m-%dT%H:%M:%E6SZ", end_time_, utc);
  census_data.trace_id = Context().TraceId();
  census_data.span_id = Context().SpanId();
  census_data.should_sample = Context().IsSampled();
  census_data.parent_span_id = parent_span_id_;
  census_data.status = status_;
  census_data.span_labels = span_labels_;
  census_data.span_annotations = span_annotations_;
  census_data.child_span_count = child_span_count_;
  return census_data;
}

}  // namespace grpc_observability
