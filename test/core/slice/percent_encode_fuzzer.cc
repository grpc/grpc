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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/slice/percent_encoding.h"
#include "test/core/util/memory_counters.h"

bool squelch = true;
bool leak_check = true;

static void test(const uint8_t* data, size_t size, const uint8_t* dict) {
  struct grpc_memory_counters counters;
  grpc_init();
  grpc_memory_counters_init();
  grpc_slice input =
      grpc_slice_from_copied_buffer(reinterpret_cast<const char*>(data), size);
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
  grpc_shutdown();
  GPR_ASSERT(counters.total_size_relative == 0);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  test(data, size, grpc_url_percent_encoding_unreserved_bytes);
  test(data, size, grpc_compatible_percent_encoding_unreserved_bytes);
  return 0;
}
