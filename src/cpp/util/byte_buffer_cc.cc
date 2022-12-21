//
//
// Copyright 2015 gRPC authors.
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
//
//

#include <vector>

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/impl/compression_types.h>
#include <grpc/slice.h>
#include <grpcpp/support/byte_buffer.h>
#include <grpcpp/support/slice.h>
#include <grpcpp/support/status.h>

namespace grpc {

Status ByteBuffer::TrySingleSlice(Slice* slice) const {
  if (!buffer_) {
    return Status(StatusCode::FAILED_PRECONDITION, "Buffer not initialized");
  }
  if ((buffer_->type == GRPC_BB_RAW) &&
      (buffer_->data.raw.compression == GRPC_COMPRESS_NONE) &&
      (buffer_->data.raw.slice_buffer.count == 1)) {
    grpc_slice internal_slice = buffer_->data.raw.slice_buffer.slices[0];
    *slice = Slice(internal_slice, Slice::ADD_REF);
    return Status::OK;
  } else {
    return Status(StatusCode::FAILED_PRECONDITION,
                  "Buffer isn't made up of a single uncompressed slice.");
  }
}

Status ByteBuffer::DumpToSingleSlice(Slice* slice) const {
  if (!buffer_) {
    return Status(StatusCode::FAILED_PRECONDITION, "Buffer not initialized");
  }
  grpc_byte_buffer_reader reader;
  if (!grpc_byte_buffer_reader_init(&reader, buffer_)) {
    return Status(StatusCode::INTERNAL,
                  "Couldn't initialize byte buffer reader");
  }
  grpc_slice s = grpc_byte_buffer_reader_readall(&reader);
  *slice = Slice(s, Slice::STEAL_REF);
  grpc_byte_buffer_reader_destroy(&reader);
  return Status::OK;
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

}  // namespace grpc
