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

#include "src/core/ext/census/tracing.h"

#include <grpc/census.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/ext/census/mlog.h"

void trace_start_span(const trace_span_context *span_ctxt,
                      const trace_string name, const start_span_options *opts,
                      trace_span_context *new_span_ctxt,
                      bool has_remote_parent) {
  // Noop implementation.
}

void trace_add_span_annotation(const trace_string description,
                               const trace_label *labels, const size_t n_labels,
                               trace_span_context *span_ctxt) {
  // Noop implementation.
}

void trace_add_span_network_event_annotation(const trace_string description,
                                             const trace_label *labels,
                                             const size_t n_labels,
                                             const gpr_timespec timestamp,
                                             bool sent, uint64_t id,
                                             trace_span_context *span_ctxt) {
  // Noop implementation.
}

void trace_add_span_labels(const trace_label *labels, const size_t n_labels,
                           trace_span_context *span_ctxt) {
  // Noop implementation.
}

void trace_end_span(const trace_status *status, trace_span_context *span_ctxt) {
  // Noop implementation.
}
