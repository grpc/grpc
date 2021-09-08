/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_VARINT_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_VARINT_H

#include <grpc/support/port_platform.h>

/* Helpers for hpack varint encoding */

namespace grpc_core {

/* maximum value that can be bitpacked with the opcode if the opcode has a
   prefix of length prefix_bits */
constexpr uint32_t MaxInVarintPrefix(uint8_t prefix_bits) {
  return (1 << (8 - prefix_bits)) - 1;
}

/* length of a value that needs varint tail encoding (it's bigger than can be
   bitpacked into the opcode byte) - returned value includes the length of the
   opcode byte */
uint32_t VarintLength(uint32_t tail_value);
void VarintWriteTail(uint32_t tail_value, uint8_t* target,
                     uint32_t tail_length);

template <uint8_t kPrefixBits>
class VarintWriter {
 public:
  static constexpr uint32_t kMaxInPrefix = MaxInVarintPrefix(kPrefixBits);

  explicit VarintWriter(uint32_t value)
      : value_(value),
        length_(value < kMaxInPrefix ? 1 : VarintLength(value - kMaxInPrefix)) {
  }

  uint32_t value() const { return value_; }
  uint32_t length() const { return length_; }

  void Write(uint8_t prefix, uint8_t* target) const {
    if (length_ == 1) {
      target[0] = prefix | value_;
    } else {
      target[0] = prefix | kMaxInPrefix;
      VarintWriteTail(value_ - kMaxInPrefix, target + 1, length_ - 1);
    }
  }

 private:
  const uint32_t value_;
  // length required to bitpack value_
  const uint32_t length_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_VARINT_H */
