/*
 *
 * Copyright 2016, Google Inc.
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

#include "test/cpp/util/byte_buffer_proto_helper.h"

namespace grpc {
namespace testing {

bool ParseFromByteBuffer(ByteBuffer* buffer, grpc::protobuf::Message* message) {
  std::vector<Slice> slices;
  (void)buffer->Dump(&slices);
  grpc::string buf;
  buf.reserve(buffer->Length());
  for (auto s = slices.begin(); s != slices.end(); s++) {
    buf.append(reinterpret_cast<const char*>(s->begin()), s->size());
  }
  return message->ParseFromString(buf);
}

std::unique_ptr<ByteBuffer> SerializeToByteBuffer(
    grpc::protobuf::Message* message) {
  grpc::string buf;
  message->SerializeToString(&buf);
  grpc_slice s = grpc_slice_from_copied_string(buf.c_str());
  Slice slice(s, Slice::STEAL_REF);
  return std::unique_ptr<ByteBuffer>(new ByteBuffer(&slice, 1));
}

}  // namespace testing
}  // namespace grpc
