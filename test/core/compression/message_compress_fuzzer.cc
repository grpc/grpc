/*
 *
 * Copyright 2019 gRPC authors.
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

#include <grpc/grpc.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/util/memory_counters.h"

bool squelch = true;
bool leak_check = true;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 1) return 0;

  // Instead of rolling something complicated to convert a uint8_t to the enum,
  // just bail out if it isn't trivially convertible.
  if (data[0] >= GRPC_MESSAGE_COMPRESS_ALGORITHMS_COUNT) return 0;
  const auto compression_algorithm =
      static_cast<grpc_message_compression_algorithm>(data[0]);

  grpc_core::testing::LeakDetector leak_detector(true);
  grpc_init();
  grpc_slice_buffer input_buffer;
  grpc_slice_buffer_init(&input_buffer);
  grpc_slice_buffer_add(&input_buffer,
                        grpc_slice_from_copied_buffer(
                            reinterpret_cast<const char*>(data + 1), size - 1));
  grpc_slice_buffer output_buffer;
  grpc_slice_buffer_init(&output_buffer);

  grpc_msg_compress(compression_algorithm, &input_buffer, &output_buffer);

  grpc_slice_buffer_destroy(&input_buffer);
  grpc_slice_buffer_destroy(&output_buffer);
  grpc_shutdown();
  return 0;
}
