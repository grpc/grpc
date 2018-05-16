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

#include <grpc++/support/byte_buffer.h>
#include <grpcpp/impl/grpc_library.h>

#include <cstring>
#include <vector>

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpcpp/support/slice.h>
#include <gtest/gtest.h>

namespace grpc {

static internal::GrpcLibraryInitializer g_gli_initializer;

namespace {

const char* kContent1 = "hello xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
const char* kContent2 = "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy world";

class ByteBufferTest : public ::testing::Test {};

TEST_F(ByteBufferTest, CreateFromSingleSlice) {
  Slice s(kContent1);
  ByteBuffer buffer(&s, 1);
  EXPECT_EQ(strlen(kContent1), buffer.Length());
}

TEST_F(ByteBufferTest, CreateFromVector) {
  std::vector<Slice> slices;
  slices.emplace_back(kContent1);
  slices.emplace_back(kContent2);
  ByteBuffer buffer(&slices[0], 2);
  EXPECT_EQ(strlen(kContent1) + strlen(kContent2), buffer.Length());
}

TEST_F(ByteBufferTest, Clear) {
  Slice s(kContent1);
  ByteBuffer buffer(&s, 1);
  buffer.Clear();
  EXPECT_EQ(static_cast<size_t>(0), buffer.Length());
}

TEST_F(ByteBufferTest, Length) {
  std::vector<Slice> slices;
  slices.emplace_back(kContent1);
  slices.emplace_back(kContent2);
  ByteBuffer buffer(&slices[0], 2);
  EXPECT_EQ(strlen(kContent1) + strlen(kContent2), buffer.Length());
}

bool SliceEqual(const Slice& a, grpc_slice b) {
  if (a.size() != GRPC_SLICE_LENGTH(b)) {
    return false;
  }
  for (size_t i = 0; i < a.size(); i++) {
    if (a.begin()[i] != GRPC_SLICE_START_PTR(b)[i]) {
      return false;
    }
  }
  return true;
}

TEST_F(ByteBufferTest, Dump) {
  grpc_slice hello = grpc_slice_from_copied_string(kContent1);
  grpc_slice world = grpc_slice_from_copied_string(kContent2);
  std::vector<Slice> slices;
  slices.push_back(Slice(hello, Slice::STEAL_REF));
  slices.push_back(Slice(world, Slice::STEAL_REF));
  ByteBuffer buffer(&slices[0], 2);
  slices.clear();
  (void)buffer.Dump(&slices);
  EXPECT_TRUE(SliceEqual(slices[0], hello));
  EXPECT_TRUE(SliceEqual(slices[1], world));
}

TEST_F(ByteBufferTest, SerializationMakesCopy) {
  grpc_slice hello = grpc_slice_from_copied_string(kContent1);
  grpc_slice world = grpc_slice_from_copied_string(kContent2);
  std::vector<Slice> slices;
  slices.push_back(Slice(hello, Slice::STEAL_REF));
  slices.push_back(Slice(world, Slice::STEAL_REF));
  ByteBuffer send_buffer;
  bool owned = false;
  ByteBuffer buffer(&slices[0], 2);
  slices.clear();
  auto status = SerializationTraits<ByteBuffer, void>::Serialize(
      buffer, &send_buffer, &owned);
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(owned);
  EXPECT_TRUE(send_buffer.Valid());
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
