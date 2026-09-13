// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_SLICE_BYTE_SOURCE_H
#define GRPC_SRC_CORE_LIB_SLICE_BYTE_SOURCE_H

#include <grpc/slice.h>
#include <grpc/support/port_platform.h>
#include <string.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "src/core/lib/slice/slice.h"
#include "absl/types/span.h"

// Bounds-checked cursors for sequential wire-format reads and writes.
//
// ByteSource and ByteSink centralize bounds checking for byte-buffer
// operations. Operations either complete within the remaining span or fail
// without accessing memory outside the span.
//
// Methods are header-inline so the compiler can optimize the cursor
// operations on hot paths.

namespace grpc_core {

class ByteSource {
 public:
  ByteSource() = default;
  explicit ByteSource(absl::Span<const uint8_t> span) : span_(span) {}
  explicit ByteSource(const Slice& slice)
      : span_(slice.data(), slice.size()) {}
  explicit ByteSource(const grpc_slice& slice)
      : span_(GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice)) {}
  ByteSource(const uint8_t* data, size_t length) : span_(data, length) {}

  size_t remaining() const { return span_.size(); }
  bool empty() const { return span_.empty(); }
  const uint8_t* data() const { return span_.data(); }
  absl::Span<const uint8_t> span() const { return span_; }

  std::optional<uint8_t> Peek() const {
    if (span_.empty()) return std::nullopt;
    return span_[0];
  }

  std::optional<uint8_t> ReadU8() {
    if (span_.empty()) return std::nullopt;
    const uint8_t v = span_[0];
    span_ = span_.subspan(1);
    return v;
  }

  std::optional<uint16_t> ReadU16BE() {
    auto bytes = ReadSpan(2);
    if (!bytes.has_value()) return std::nullopt;
    return (static_cast<uint16_t>((*bytes)[0]) << 8) |
           static_cast<uint16_t>((*bytes)[1]);
  }

  std::optional<uint32_t> ReadU24BE() {
    auto bytes = ReadSpan(3);
    if (!bytes.has_value()) return std::nullopt;
    return (static_cast<uint32_t>((*bytes)[0]) << 16) |
           (static_cast<uint32_t>((*bytes)[1]) << 8) |
           static_cast<uint32_t>((*bytes)[2]);
  }

  std::optional<uint32_t> ReadU32BE() {
    auto bytes = ReadSpan(4);
    if (!bytes.has_value()) return std::nullopt;
    return (static_cast<uint32_t>((*bytes)[0]) << 24) |
           (static_cast<uint32_t>((*bytes)[1]) << 16) |
           (static_cast<uint32_t>((*bytes)[2]) << 8) |
           static_cast<uint32_t>((*bytes)[3]);
  }

  // HTTP/2 31-bit integers: the high bit is reserved and must be ignored.
  std::optional<uint32_t> ReadU31BE() {
    auto v = ReadU32BE();
    if (!v.has_value()) return std::nullopt;
    return *v & 0x7fffffffu;
  }

  std::optional<uint64_t> ReadU64BE() {
    auto bytes = ReadSpan(8);
    if (!bytes.has_value()) return std::nullopt;
    return (static_cast<uint64_t>((*bytes)[0]) << 56) |
           (static_cast<uint64_t>((*bytes)[1]) << 48) |
           (static_cast<uint64_t>((*bytes)[2]) << 40) |
           (static_cast<uint64_t>((*bytes)[3]) << 32) |
           (static_cast<uint64_t>((*bytes)[4]) << 24) |
           (static_cast<uint64_t>((*bytes)[5]) << 16) |
           (static_cast<uint64_t>((*bytes)[6]) << 8) |
           static_cast<uint64_t>((*bytes)[7]);
  }

  std::optional<absl::Span<const uint8_t>> ReadSpan(size_t n) {
    if (n > span_.size()) return std::nullopt;
    auto out = span_.subspan(0, n);
    span_ = span_.subspan(n);
    return out;
  }

  bool Skip(size_t n) {
    if (n > span_.size()) return false;
    span_ = span_.subspan(n);
    return true;
  }

  bool CopyTo(absl::Span<uint8_t> dst, size_t n) {
    if (n > dst.size()) return false;

    auto bytes = ReadSpan(n);
    if (!bytes.has_value()) return false;

    if (n != 0) {
      memcpy(dst.data(), bytes->data(), n);
    }
    return true;
  }

  // Consume as many of the remaining bytes as fit in `dst`, copying them
  // into `dst`. Returns the number of bytes copied. Never overruns `dst`
  // or the source span.
  size_t CopyAtMost(absl::Span<uint8_t> dst) {
    const size_t n = std::min(dst.size(), span_.size());

    if (n != 0) {
      memcpy(dst.data(), span_.data(), n);
    }

    span_ = span_.subspan(n);
    return n;
  }

 private:
  absl::Span<const uint8_t> span_;
};

class ByteSink {
 public:
  ByteSink() = default;
  explicit ByteSink(absl::Span<uint8_t> span) : span_(span), start_size_(span.size()) {}
  explicit ByteSink(MutableSlice& slice)
      : span_(slice.data(), slice.size()), start_size_(slice.size()) {}
  ByteSink(uint8_t* data, size_t length)
      : span_(data, length), start_size_(length) {}

  size_t remaining() const { return span_.size(); }
  size_t bytes_written() const { return start_size_ - span_.size(); }
  bool empty() const { return span_.empty(); }

  bool WriteU8(uint8_t v) {
    if (span_.empty()) return false;
    span_[0] = v;
    span_ = span_.subspan(1);
    return true;
  }

  bool WriteU16BE(uint16_t v) {
    if (span_.size() < 2) return false;
    span_[0] = static_cast<uint8_t>(v >> 8);
    span_[1] = static_cast<uint8_t>(v);
    span_ = span_.subspan(2);
    return true;
  }

  bool WriteU24BE(uint32_t v) {
    if (span_.size() < 3) return false;
    span_[0] = static_cast<uint8_t>(v >> 16);
    span_[1] = static_cast<uint8_t>(v >> 8);
    span_[2] = static_cast<uint8_t>(v);
    span_ = span_.subspan(3);
    return true;
  }

  bool WriteU31BE(uint32_t v) {
    return WriteU32BE(v & 0x7fffffffu);
  }

  bool WriteU32BE(uint32_t v) {
    if (span_.size() < 4) return false;
    span_[0] = static_cast<uint8_t>(v >> 24);
    span_[1] = static_cast<uint8_t>(v >> 16);
    span_[2] = static_cast<uint8_t>(v >> 8);
    span_[3] = static_cast<uint8_t>(v);
    span_ = span_.subspan(4);
    return true;
  }

  bool WriteU64BE(uint64_t v) {
    if (span_.size() < 8) return false;
    span_[0] = static_cast<uint8_t>(v >> 56);
    span_[1] = static_cast<uint8_t>(v >> 48);
    span_[2] = static_cast<uint8_t>(v >> 40);
    span_[3] = static_cast<uint8_t>(v >> 32);
    span_[4] = static_cast<uint8_t>(v >> 24);
    span_[5] = static_cast<uint8_t>(v >> 16);
    span_[6] = static_cast<uint8_t>(v >> 8);
    span_[7] = static_cast<uint8_t>(v);
    span_ = span_.subspan(8);
    return true;
  }

  bool WriteSpan(absl::Span<const uint8_t> src) {
    if (src.size() > span_.size()) return false;
    if (!src.empty()) memcpy(span_.data(), src.data(), src.size());
    span_ = span_.subspan(src.size());
    return true;
  }

  bool Skip(size_t n) {
    if (n > span_.size()) return false;
    span_ = span_.subspan(n);
    return true;
  }

 private:
  absl::Span<uint8_t> span_;
  size_t start_size_ = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SLICE_BYTE_SOURCE_H
