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
