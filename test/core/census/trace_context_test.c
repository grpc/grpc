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

#include "src/core/ext/census/resource.h"
#include <grpc/census.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "src/core/ext/census/base_resources.h"
#include "test/core/util/test_config.h"

#include "src/core/ext/census/trace_context.h"
#include "third_party/nanopb/pb_encode.h"
#include "third_party/nanopb/pb_decode.h"
#include "src/core/ext/census/gen/trace_context.pb.h"

bool validate_context(uint8_t* buffer, size_t buf_size) {
  google_trace_TraceContext ctxt1 = google_trace_TraceContext_init_zero;
  google_trace_TraceContext ctxt2 = google_trace_TraceContext_init_zero;
  size_t msg_length;

  ctxt1.has_trace_id = true;
  ctxt1.trace_id.has_hi = true;
  ctxt1.trace_id.has_lo = true;
  ctxt1.trace_id.lo = 1;
  ctxt1.trace_id.hi = 2;
  ctxt1.has_span_id = true;
  ctxt1.span_id = 3;

  if (!encode_trace_context(&ctxt1, buffer, buf_size, &msg_length)) {
    return false;
  }

  if (!decode_trace_context(&ctxt2, buffer, msg_length)) {
    return false;
  }

  if (!ctxt2.has_trace_id || !ctxt2.has_span_id) {
    return false;
  }

  fprintf(stderr, "encoding and decoding successful...\n");
  fprintf(stderr, "trace_id: (%"PRIu64": %"PRIu64")  span_id: %"PRIu64"\n",
          ctxt2.trace_id.hi, ctxt2.trace_id.lo, ctxt2.span_id);

  return true;
}

int main(int argc, char **argv) {
  uint8_t buffer[256] = {0};
  validate_context(buffer, sizeof(buffer));

  return 0;
}
