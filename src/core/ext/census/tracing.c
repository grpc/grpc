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

#include "src/core/ext/census/tracing.h"

#include <grpc/census.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <openssl/rand.h>
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
