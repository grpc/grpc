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

#ifndef GRPCXX_BYTE_BUFFER_H
#define GRPCXX_BYTE_BUFFER_H

#include <grpc/grpc.h>
#include <grpc/byte_buffer.h>
#include <grpc/support/log.h>
#include <grpc++/config.h>
#include <grpc++/slice.h>
#include <grpc++/status.h>
#include <grpc++/impl/serialization_traits.h>

#include <vector>

namespace grpc {

class ByteBuffer GRPC_FINAL {
 public:
  ByteBuffer() : buffer_(nullptr) {}

  ByteBuffer(const Slice* slices, size_t nslices);

  ~ByteBuffer() {
    if (buffer_) {
      grpc_byte_buffer_destroy(buffer_);
    }
  }

  void Dump(std::vector<Slice>* slices) const;

  void Clear();
  size_t Length() const;

 private:
  friend class SerializationTraits<ByteBuffer, void>;

  ByteBuffer(const ByteBuffer&);
  ByteBuffer& operator=(const ByteBuffer&);

  // takes ownership
  void set_buffer(grpc_byte_buffer* buf) {
    if (buffer_) {
      gpr_log(GPR_ERROR, "Overriding existing buffer");
      Clear();
    }
    buffer_ = buf;
  }

  grpc_byte_buffer* buffer() const { return buffer_; }

  grpc_byte_buffer* buffer_;
};

template <>
class SerializationTraits<ByteBuffer, void> {
 public:
  static Status Deserialize(grpc_byte_buffer* byte_buffer, ByteBuffer* dest,
                            int max_message_size) {
    dest->set_buffer(byte_buffer);
    return Status::OK;
  }
  static Status Serialize(const ByteBuffer& source, grpc_byte_buffer** buffer, 
                        bool* own_buffer) {
    *buffer = source.buffer();
    *own_buffer = false;
    return Status::OK;
  }
};

}  // namespace grpc

#endif  // GRPCXX_BYTE_BUFFER_H
