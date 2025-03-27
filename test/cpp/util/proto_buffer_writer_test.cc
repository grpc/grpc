//
// Copyright 2023 gRPC authors.
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

#include <grpcpp/support/byte_buffer.h>
#include <grpcpp/support/proto_buffer_writer.h>

#include "gtest/gtest.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace {

TEST(ProtoBufferWriterTest, Next) {
  ByteBuffer buffer;
  ProtoBufferWriter writer(&buffer, 16, 256);
  // 1st next
  void* data1;
  int size1;
  EXPECT_TRUE(writer.Next(&data1, &size1));
  EXPECT_GT(size1, 0);
  memset(data1, 1, size1);
  // 2nd next
  void* data2;
  int size2;
  EXPECT_TRUE(writer.Next(&data2, &size2));
  EXPECT_GT(size2, 0);
  memset(data2, 2, size2);
  // Done
  EXPECT_EQ(writer.ByteCount(), size1 + size2);
  EXPECT_EQ(buffer.Length(), size1 + size2);
  Slice slice;
  EXPECT_TRUE(buffer.DumpToSingleSlice(&slice).ok());
  EXPECT_EQ(memcmp(slice.begin(), data1, size1), 0);
  EXPECT_EQ(memcmp(slice.begin() + size1, data2, size2), 0);
}

#ifdef GRPC_PROTOBUF_CORD_SUPPORT_ENABLED

TEST(ProtoBufferWriterTest, WriteCord) {
  ByteBuffer buffer;
  ProtoBufferWriter writer(&buffer, 16, 4096);
  // Cord
  absl::Cord cord;
  std::string str1 = std::string(1024, 'a');
  cord.Append(str1);
  std::string str2 = std::string(1024, 'b');
  cord.Append(str2);
  writer.WriteCord(cord);
  // Done
  EXPECT_EQ(writer.ByteCount(), str1.size() + str2.size());
  EXPECT_EQ(buffer.Length(), str1.size() + str2.size());
  Slice slice;
  EXPECT_TRUE(buffer.DumpToSingleSlice(&slice).ok());
  EXPECT_EQ(memcmp(slice.begin() + str1.size(), str2.c_str(), str2.size()), 0);
}

#endif  // GRPC_PROTOBUF_CORD_SUPPORT_ENABLED

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
