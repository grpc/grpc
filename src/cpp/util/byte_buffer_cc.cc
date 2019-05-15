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

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/support/byte_buffer.h>

namespace grpc {

static internal::GrpcLibraryInitializer g_gli_initializer;

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

}  // namespace grpc
