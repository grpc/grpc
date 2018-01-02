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

namespace varint_impl {
uint32_t ComputeLengthWithTail(uint32_t tail_value);
void WriteTail(uint32_t tail_value, uint8_t* target, uint32_t tail_length);
}  // namespace varint_impl

template <uint32_t kPrefixBits>
class VarIntEncoder {
 public:
  VarIntEncoder(uint32_t value, uint32_t prefix_or)
      : value_(value), length_(ComputeLength(value)), prefix_or_(prefix_or) {}

  uint32_t length() const { return length_; }

  void Write(uint8_t* tgt) const {
    if (length_ == 1u) {
      (tgt)[0] = static_cast<uint8_t>(prefix_or_ | value_);
    } else {
      (tgt)[0] = prefix_or_ | kMaxInPrefix;
      varint_impl::WriteTail(value_ - kMaxInPrefix, (tgt) + 1, length_ - 1);
    }
  }

 private:
  static constexpr uint32_t kMaxInPrefix = (1 << (8 - kPrefixBits)) - 1;

  static uint32_t ComputeLength(uint32_t value) {
    if (value < kMaxInPrefix) return 1;
    return varint_impl::ComputeLengthWithTail(value - kMaxInPrefix);
  }

  const uint32_t value_;
  const uint32_t length_;
  const uint32_t prefix_or_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_VARINT_H */
