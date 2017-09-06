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
