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

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  grpc_init();
  grpc_slice input = grpc_slice_from_copied_buffer((const char*)data, size);
  absl::optional<grpc_slice> output;
  output =
      grpc_core::PercentDecodeSlice(input, grpc_core::PercentEncodingType::URL);
  if (output.has_value()) {
    grpc_slice_unref(*output);
  }
  output = grpc_core::PercentDecodeSlice(
      input, grpc_core::PercentEncodingType::Compatible);
  if (output.has_value()) {
    grpc_slice_unref(*output);
  }
  grpc_slice_unref(grpc_core::PermissivePercentDecodeSlice(input));
  grpc_slice_unref(input);
  grpc_shutdown();
  return 0;
}
