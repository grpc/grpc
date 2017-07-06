/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc++/impl/codegen/proto_utils.h>
#include <grpc++/impl/grpc_library.h>
#include <gtest/gtest.h>

#include "src/proto/grpc/testing/messages.pb.h"

namespace grpc {
namespace testing {

static internal::GrpcLibraryInitializer g_gli_initializer;


class SerializationTraitsTest : public ::testing::Test {};

TEST_F(SerializationTraitsTest, Foo) {
  SimpleRequest request;
  const int size = 1024;
  request.set_response_size(size);
  grpc::string payload(size, 'a');
  request.mutable_payload()->set_body(payload.c_str(), size);
  grpc_byte_buffer* bb;
  bool owns_buffer = false;
  SerializationTraits<SimpleRequest>::Serialize(request, &bb, &owns_buffer);
  SimpleRequest request2;
  SerializationTraits<SimpleRequest>::Deserialize(bb, &request2);
  EXPECT_EQ(request.payload().body(), request2.payload().body());
 }

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
