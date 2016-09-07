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

#include <grpc/census.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "src/core/ext/census/base_resources.h"
#include "src/core/ext/census/resource.h"
#include "test/core/util/test_config.h"

#include "src/core/ext/census/gen/trace_context.pb.h"
#include "src/core/ext/census/trace_context.h"
#include "third_party/nanopb/pb_decode.h"
#include "third_party/nanopb/pb_encode.h"

#define BUF_SIZE 512

// Encodes a proto-encoded TraceContext (ctxt1) to a buffer, and then decodes it
// to a second TraceContext (ctxt2).  Validates that the resulting TraceContext
// has a span_id, trace_id, and that the values are equal to those in initial
// TraceContext.
bool validate_encode_decode_context(google_trace_TraceContext *ctxt1,
                                    uint8_t *buffer, size_t buf_size) {
  google_trace_TraceContext ctxt2 = google_trace_TraceContext_init_zero;
  size_t msg_length;

  if (!encode_trace_context(ctxt1, buffer, buf_size, &msg_length)) {
    return false;
  }

  if (!decode_trace_context(&ctxt2, buffer, msg_length)) {
    return false;
  }

  if (!ctxt2.has_trace_id || !ctxt2.has_span_id) {
    return false;
  }

  GPR_ASSERT(ctxt1->trace_id.hi == ctxt2.trace_id.hi &&
             ctxt1->trace_id.lo == ctxt2.trace_id.lo &&
             ctxt1->span_id == ctxt2.span_id);

  return true;
}

// Decodes and proto-encoded TraceContext from a buffer.
bool validate_decode_context(uint8_t *buffer, size_t msg_length) {
  google_trace_TraceContext ctxt = google_trace_TraceContext_init_zero;

  // validate the decoding of a context written to buffer
  if (!decode_trace_context(&ctxt, buffer, msg_length)) {
    return false;
  }

  if (!ctxt.has_trace_id || !ctxt.has_span_id) {
    return false;
  }

  return true;
}

// Read an encoded trace context from a file.  Validates that the decoding
// gives the expected result (succeed).
static void read_context_from_file(const char *file, const bool succeed) {
  uint8_t buffer[BUF_SIZE];
  FILE *input = fopen(file, "rb");
  GPR_ASSERT(input != NULL);
  size_t nbytes = fread(buffer, 1, BUF_SIZE, input);
  GPR_ASSERT(nbytes < BUF_SIZE && feof(input) && !ferror(input));
  bool res = validate_decode_context(buffer, nbytes);
  GPR_ASSERT(res == succeed);
  GPR_ASSERT(fclose(input) == 0);
}

// Test full proto-buffer
static void test_full() {
  read_context_from_file("test/core/census/data/context_full.pb", true);
}

// Test empty proto-buffer
static void test_empty() {
  read_context_from_file("test/core/census/data/context_empty.pb", false);
}

// Test proto-buffer with only trace_id
static void test_trace_only() {
  read_context_from_file("test/core/census/data/context_trace_only.pb", false);
}

// Test proto-buffer with only span_id
static void test_span_only() {
  read_context_from_file("test/core/census/data/context_span_only.pb", false);
}

// Test proto-buffer without is_sampled value
static void test_no_sample() {
  read_context_from_file("test/core/census/data/context_no_sample.pb", true);
}

static void test_encode_decode() {
  uint8_t buffer[BUF_SIZE] = {0};

  google_trace_TraceContext ctxt1 = google_trace_TraceContext_init_zero;
  ctxt1.has_trace_id = true;
  ctxt1.trace_id.has_hi = true;
  ctxt1.trace_id.has_lo = true;
  ctxt1.trace_id.lo = 1;
  ctxt1.trace_id.hi = 2;
  ctxt1.has_span_id = true;
  ctxt1.span_id = 3;
  validate_encode_decode_context(&ctxt1, buffer, sizeof(buffer));

  google_trace_TraceContext ctxt2 = google_trace_TraceContext_init_zero;
  ctxt2.has_trace_id = true;
  ctxt2.trace_id.has_hi = false;
  ctxt2.trace_id.has_lo = false;
  ctxt2.has_span_id = true;
  validate_encode_decode_context(&ctxt2, buffer, sizeof(buffer));
}

// Test a corrupted proto-buffer
static void test_corrupt() {
  uint8_t buffer[BUF_SIZE] = {0};
  google_trace_TraceContext ctxt1 = google_trace_TraceContext_init_zero;
  size_t msg_length;

  ctxt1.has_trace_id = true;
  ctxt1.trace_id.has_hi = true;
  ctxt1.trace_id.has_lo = true;
  ctxt1.trace_id.lo = 1;
  ctxt1.trace_id.hi = 2;
  ctxt1.has_span_id = true;
  ctxt1.span_id = 3;
  encode_trace_context(&ctxt1, buffer, sizeof(buffer), &msg_length);

  // corrupt some bytes
  buffer[1] = 255;

  bool res = validate_decode_context(buffer, msg_length);
  GPR_ASSERT(res == false);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_full();
  test_empty();
  test_trace_only();
  test_span_only();
  test_encode_decode();
  test_corrupt();
  test_no_sample();

  return 0;
}
