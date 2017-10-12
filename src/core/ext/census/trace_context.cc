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
