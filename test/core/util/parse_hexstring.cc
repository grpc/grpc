//
//
// Copyright 2015 gRPC authors.
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

#include "test/core/util/parse_hexstring.h"

#include <stddef.h>
#include <stdint.h>

#include <grpc/slice.h>
#include <grpc/support/log.h>

namespace grpc_core {
Slice ParseHexstring(absl::string_view hexstring) {
  size_t nibbles = 0;
  uint8_t* out;
  uint8_t temp;
  grpc_slice slice;

  for (auto c : hexstring) {
    nibbles += (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  }

  GPR_ASSERT((nibbles & 1) == 0);

  slice = grpc_slice_malloc(nibbles / 2);
  out = GRPC_SLICE_START_PTR(slice);

  nibbles = 0;
  temp = 0;
  for (auto c : hexstring) {
    if (c >= '0' && c <= '9') {
      temp = static_cast<uint8_t>(temp << 4) | static_cast<uint8_t>(c - '0');
      nibbles++;
    } else if (c >= 'a' && c <= 'f') {
      temp =
          static_cast<uint8_t>(temp << 4) | static_cast<uint8_t>(c - 'a' + 10);
      nibbles++;
    }
    if (nibbles == 2) {
      *out++ = temp;
      nibbles = 0;
    }
  }

  return Slice(slice);
}
}  // namespace grpc_core
