/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_CORE_EXT_CENSUS_TRACING_H
#define GRPC_CORE_EXT_CENSUS_TRACING_H

#include "src/core/ext/census/trace_context.h"
#include "src/core/ext/census/trace_label.h"
#include "src/core/ext/census/trace_status.h"
#include <grpc/support/time.h>
#include <stdbool.h>

/* These API functions deal with the creation/termination and annotation of
   trace spans.  Spans can be created from a previous span parent span
   (remote/local) or a from a NULL parent in which case a new trace is created.
   Annotations, labels, and network events can be added to the spans. The span
   context contains all the relevant user information about a span, namely the
   trace ID, span ID, and option flags. */

typedef struct trace_span_context {
  /* Trace span context stores Span ID, Trace ID, and option flags. */
  uint64_t trace_id_hi;
  uint64_t trace_id_lo;
  uint64_t span_id;
  uint32_t flags;
} trace_span_context;

typedef struct start_span_options {
  /* If set, this will override the Span.local_start_time for the Span. */
  gpr_timespec local_start_timestamp;

  /* If set, the Spans are linked to the created Span. */
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

/* Add a new Annotation to the Span. The description corresponds to
   Span->annotations[].description. */
void trace_add_span_annotation(const trace_string description,
                               const trace_label *labels, const size_t n_labels,
                               trace_span_context *span_ctxt);

/* Add a new NetworkEvent annotation to a Span. This function is only intended
  to be used by RPC systems (either client or server), not by higher level
  applications. The timestamp type will be system-defined, the sent argument
  designates whether this is a network send event (client request, server
  reply) or receive (server request, client reply). The id argument serves to
  uniquely identify each network message. */
void trace_add_span_network_event(const trace_string description,
                                  const trace_label *labels,
                                  const size_t n_labels,
                                  const gpr_timespec timestamp, bool sent,
                                  uint64_t id, trace_span_context *span_ctxt);

/* Add a set of labels to the Span. */
void trace_add_span_labels(const trace_label *labels, const size_t n_labels,
                           trace_span_context *span_ctxt);

/* Mark the end of Span Execution with the given status. Only the timing of the
first EndSpan call for a given Span will be recorded, and implementations are
free to ignore all further calls using the Span. EndSpanOptions can
optionally be NULL. */
void trace_end_span(const trace_status *status, trace_span_context *span_ctxt);

#endif
