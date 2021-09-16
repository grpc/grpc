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

bool squelch = true;
bool leak_check = true;

static void test(const uint8_t* data, size_t size,
                 grpc_core::PercentEncodingType type) {
  grpc_init();
  grpc_slice input =
      grpc_slice_from_copied_buffer(reinterpret_cast<const char*>(data), size);
  grpc_slice output = grpc_core::PercentEncodeSlice(input, type);
  absl::optional<grpc_slice> decoded_output =
      grpc_core::PercentDecodeSlice(output, type);
  // encoder must always produce decodable output
  GPR_ASSERT(decoded_output.has_value());
  grpc_slice permissive_decoded_output =
      grpc_core::PermissivePercentDecodeSlice(output);
  // and decoded output must always match the input
  GPR_ASSERT(grpc_slice_eq(input, *decoded_output));
  GPR_ASSERT(grpc_slice_eq(input, permissive_decoded_output));
  grpc_slice_unref(input);
  grpc_slice_unref(output);
  grpc_slice_unref(*decoded_output);
  grpc_slice_unref(permissive_decoded_output);
  grpc_shutdown();
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  test(data, size, grpc_core::PercentEncodingType::URL);
  test(data, size, grpc_core::PercentEncodingType::Compatible);
  return 0;
}
