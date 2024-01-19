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

#include <gtest/gtest.h>

#include <grpcpp/support/byte_buffer.h>
#include <grpcpp/support/proto_buffer_reader.h>

#include "test/core/util/test_config.h"

namespace grpc {
namespace {

void ExpectBufferEqual(const ByteBuffer& a, const ByteBuffer& b) {
  Slice a_slice;
  EXPECT_TRUE(a.DumpToSingleSlice(&a_slice).ok());
  Slice b_slice;
  EXPECT_TRUE(b.DumpToSingleSlice(&b_slice).ok());
  EXPECT_EQ(a_slice.size(), b_slice.size());
  EXPECT_EQ(memcmp(a_slice.begin(), b_slice.begin(), a_slice.size()), 0);
}

TEST(ProtoBufferReaderTest, Next) {
  Slice slices[] = {
      Slice(std::string(128, 'a')),
      Slice(std::string(256, 'b')),
  };
  ByteBuffer buffer(slices, 2);
  ProtoBufferReader reader(&buffer);
  // read all data from buffer
  std::vector<Slice> read_slices;
  int read_size = 0;
  while (read_size < static_cast<int>(buffer.Length())) {
    const void* data;
    int size = 0;
    EXPECT_TRUE(reader.Next(&data, &size));
    read_slices.emplace_back(data, size);
    read_size += size;
  }
  EXPECT_EQ(reader.ByteCount(), read_size);
  // check if read data is equal to original data
  ByteBuffer read_buffer(&*read_slices.begin(), read_slices.size());
  ExpectBufferEqual(read_buffer, buffer);
}

#ifdef GRPC_PROTOBUF_CORD_SUPPORT_ENABLED

TEST(ProtoBufferReaderTest, ReadCord) {
  std::string str1 = std::string(128, 'a');
  std::string str2 = std::string(256, 'b');
  Slice slices[] = {Slice(str1), Slice(str2)};
  ByteBuffer buffer(slices, 2);
  ProtoBufferReader reader(&buffer);
  // read cords from buffer
  absl::Cord cord1;
  reader.ReadCord(&cord1, str1.size());
  EXPECT_EQ(cord1.size(), str1.size());
  EXPECT_EQ(std::string(cord1), str1);
  absl::Cord cord2;
  reader.ReadCord(&cord2, str2.size());
  EXPECT_EQ(cord2.size(), str2.size());
  EXPECT_EQ(std::string(cord2), str2);
  EXPECT_EQ(reader.ByteCount(), cord1.size() + cord2.size());
}

#endif  // GRPC_PROTOBUF_CORD_SUPPORT_ENABLED

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
