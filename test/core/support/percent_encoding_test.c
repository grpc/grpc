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

#include "src/core/lib/support/percent_encoding.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/support/string.h"
#include "test/core/util/test_config.h"

#define TEST_VECTOR(raw, encoded) \
  test_vector(raw, sizeof(raw) - 1, encoded, sizeof(encoded) - 1)

static void test_vector(const char *raw, size_t raw_length, const char *encoded,
                        size_t encoded_length) {
  char *raw_msg = gpr_dump(raw, raw_length, GPR_DUMP_HEX | GPR_DUMP_ASCII);
  char *encoded_msg =
      gpr_dump(encoded, encoded_length, GPR_DUMP_HEX | GPR_DUMP_ASCII);
  gpr_log(GPR_DEBUG, "Trial:\nraw = %s\nencoded = %s", raw_msg, encoded_msg);
  gpr_free(raw_msg);
  gpr_free(encoded_msg);

  gpr_slice raw_slice = gpr_slice_from_copied_buffer(raw, raw_length);
  gpr_slice encoded_slice =
      gpr_slice_from_copied_buffer(encoded, encoded_length);
  gpr_slice raw2encoded_slice = gpr_percent_encode_slice(raw_slice);
  gpr_slice encoded2raw_slice;
  GPR_ASSERT(gpr_percent_decode_slice(encoded_slice, &encoded2raw_slice));

  char *raw2encoded_msg =
      gpr_dump_slice(raw2encoded_slice, GPR_DUMP_HEX | GPR_DUMP_ASCII);
  char *encoded2raw_msg =
      gpr_dump_slice(encoded2raw_slice, GPR_DUMP_HEX | GPR_DUMP_ASCII);
  gpr_log(GPR_DEBUG, "Result:\nraw2encoded = %s\nencoded2raw = %s",
          raw2encoded_msg, encoded2raw_msg);
  gpr_free(raw2encoded_msg);
  gpr_free(encoded2raw_msg);

  GPR_ASSERT(0 == gpr_slice_cmp(raw_slice, encoded2raw_slice));
  GPR_ASSERT(0 == gpr_slice_cmp(encoded_slice, raw2encoded_slice));

  gpr_slice_unref(encoded2raw_slice);
  gpr_slice_unref(raw2encoded_slice);
  gpr_slice_unref(raw_slice);
  gpr_slice_unref(encoded_slice);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  TEST_VECTOR(
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.~",
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.~");
  TEST_VECTOR("\x00", "%00");
  TEST_VECTOR("\x01", "%01");
  TEST_VECTOR("a b", "a%20b");
  TEST_VECTOR(" b", "%20b");
  TEST_VECTOR("\x0f", "%0F");
  TEST_VECTOR("\xff", "%FF");
  TEST_VECTOR("\xee", "%EE");
  return 0;
}
