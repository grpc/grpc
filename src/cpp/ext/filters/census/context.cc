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

#include "opencensus/tags/context_util.h"
#include "opencensus/trace/context_util.h"
#include "opencensus/trace/propagation/grpc_trace_bin.h"
#include "src/cpp/ext/filters/census/context.h"

namespace grpc {

using ::opencensus::tags::TagMap;
using ::opencensus::trace::Span;
using ::opencensus::trace::SpanContext;

void GenerateServerContext(absl::string_view tracing, absl::string_view method,
                           CensusContext* context) {
  // Destruct the current CensusContext to free the Span memory before
  // overwriting it below.
  context->~CensusContext();
  SpanContext parent_ctx =
      opencensus::trace::propagation::FromGrpcTraceBinHeader(tracing);
  if (parent_ctx.IsValid()) {
    new (context) CensusContext(method, parent_ctx);
    return;
  }
  new (context) CensusContext(method, TagMap{});
}

void GenerateClientContext(absl::string_view method, CensusContext* ctxt,
                           CensusContext* parent_ctxt) {
  // Destruct the current CensusContext to free the Span memory before
  // overwriting it below.
  ctxt->~CensusContext();
  if (parent_ctxt != nullptr) {
    SpanContext span_ctxt = parent_ctxt->Context();
    Span span = parent_ctxt->Span();
    if (span_ctxt.IsValid()) {
      new (ctxt) CensusContext(method, &span, TagMap{});
      return;
    }
  }
  const Span& span = opencensus::trace::GetCurrentSpan();
  const TagMap& tags = opencensus::tags::GetCurrentTagMap();
  if (span.context().IsValid()) {
    // Create span with parent.
    new (ctxt) CensusContext(method, &span, tags);
    return;
  }
  // Create span without parent.
  new (ctxt) CensusContext(method, tags);
}

size_t TraceContextSerialize(const ::opencensus::trace::SpanContext& context,
                             char* tracing_buf, size_t tracing_buf_size) {
  if (tracing_buf_size <
      opencensus::trace::propagation::kGrpcTraceBinHeaderLen) {
    return 0;
  }
  opencensus::trace::propagation::ToGrpcTraceBinHeader(
      context, reinterpret_cast<uint8_t*>(tracing_buf));
  return opencensus::trace::propagation::kGrpcTraceBinHeaderLen;
}

size_t StatsContextSerialize(size_t /*max_tags_len*/, grpc_slice* /*tags*/) {
  // TODO(unknown): Add implementation. Waiting on stats tagging to be added.
  return 0;
}

size_t ServerStatsSerialize(uint64_t server_elapsed_time, char* buf,
                            size_t buf_size) {
  return RpcServerStatsEncoding::Encode(server_elapsed_time, buf, buf_size);
}

size_t ServerStatsDeserialize(const char* buf, size_t buf_size,
                              uint64_t* server_elapsed_time) {
  return RpcServerStatsEncoding::Decode(absl::string_view(buf, buf_size),
                                        server_elapsed_time);
}

uint64_t GetIncomingDataSize(const grpc_call_final_info* final_info) {
  return final_info->stats.transport_stream_stats.incoming.data_bytes;
}

uint64_t GetOutgoingDataSize(const grpc_call_final_info* final_info) {
  return final_info->stats.transport_stream_stats.outgoing.data_bytes;
}

SpanContext SpanContextFromCensusContext(const census_context* ctxt) {
  return reinterpret_cast<const CensusContext*>(ctxt)->Context();
}

Span SpanFromCensusContext(const census_context* ctxt) {
  return reinterpret_cast<const CensusContext*>(ctxt)->Span();
}

absl::string_view StatusCodeToString(grpc_status_code code) {
  switch (code) {
    case GRPC_STATUS_OK:
      return "OK";
    case GRPC_STATUS_CANCELLED:
      return "CANCELLED";
    case GRPC_STATUS_UNKNOWN:
      return "UNKNOWN";
    case GRPC_STATUS_INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case GRPC_STATUS_DEADLINE_EXCEEDED:
      return "DEADLINE_EXCEEDED";
    case GRPC_STATUS_NOT_FOUND:
      return "NOT_FOUND";
    case GRPC_STATUS_ALREADY_EXISTS:
      return "ALREADY_EXISTS";
    case GRPC_STATUS_PERMISSION_DENIED:
      return "PERMISSION_DENIED";
    case GRPC_STATUS_UNAUTHENTICATED:
      return "UNAUTHENTICATED";
    case GRPC_STATUS_RESOURCE_EXHAUSTED:
      return "RESOURCE_EXHAUSTED";
    case GRPC_STATUS_FAILED_PRECONDITION:
      return "FAILED_PRECONDITION";
    case GRPC_STATUS_ABORTED:
      return "ABORTED";
    case GRPC_STATUS_OUT_OF_RANGE:
      return "OUT_OF_RANGE";
    case GRPC_STATUS_UNIMPLEMENTED:
      return "UNIMPLEMENTED";
    case GRPC_STATUS_INTERNAL:
      return "INTERNAL";
    case GRPC_STATUS_UNAVAILABLE:
      return "UNAVAILABLE";
    case GRPC_STATUS_DATA_LOSS:
      return "DATA_LOSS";
    default:
      // gRPC wants users of this enum to include a default branch so that
      // adding values is not a breaking change.
      return "UNKNOWN_STATUS";
  }
}

}  // namespace grpc
