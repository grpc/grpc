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

/* Functions for manipulating trace contexts as defined in
   src/proto/census/trace.proto */
#ifndef GRPC_CORE_EXT_CENSUS_TRACE_CONTEXT_H
#define GRPC_CORE_EXT_CENSUS_TRACE_CONTEXT_H

#include "src/core/ext/census/gen/trace_context.pb.h"

/* Span option flags. */
#define SPAN_OPTIONS_IS_SAMPLED 0x01

/* Maximum number of bytes required to encode a TraceContext (31)
1 byte for trace_id field
1 byte for trace_id length
1 byte for trace_id.hi field
8 bytes for trace_id.hi (uint64_t)
1 byte for trace_id.lo field
8 bytes for trace_id.lo (uint64_t)
1 byte for span_id field
8 bytes for span_id (uint64_t)
1 byte for is_sampled field
1 byte for is_sampled (bool) */
#define TRACE_MAX_CONTEXT_SIZE 31

/* Encode a trace context (ctxt) into proto format to the buffer provided.  The
size of buffer must be at least TRACE_MAX_CONTEXT_SIZE.  On success, returns the
number of bytes successfully encoded into buffer.  On failure, returns 0. */
size_t encode_trace_context(google_trace_TraceContext *ctxt, uint8_t *buffer,
                            const size_t buf_size);

/* Decode a proto-encoded TraceContext from the provided buffer into the
TraceContext structure (ctxt).  The function expects to be supplied the number
of bytes to be read from buffer (nbytes).  This function will also validate that
the TraceContext has a span_id and a trace_id, and will return false if either
of these do not exist. On success, returns true and false otherwise. */
bool decode_trace_context(google_trace_TraceContext *ctxt, uint8_t *buffer,
                          const size_t nbytes);

#endif /* GRPC_CORE_EXT_CENSUS_TRACE_CONTEXT_H */
