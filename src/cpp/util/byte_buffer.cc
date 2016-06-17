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

#include <grpc++/support/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>

namespace grpc {

ByteBuffer::ByteBuffer(const Slice* slices, size_t nslices) {
  // TODO(yangg) maybe expose some core API to simplify this
  std::vector<gpr_slice> c_slices(nslices);
  for (size_t i = 0; i < nslices; i++) {
    c_slices[i] = slices[i].slice_;
  }
  buffer_ = grpc_raw_byte_buffer_create(c_slices.data(), nslices);
}

ByteBuffer::~ByteBuffer() {
  if (buffer_) {
    grpc_byte_buffer_destroy(buffer_);
  }
}

void ByteBuffer::Clear() {
  if (buffer_) {
    grpc_byte_buffer_destroy(buffer_);
    buffer_ = nullptr;
  }
}

void ByteBuffer::Dump(std::vector<Slice>* slices) const {
  slices->clear();
  if (!buffer_) {
    return;
  }
  grpc_byte_buffer_reader reader;
  grpc_byte_buffer_reader_init(&reader, buffer_);
  gpr_slice s;
  while (grpc_byte_buffer_reader_next(&reader, &s)) {
    slices->push_back(Slice(s, Slice::STEAL_REF));
  }
  grpc_byte_buffer_reader_destroy(&reader);
}

size_t ByteBuffer::Length() const {
  if (buffer_) {
    return grpc_byte_buffer_length(buffer_);
  } else {
    return 0;
  }
}

ByteBuffer::ByteBuffer(const ByteBuffer& buf)
    : buffer_(grpc_byte_buffer_copy(buf.buffer_)) {}

ByteBuffer& ByteBuffer::operator=(const ByteBuffer& buf) {
  Clear();  // first remove existing data
  if (buf.buffer_) {
    buffer_ = grpc_byte_buffer_copy(buf.buffer_);  // then copy
  }
  return *this;
}

}  // namespace grpc
