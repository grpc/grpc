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

#ifndef GRPC_CORE_EXT_CENSUS_TRACE_PROPAGATION_H
#define GRPC_CORE_EXT_CENSUS_TRACE_PROPAGATION_H

#include "src/core/ext/census/tracing.h"

/* Encoding and decoding functions for receiving and sending trace contexts
   over the wire.  Only RPC libraries should be calling these
   functions.  These functions return the number of bytes encoded/decoded
   (0 if a failure has occurred). buf_size indicates the size of the
   input/output buffer. trace_span_context is a struct that includes the
   trace ID, span ID, and a set of option flags (is_sampled, etc.). */

/* Converts a span context to a binary byte buffer. */
size_t trace_span_context_to_binary(const trace_span_context *ctxt,
                                    uint8_t *buf, size_t buf_size);

/* Reads a binary byte buffer and populates a span context structure. */
size_t binary_to_trace_span_context(const uint8_t *buf, size_t buf_size,
                                    trace_span_context *ctxt);

/* Converts a span context to an http metadata compatible string. */
size_t trace_span_context_to_http_format(const trace_span_context *ctxt,
                                         char *buf, size_t buf_size);

/* Reads an http metadata compatible string and populates a span context
   structure. */
size_t http_format_to_trace_span_context(const char *buf, size_t buf_size,
                                         trace_span_context *ctxt);

#endif /* GRPC_CORE_EXT_CENSUS_TRACE_PROPAGATION_H */
