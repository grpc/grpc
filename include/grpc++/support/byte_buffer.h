/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPCXX_SUPPORT_BYTE_BUFFER_H
#define GRPCXX_SUPPORT_BYTE_BUFFER_H

#include <grpc++/impl/serialization_traits.h>
#include <grpc++/support/config.h>
#include <grpc++/support/slice.h>
#include <grpc++/support/status.h>
#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include <vector>

namespace grpc {

/// A sequence of bytes.
class ByteBuffer final {
 public:
  /// Constuct an empty buffer.
  ByteBuffer() : buffer_(nullptr) {}

  /// Construct buffer from \a slices, of which there are \a nslices.
  ByteBuffer(const Slice* slices, size_t nslices);

  /// Constuct a byte buffer by referencing elements of existing buffer
  /// \a buf. Wrapper of core function grpc_byte_buffer_copy
  ByteBuffer(const ByteBuffer& buf);

  ~ByteBuffer();

  ByteBuffer& operator=(const ByteBuffer&);

  /// Dump (read) the buffer contents into \a slices.
  Status Dump(std::vector<Slice>* slices) const;

  /// Remove all data.
  void Clear();

  /// Buffer size in bytes.
  size_t Length() const;

  /// Swap the state of *this and *other.
  void Swap(ByteBuffer* other);

 private:
  friend class SerializationTraits<ByteBuffer, void>;

  // takes ownership
  void set_buffer(grpc_byte_buffer* buf) {
    if (buffer_) {
      Clear();
    }
    buffer_ = buf;
  }

  // For \a SerializationTraits's usage.
  grpc_byte_buffer* buffer() const { return buffer_; }

  grpc_byte_buffer* buffer_;
};

template <>
class SerializationTraits<ByteBuffer, void> {
 public:
  static Status Deserialize(grpc_byte_buffer* byte_buffer, ByteBuffer* dest) {
    dest->set_buffer(byte_buffer);
    return Status::OK;
  }
  static Status Serialize(const ByteBuffer& source, grpc_byte_buffer** buffer,
                          bool* own_buffer) {
    *buffer = grpc_byte_buffer_copy(source.buffer());
    *own_buffer = true;
    return Status::OK;
  }
};

}  // namespace grpc

#endif  // GRPCXX_SUPPORT_BYTE_BUFFER_H
