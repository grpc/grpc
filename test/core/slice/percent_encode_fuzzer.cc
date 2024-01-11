//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <stdint.h>
#include <string.h>

#include <utility>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/slice/slice.h"

bool squelch = true;
bool leak_check = true;

static void test(const uint8_t* data, size_t size,
                 grpc_core::PercentEncodingType type) {
  grpc_init();
  auto input = grpc_core::Slice::FromCopiedBuffer(
      reinterpret_cast<const char*>(data), size);
  auto output = grpc_core::PercentEncodeSlice(input.Ref(), type);
  auto permissive_decoded_output =
      grpc_core::PermissivePercentDecodeSlice(std::move(output));
  // decoded output must always match the input
  GPR_ASSERT(input == permissive_decoded_output);
  grpc_shutdown();
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  test(data, size, grpc_core::PercentEncodingType::URL);
  test(data, size, grpc_core::PercentEncodingType::Compatible);
  return 0;
}
