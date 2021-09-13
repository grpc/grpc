// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/hpack_utils.h"

#include "src/core/lib/surface/validate_metadata.h"

namespace grpc_core {

namespace {
size_t Base64EncodedSize(size_t raw_length) {
  static constexpr uint8_t tail_xtra[3] = {0, 2, 3};
  return raw_length / 3 * 4 + tail_xtra[raw_length % 3];
}
}  // namespace

// Return the size occupied by some metadata in the HPACK table.
size_t MetadataSizeInHPackTable(grpc_mdelem elem,
                                bool use_true_binary_metadata) {
  const uint8_t* key_buf = GRPC_SLICE_START_PTR(GRPC_MDKEY(elem));
  size_t key_len = GRPC_SLICE_LENGTH(GRPC_MDKEY(elem));
  size_t overhead_and_key = 32 + key_len;
  size_t value_len = GRPC_SLICE_LENGTH(GRPC_MDVALUE(elem));
  if (grpc_key_is_binary_header(key_buf, key_len)) {
    return overhead_and_key + (use_true_binary_metadata
                                   ? value_len + 1
                                   : Base64EncodedSize(value_len));
  } else {
    return overhead_and_key + value_len;
  }
}

}  // namespace grpc_core
