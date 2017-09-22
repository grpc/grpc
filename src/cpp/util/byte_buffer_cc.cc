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

#include <grpc++/impl/grpc_library.h>
#include <grpc++/support/byte_buffer.h>
#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>

namespace grpc {

static internal::GrpcLibraryInitializer g_gli_initializer;

ByteBuffer::ByteBuffer(const Slice* slices, size_t nslices) {
  // The following assertions check that the representation of a grpc::Slice is
  // identical to that of a grpc_slice:  it has a grpc_slice field, and nothing
  // else.
  static_assert(std::is_same<decltype(slices[0].slice_), grpc_slice>::value,
                "Slice must have same representation as grpc_slice");
  static_assert(sizeof(Slice) == sizeof(grpc_slice),
                "Slice must have same representation as grpc_slice");
  // The following assertions check that the representation of a ByteBuffer is
  // identical to grpc_byte_buffer*:  it has a grpc_byte_buffer* field,
  // and nothing else.
  static_assert(std::is_same<decltype(buffer_), grpc_byte_buffer*>::value,
                "ByteBuffer must have same representation as "
                "grpc_byte_buffer*");
  static_assert(sizeof(ByteBuffer) == sizeof(grpc_byte_buffer*),
                "ByteBuffer must have same representation as "
                "grpc_byte_buffer*");
  g_gli_initializer.summon();  // Make sure that initializer linked in
  // The const_cast is legal if grpc_raw_byte_buffer_create() does no more
  // than its advertised side effect of increasing the reference count of the
  // slices it processes, and such an increase does not affect the semantics
  // seen by the caller of this constructor.
  buffer_ = grpc_raw_byte_buffer_create(
      reinterpret_cast<grpc_slice*>(const_cast<Slice*>(slices)), nslices);
}

Status ByteBuffer::Dump(std::vector<Slice>* slices) const {
  slices->clear();
  if (!buffer_) {
    return Status(StatusCode::FAILED_PRECONDITION, "Buffer not initialized");
  }
  grpc_byte_buffer_reader reader;
  if (!grpc_byte_buffer_reader_init(&reader, buffer_)) {
    return Status(StatusCode::INTERNAL,
                  "Couldn't initialize byte buffer reader");
  }
  grpc_slice s;
  while (grpc_byte_buffer_reader_next(&reader, &s)) {
    slices->push_back(Slice(s, Slice::STEAL_REF));
  }
  grpc_byte_buffer_reader_destroy(&reader);
  return Status::OK;
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
  if (this != &buf) {
    Clear();  // first remove existing data
  }
  if (buf.buffer_) {
    buffer_ = grpc_byte_buffer_copy(buf.buffer_);  // then copy
  }
  return *this;
}

void ByteBuffer::Swap(ByteBuffer* other) {
  grpc_byte_buffer* tmp = other->buffer_;
  other->buffer_ = buffer_;
  buffer_ = tmp;
}

}  // namespace grpc
