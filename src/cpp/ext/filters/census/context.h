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

#ifndef GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_CONTEXT_H
#define GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_CONTEXT_H

#include <grpc/support/port_platform.h>

#include <grpc/status.h>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#include "opencensus/context/context.h"
#include "opencensus/tags/tag_map.h"
#include "opencensus/trace/context_util.h"
#include "opencensus/trace/span.h"
#include "opencensus/trace/span_context.h"
#include "opencensus/trace/trace_params.h"

#include "src/core/lib/slice/slice_internal.h"
#include "src/cpp/common/channel_filter.h"
#include "src/cpp/ext/filters/census/rpc_encoding.h"

// This is needed because grpc has hardcoded CensusContext with a
// forward declaration of 'struct census_context;'
struct census_context;

namespace grpc {

// Thread compatible.
class CensusContext {
 public:
  CensusContext() : span_(::opencensus::trace::Span::BlankSpan()), tags_({}) {}

  explicit CensusContext(absl::string_view name,
                         const ::opencensus::tags::TagMap& tags)
      : span_(::opencensus::trace::Span::StartSpan(name)), tags_(tags) {}

  CensusContext(absl::string_view name, const ::opencensus::trace::Span* parent,
                const ::opencensus::tags::TagMap& tags)
      : span_(::opencensus::trace::Span::StartSpan(name, parent)),
        tags_(tags) {}

  CensusContext(absl::string_view name,
                const ::opencensus::trace::SpanContext& parent_ctxt)
      : span_(::opencensus::trace::Span::StartSpanWithRemoteParent(
            name, parent_ctxt)),
        tags_({}) {}

  const ::opencensus::trace::Span& Span() const { return span_; }
  const ::opencensus::tags::TagMap& tags() const { return tags_; }

  ::opencensus::trace::SpanContext Context() const { return Span().context(); }
  void EndSpan() { Span().End(); }

 private:
  ::opencensus::trace::Span span_;
  ::opencensus::tags::TagMap tags_;
};

// Serializes the outgoing trace context. tracing_buf must be
// opencensus::trace::propagation::kGrpcTraceBinHeaderLen bytes long.
size_t TraceContextSerialize(const ::opencensus::trace::SpanContext& context,
                             char* tracing_buf, size_t tracing_buf_size);

// Serializes the outgoing stats context.  Field IDs are 1 byte followed by
// field data. A 1 byte version ID is always encoded first. Tags are directly
// serialized into the given grpc_slice.
size_t StatsContextSerialize(size_t max_tags_len, grpc_slice* tags);

// Serialize outgoing server stats. Returns the number of bytes serialized.
size_t ServerStatsSerialize(uint64_t server_elapsed_time, char* buf,
                            size_t buf_size);

// Deserialize incoming server stats. Returns the number of bytes deserialized.
size_t ServerStatsDeserialize(const char* buf, size_t buf_size,
                              uint64_t* server_elapsed_time);

// Deserialize the incoming SpanContext and generate a new server context based
// on that. This new span will never be a root span. This should only be called
// with a blank CensusContext as it overwrites it.
void GenerateServerContext(absl::string_view tracing, absl::string_view method,
                           CensusContext* context);

// Creates a new client context that is by default a new root context.
// If the current context is the default context then the newly created
// span automatically becomes a root span. This should only be called with a
// blank CensusContext as it overwrites it.
void GenerateClientContext(absl::string_view method, CensusContext* ctxt,
                           CensusContext* parent_ctx);

// Returns the incoming data size from the grpc call final info.
uint64_t GetIncomingDataSize(const grpc_call_final_info* final_info);

// Returns the outgoing data size from the grpc call final info.
uint64_t GetOutgoingDataSize(const grpc_call_final_info* final_info);

// These helper functions return the SpanContext and Span, respectively
// associated with the census_context* stored by grpc. The user will need to
// call this for manual propagation of tracing data.
::opencensus::trace::SpanContext SpanContextFromCensusContext(
    const census_context* ctxt);
::opencensus::trace::Span SpanFromCensusContext(const census_context* ctxt);

// Returns a string representation of the StatusCode enum.
absl::string_view StatusCodeToString(grpc_status_code code);

inline absl::string_view GetMethod(const grpc_slice* path) {
  if (GRPC_SLICE_IS_EMPTY(*path)) {
    return "";
  }
  // Check for leading '/' and trim it if present.
  return absl::StripPrefix(absl::string_view(reinterpret_cast<const char*>(
                                                 GRPC_SLICE_START_PTR(*path)),
                                             GRPC_SLICE_LENGTH(*path)),
                           "/");
}

}  // namespace grpc

#endif /* GRPC_INTERNAL_CPP_EXT_FILTERS_CENSUS_CONTEXT_H */
