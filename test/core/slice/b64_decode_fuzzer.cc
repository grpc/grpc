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

#include <stdbool.h>
#include <stdint.h>

#include <grpc/grpc.h>

#include "src/core/lib/slice/b64.h"

bool squelch = true;
bool leak_check = true;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 1) return 0;
  grpc_init();
  const bool url_safe = static_cast<uint8_t>(0x100) < data[0];
  grpc_slice res = grpc_base64_decode_with_len(
      reinterpret_cast<const char*>(data + 1), size - 1, url_safe);
  grpc_slice_unref(res);
  grpc_shutdown();
  return 0;
}
