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

#include "src/core/ext/filters/census/context.h"

#include <grpc/census.h>
#include <grpc/grpc.h>
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"

namespace grpc_core {

using ::opencensus::CensusContext;
using ::opencensus::trace::Span;
using ::opencensus::trace::SpanContext;

void GenerateServerContext(absl::string_view tracing, absl::string_view stats,
                           absl::string_view primary_role,
                           absl::string_view method, CensusContext* context) {
  GrpcTraceContext trace_ctxt;
  TraceContextEncoding::Decode(tracing, &trace_ctxt);
  SpanContext parent_ctx = trace_ctxt.ToSpanContext();
  new (context) CensusContext(method, parent_ctx);
}

void GenerateClientContext(absl::string_view method, CensusContext* ctxt,
                           CensusContext* parent_ctxt) {
  if (parent_ctxt != nullptr) {
    SpanContext span_ctxt = parent_ctxt->Context();
    Span span = parent_ctxt->Span();
    if (span_ctxt.IsValid()) {
      new (ctxt) CensusContext(method, &span);
      return;
    }
  }
  new (ctxt) CensusContext(method);
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

}  // namespace grpc_core

// These functions are defined in include/grpc/grpc.h within global namespace.
void grpc_census_call_set_context(grpc_call* call, census_context* context) {
  GRPC_API_TRACE("grpc_census_call_set_context(call=%p, census_context=%p)", 2,
                 (call, context));
  if (context != nullptr) {
    grpc_call_context_set(call, GRPC_CONTEXT_TRACING, context, nullptr);
  }
}

census_context* grpc_census_call_get_context(grpc_call* call) {
  GRPC_API_TRACE("grpc_census_call_get_context(call=%p)", 1, (call));
  return static_cast<census_context*>(
      grpc_call_context_get(call, GRPC_CONTEXT_TRACING));
}
