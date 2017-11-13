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

#include "third_party/protobuf/src/google/protobuf/util/message_differencer.h"

namespace grpc {
namespace testing {

static internal::GrpcLibraryInitializer g_gli_initializer;

using google::protobuf::util::MessageDifferencer;

class SerializationTraitsTest : public ::testing::Test {
 public:
  template <class Message>
  void SerializeAndDeserialize(Message& m1, Message& m2) {
    grpc_byte_buffer* bb;
    bool owns_buffer = false;
    ASSERT_TRUE(
        SerializationTraits<Message>::Serialize(m1, &bb, &owns_buffer).ok());
    ASSERT_TRUE(SerializationTraits<Message>::Deserialize(bb, &m2).ok());
  }
};

TEST_F(SerializationTraitsTest, TestEmpty) {
  SimpleRequest request;
  SimpleRequest request2;
  SerializeAndDeserialize(request, request2);
  EXPECT_TRUE(MessageDifferencer::Equals(request, request2));
}

TEST_F(SerializationTraitsTest, TestPartiallyFilled1) {
  SimpleRequest request;
  std::string payload(1024, 'a');
  request.mutable_payload()->set_body(payload);
  SimpleRequest request2;
  SerializeAndDeserialize(request, request2);
  EXPECT_TRUE(MessageDifferencer::Equals(request, request2));
}

TEST_F(SerializationTraitsTest, TestPartiallyFilled2) {
  SimpleRequest request;
  std::string payload(1024, 'a');
  request.mutable_payload()->set_body(payload);
  request.mutable_payload()->set_type(COMPRESSABLE);
  request.set_fill_username(true);
  request.mutable_response_compressed()->set_value(false);
  request.mutable_response_status()->set_code(1234);
  request.mutable_response_status()->set_message("Cheerios!");
  SimpleRequest request2;
  SerializeAndDeserialize(request, request2);
  EXPECT_TRUE(MessageDifferencer::Equals(request, request2));
}

TEST_F(SerializationTraitsTest, TestSmallResponse) {
  SimpleResponse response;
  std::string payload(1024, 'a');
  response.mutable_payload()->set_body(payload);
  SimpleResponse response2;
  SerializeAndDeserialize(response, response2);
  EXPECT_TRUE(MessageDifferencer::Equals(response, response2));
}

TEST_F(SerializationTraitsTest, TestBigResponse) {
  SimpleResponse response;
  std::string payload(1024 * 1024 * 1024, 'a');
  response.mutable_payload()->set_body(payload);
  SimpleResponse response2;
  SerializeAndDeserialize(response, response2);
  EXPECT_TRUE(MessageDifferencer::Equals(response, response2));
}

TEST_F(SerializationTraitsTest, TestRepeated) {
  StreamingOutputCallRequest request;
  request.add_response_parameters()->set_size(1);
  request.add_response_parameters()->set_size(2);
  request.add_response_parameters()->set_size(100);
  request.add_response_parameters()->set_size(1000);
  StreamingOutputCallRequest request2;
  SerializeAndDeserialize(request, request2);
  EXPECT_TRUE(MessageDifferencer::Equals(request, request2));
}

TEST_F(SerializationTraitsTest, TestNonEmptySecond) {
  SimpleRequest request;
  std::string payload(1024, 'a');
  request.mutable_payload()->set_body(payload);
  request.mutable_response_compressed()->set_value(true);
  request.mutable_response_status()->set_code(1234);
  request.mutable_response_status()->set_message("Cheerios!");
  SimpleRequest request2;
  std::string payload2(10, 'x');
  request2.mutable_payload()->set_body(payload2);
  request2.mutable_payload()->set_type(COMPRESSABLE);
  request2.set_fill_username(true);
  request2.mutable_response_compressed()->set_value(false);
  request2.mutable_response_status()->set_message("Lucky Charms");
  SerializeAndDeserialize(request, request2);
  EXPECT_TRUE(MessageDifferencer::Equals(request, request2));
}

TEST_F(SerializationTraitsTest, TestTypeMismatch) {
  SimpleRequest request;
  SimpleResponse response;
  grpc_byte_buffer* bb;
  bool owns_buffer = false;
  ASSERT_TRUE(
      SerializationTraits<SimpleRequest>::Serialize(request, &bb, &owns_buffer)
          .ok());
  ASSERT_TRUE(
      SerializationTraits<SimpleResponse>::Deserialize(bb, &response).ok());
  EXPECT_FALSE(MessageDifferencer::Equals(request, response));
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
