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

#include "src/core/ext/census/trace_context.h"

#include <grpc/census.h>
#include <grpc/support/log.h>
#include <stdbool.h>

#include "third_party/nanopb/pb_decode.h"
#include "third_party/nanopb/pb_encode.h"

// This function assumes the TraceContext is valid.
size_t encode_trace_context(google_trace_TraceContext *ctxt, uint8_t *buffer,
                            const size_t buf_size) {
  // Create a stream that will write to our buffer.
  pb_ostream_t stream = pb_ostream_from_buffer(buffer, buf_size);

  // encode message
  bool status = pb_encode(&stream, google_trace_TraceContext_fields, ctxt);

  if (!status) {
    gpr_log(GPR_DEBUG, "TraceContext encoding failed: %s",
            PB_GET_ERROR(&stream));
    return 0;
  }

  return stream.bytes_written;
}

bool decode_trace_context(google_trace_TraceContext *ctxt, uint8_t *buffer,
                          const size_t nbytes) {
  // Create a stream that reads nbytes from the buffer.
  pb_istream_t stream = pb_istream_from_buffer(buffer, nbytes);

  // decode message
  bool status = pb_decode(&stream, google_trace_TraceContext_fields, ctxt);

  if (!status) {
    gpr_log(GPR_DEBUG, "TraceContext decoding failed: %s",
            PB_GET_ERROR(&stream));
    return false;
  }

  // check fields
  if (!ctxt->has_trace_id_hi || !ctxt->has_trace_id_lo) {
    gpr_log(GPR_DEBUG, "Invalid TraceContext: missing trace_id");
    return false;
  }
  if (!ctxt->has_span_id) {
    gpr_log(GPR_DEBUG, "Invalid TraceContext: missing span_id");
    return false;
  }

  return true;
}
