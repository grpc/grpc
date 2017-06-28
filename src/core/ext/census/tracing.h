/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_EXT_CENSUS_TRACING_H
#define GRPC_CORE_EXT_CENSUS_TRACING_H

#include <grpc/support/time.h>
#include <stdbool.h>
#include "src/core/ext/census/trace_context.h"
#include "src/core/ext/census/trace_label.h"
#include "src/core/ext/census/trace_status.h"

/* This is the low level tracing API that other languages will interface with.
   This is not intended to be accessed by the end-user, therefore it has been
   designed with performance in mind rather than ease of use. */

/* The tracing level. */
enum TraceLevel {
  /* Annotations on this context will be silently discarded. */
  NO_TRACING = 0,
  /* Annotations will not be saved to a persistent store. They will be
     available via local APIs only. This setting is not propagated to child
     spans. */
  TRANSIENT_TRACING = 1,
  /* Annotations are recorded for the entire distributed trace and they are
     saved to a persistent store. This setting is propagated to child spans. */
  PERSISTENT_TRACING = 2,
};

typedef struct trace_span_context {
  /* Trace span context stores Span ID, Trace ID, and option flags. */
  /* Trace ID is 128 bits split into 2 64-bit chunks (hi and lo). */
  uint64_t trace_id_hi;
  uint64_t trace_id_lo;
  /* Span ID is 64 bits. */
  uint64_t span_id;
  /* Span-options is 32-bit value which contains flag options. */
  uint32_t span_options;
} trace_span_context;

typedef struct start_span_options {
  /* If set, this will override the Span.local_start_time for the Span. */
  gpr_timespec local_start_timestamp;

  /* Linked spans can be used to identify spans that are linked to this span in
     a different trace.  This can be used (for example) in batching operations,
     where a single batch handler processes multiple requests from different
     traces. If set, points to a list of Spans are linked to the created Span.*/
  trace_span_context *linked_spans;
  /* The number of linked spans. */
  size_t n_linked_spans;
} start_span_options;

/* Create a new child Span (or root if parent is NULL), with parent being the
   designated Span. The child span will have the provided name and starting
   span options (optional). The bool has_remote_parent marks whether the
   context refers to a remote parent span or not. */
void trace_start_span(const trace_span_context *span_ctxt,
                      const trace_string name, const start_span_options *opts,
                      trace_span_context *new_span_ctxt,
                      bool has_remote_parent);

/* Add a new Annotation to the Span. Annotations consist of a description
   (trace_string) and a set of n labels (trace_label).  This can be populated
   with arbitrary user data. */
void trace_add_span_annotation(const trace_string description,
                               const trace_label *labels, const size_t n_labels,
                               trace_span_context *span_ctxt);

/* Add a new NetworkEvent annotation to a Span. This function is only intended
  to be used by RPC systems (either client or server), not by higher level
  applications. The timestamp type will be system-defined, the sent argument
  designates whether this is a network send event (client request, server
  reply)or receive (server request, client reply). The id argument corresponds
  to Span.Annotation.NetworkEvent.id from the data model, and serves to uniquely
  identify each network message. */
void trace_add_span_network_event(const trace_string description,
                                  const trace_label *labels,
                                  const size_t n_labels,
                                  const gpr_timespec timestamp, bool sent,
                                  uint64_t id, trace_span_context *span_ctxt);

/* Add a set of labels to the Span. These will correspond to the field
Span.labels in the data model. */
void trace_add_span_labels(const trace_label *labels, const size_t n_labels,
                           trace_span_context *span_ctxt);

/* Mark the end of Span Execution with the given status. Only the timing of the
first EndSpan call for a given Span will be recorded, and implementations are
free to ignore all further calls using the Span. EndSpanOptions can
optionally be NULL. */
void trace_end_span(const trace_status *status, trace_span_context *span_ctxt);

#endif /* GRPC_CORE_EXT_CENSUS_TRACING_H */
