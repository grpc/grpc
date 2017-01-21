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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/slice/percent_encoding.h"
#include "test/core/util/memory_counters.h"

bool squelch = true;
bool leak_check = true;

static void test(const uint8_t *data, size_t size, const uint8_t *dict) {
  struct grpc_memory_counters counters;
  grpc_memory_counters_init();
  grpc_slice input = grpc_slice_from_copied_buffer((const char *)data, size);
  grpc_slice output = grpc_percent_encode_slice(input, dict);
  grpc_slice decoded_output;
  // encoder must always produce decodable output
  GPR_ASSERT(grpc_strict_percent_decode_slice(output, dict, &decoded_output));
  grpc_slice permissive_decoded_output =
      grpc_permissive_percent_decode_slice(output);
  // and decoded output must always match the input
  GPR_ASSERT(grpc_slice_eq(input, decoded_output));
  GPR_ASSERT(grpc_slice_eq(input, permissive_decoded_output));
  grpc_slice_unref(input);
  grpc_slice_unref(output);
  grpc_slice_unref(decoded_output);
  grpc_slice_unref(permissive_decoded_output);
  counters = grpc_memory_counters_snapshot();
  grpc_memory_counters_destroy();
  GPR_ASSERT(counters.total_size_relative == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  test(data, size, grpc_url_percent_encoding_unreserved_bytes);
  test(data, size, grpc_compatible_percent_encoding_unreserved_bytes);
  return 0;
}
