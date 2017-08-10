/*
 *
 * Copyright 2016 gRPC authors.
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
  Slice slice(buf);
  return std::unique_ptr<ByteBuffer>(new ByteBuffer(&slice, 1));
}

}  // namespace testing
}  // namespace grpc
