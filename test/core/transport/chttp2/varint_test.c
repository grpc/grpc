/*
 *
 * Copyright 2015, Google Inc.
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

#include "src/core/transport/chttp2/varint.h"

#include <grpc/support/log.h>
#include <grpc/support/slice.h>

#include "test/core/util/test_config.h"

static void test_varint(gpr_uint32 value, gpr_uint32 prefix_bits,
                        gpr_uint8 prefix_or, const char *expect_bytes,
                        size_t expect_length) {
  gpr_uint32 nbytes = GRPC_CHTTP2_VARINT_LENGTH(value, prefix_bits);
  gpr_slice expect = gpr_slice_from_copied_buffer(expect_bytes, expect_length);
  gpr_slice slice;
  gpr_log(GPR_DEBUG, "Test: 0x%08x", value);
  GPR_ASSERT(nbytes == expect_length);
  slice = gpr_slice_malloc(nbytes);
  GRPC_CHTTP2_WRITE_VARINT(value, prefix_bits, prefix_or,
                           GPR_SLICE_START_PTR(slice), nbytes);
  GPR_ASSERT(gpr_slice_cmp(expect, slice) == 0);
  gpr_slice_unref(expect);
  gpr_slice_unref(slice);
}

#define TEST_VARINT(value, prefix_bits, prefix_or, expect) \
  test_varint(value, prefix_bits, prefix_or, expect, sizeof(expect) - 1)

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  TEST_VARINT(0, 1, 0, "\x00");
  TEST_VARINT(128, 1, 0, "\x7f\x01");
  TEST_VARINT(16384, 1, 0, "\x7f\x81\x7f");
  TEST_VARINT(2097152, 1, 0, "\x7f\x81\xff\x7f");
  TEST_VARINT(268435456, 1, 0, "\x7f\x81\xff\xff\x7f");
  TEST_VARINT(0xffffffff, 1, 0, "\x7f\x80\xff\xff\xff\x0f");
  return 0;
}
