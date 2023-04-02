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

#include <string.h>

#include "gtest/gtest.h"

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", x)

TEST(GrpcByteBufferReaderTest, TestReadOneSlice) {
  grpc_slice slice;
  grpc_byte_buffer* buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice first_slice, second_slice;
  int first_code, second_code;

  LOG_TEST("test_read_one_slice");
  slice = grpc_slice_from_copied_string("test");
  buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  ASSERT_TRUE(grpc_byte_buffer_reader_init(&reader, buffer) &&
              "Couldn't init byte buffer reader");
  first_code = grpc_byte_buffer_reader_next(&reader, &first_slice);
  ASSERT_NE(first_code, 0);
  ASSERT_EQ(memcmp(GRPC_SLICE_START_PTR(first_slice), "test", 4), 0);
  grpc_slice_unref(first_slice);
  second_code = grpc_byte_buffer_reader_next(&reader, &second_slice);
  ASSERT_EQ(second_code, 0);
  grpc_byte_buffer_destroy(buffer);
}

TEST(GrpcByteBufferReaderTest, TestReadOneSliceMalloc) {
  grpc_slice slice;
  grpc_byte_buffer* buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice first_slice, second_slice;
  int first_code, second_code;

  LOG_TEST("test_read_one_slice_malloc");
  slice = grpc_slice_malloc(4);
  memcpy(GRPC_SLICE_START_PTR(slice), "test", 4);
  buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  ASSERT_TRUE(grpc_byte_buffer_reader_init(&reader, buffer) &&
              "Couldn't init byte buffer reader");
  first_code = grpc_byte_buffer_reader_next(&reader, &first_slice);
  ASSERT_NE(first_code, 0);
  ASSERT_EQ(memcmp(GRPC_SLICE_START_PTR(first_slice), "test", 4), 0);
  grpc_slice_unref(first_slice);
  second_code = grpc_byte_buffer_reader_next(&reader, &second_slice);
  ASSERT_EQ(second_code, 0);
  grpc_byte_buffer_destroy(buffer);
}

TEST(GrpcByteBufferReaderTest, TestReadNoneCompressedSlice) {
  grpc_slice slice;
  grpc_byte_buffer* buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice first_slice, second_slice;
  int first_code, second_code;

  LOG_TEST("test_read_none_compressed_slice");
  slice = grpc_slice_from_copied_string("test");
  buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  ASSERT_TRUE(grpc_byte_buffer_reader_init(&reader, buffer) &&
              "Couldn't init byte buffer reader");
  first_code = grpc_byte_buffer_reader_next(&reader, &first_slice);
  ASSERT_NE(first_code, 0);
  ASSERT_EQ(memcmp(GRPC_SLICE_START_PTR(first_slice), "test", 4), 0);
  grpc_slice_unref(first_slice);
  second_code = grpc_byte_buffer_reader_next(&reader, &second_slice);
  ASSERT_EQ(second_code, 0);
  grpc_byte_buffer_destroy(buffer);
}

TEST(GrpcByteBufferReaderTest, TestPeekOneSlice) {
  grpc_slice slice;
  grpc_byte_buffer* buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice* first_slice;
  grpc_slice* second_slice;
  int first_code, second_code;

  LOG_TEST("test_peek_one_slice");
  slice = grpc_slice_from_copied_string("test");
  buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  ASSERT_TRUE(grpc_byte_buffer_reader_init(&reader, buffer) &&
              "Couldn't init byte buffer reader");
  first_code = grpc_byte_buffer_reader_peek(&reader, &first_slice);
  ASSERT_NE(first_code, 0);
  ASSERT_EQ(memcmp(GRPC_SLICE_START_PTR(*first_slice), "test", 4), 0);
  second_code = grpc_byte_buffer_reader_peek(&reader, &second_slice);
  ASSERT_EQ(second_code, 0);
  grpc_byte_buffer_destroy(buffer);
}

TEST(GrpcByteBufferReaderTest, TestPeekOneSliceMalloc) {
  grpc_slice slice;
  grpc_byte_buffer* buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice* first_slice;
  grpc_slice* second_slice;
  int first_code, second_code;

  LOG_TEST("test_peek_one_slice_malloc");
  slice = grpc_slice_malloc(4);
  memcpy(GRPC_SLICE_START_PTR(slice), "test", 4);
  buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  ASSERT_TRUE(grpc_byte_buffer_reader_init(&reader, buffer) &&
              "Couldn't init byte buffer reader");
  first_code = grpc_byte_buffer_reader_peek(&reader, &first_slice);
  ASSERT_NE(first_code, 0);
  ASSERT_EQ(memcmp(GRPC_SLICE_START_PTR(*first_slice), "test", 4), 0);
  second_code = grpc_byte_buffer_reader_peek(&reader, &second_slice);
  ASSERT_EQ(second_code, 0);
  grpc_byte_buffer_destroy(buffer);
}

TEST(GrpcByteBufferReaderTest, TestPeekNoneCompressedSlice) {
  grpc_slice slice;
  grpc_byte_buffer* buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice* first_slice;
  grpc_slice* second_slice;
  int first_code, second_code;

  LOG_TEST("test_peek_none_compressed_slice");
  slice = grpc_slice_from_copied_string("test");
  buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  ASSERT_TRUE(grpc_byte_buffer_reader_init(&reader, buffer) &&
              "Couldn't init byte buffer reader");
  first_code = grpc_byte_buffer_reader_peek(&reader, &first_slice);
  ASSERT_NE(first_code, 0);
  ASSERT_EQ(memcmp(GRPC_SLICE_START_PTR(*first_slice), "test", 4), 0);
  second_code = grpc_byte_buffer_reader_peek(&reader, &second_slice);
  ASSERT_EQ(second_code, 0);
  grpc_byte_buffer_destroy(buffer);
}

TEST(GrpcByteBufferReaderTest, TestByteBufferFromReader) {
  grpc_slice slice;
  grpc_byte_buffer *buffer, *buffer_from_reader;
  grpc_byte_buffer_reader reader;

  LOG_TEST("test_byte_buffer_from_reader");
  slice = grpc_slice_malloc(4);
  memcpy(GRPC_SLICE_START_PTR(slice), "test", 4);
  buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  ASSERT_TRUE(grpc_byte_buffer_reader_init(&reader, buffer) &&
              "Couldn't init byte buffer reader");

  buffer_from_reader = grpc_raw_byte_buffer_from_reader(&reader);
  ASSERT_EQ(buffer->type, buffer_from_reader->type);
  ASSERT_EQ(buffer_from_reader->data.raw.compression, GRPC_COMPRESS_NONE);
  ASSERT_EQ(buffer_from_reader->data.raw.slice_buffer.count, 1);
  ASSERT_EQ(memcmp(GRPC_SLICE_START_PTR(
                       buffer_from_reader->data.raw.slice_buffer.slices[0]),
                   "test", 4),
            0);

  grpc_byte_buffer_destroy(buffer);
  grpc_byte_buffer_destroy(buffer_from_reader);
}

TEST(GrpcByteBufferReaderTest, TestReadall) {
  char* lotsa_as[512];
  char* lotsa_bs[1024];
  grpc_slice slices[2];
  grpc_byte_buffer* buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice slice_out;

  LOG_TEST("test_readall");

  memset(lotsa_as, 'a', 512 * sizeof(lotsa_as[0]));
  memset(lotsa_bs, 'b', 1024 * sizeof(lotsa_bs[0]));
  // use slices large enough to overflow inlining
  slices[0] = grpc_slice_malloc(512);
  memcpy(GRPC_SLICE_START_PTR(slices[0]), lotsa_as, 512);
  slices[1] = grpc_slice_malloc(1024);
  memcpy(GRPC_SLICE_START_PTR(slices[1]), lotsa_bs, 1024);

  buffer = grpc_raw_byte_buffer_create(slices, 2);
  grpc_slice_unref(slices[0]);
  grpc_slice_unref(slices[1]);

  ASSERT_TRUE(grpc_byte_buffer_reader_init(&reader, buffer) &&
              "Couldn't init byte buffer reader");
  slice_out = grpc_byte_buffer_reader_readall(&reader);

  ASSERT_EQ(GRPC_SLICE_LENGTH(slice_out), 512 + 1024);
  ASSERT_EQ(memcmp(GRPC_SLICE_START_PTR(slice_out), lotsa_as, 512), 0);
  ASSERT_EQ(memcmp(&(GRPC_SLICE_START_PTR(slice_out)[512]), lotsa_bs, 1024), 0);
  grpc_slice_unref(slice_out);
  grpc_byte_buffer_destroy(buffer);
}

TEST(GrpcByteBufferReaderTest, TestByteBufferCopy) {
  char* lotsa_as[512];
  char* lotsa_bs[1024];
  grpc_slice slices[2];
  grpc_byte_buffer* buffer;
  grpc_byte_buffer* copied_buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice slice_out;

  LOG_TEST("test_byte_buffer_copy");

  memset(lotsa_as, 'a', 512 * sizeof(lotsa_as[0]));
  memset(lotsa_bs, 'b', 1024 * sizeof(lotsa_bs[0]));
  // use slices large enough to overflow inlining
  slices[0] = grpc_slice_malloc(512);
  memcpy(GRPC_SLICE_START_PTR(slices[0]), lotsa_as, 512);
  slices[1] = grpc_slice_malloc(1024);
  memcpy(GRPC_SLICE_START_PTR(slices[1]), lotsa_bs, 1024);

  buffer = grpc_raw_byte_buffer_create(slices, 2);
  grpc_slice_unref(slices[0]);
  grpc_slice_unref(slices[1]);
  copied_buffer = grpc_byte_buffer_copy(buffer);

  ASSERT_TRUE(grpc_byte_buffer_reader_init(&reader, buffer) &&
              "Couldn't init byte buffer reader");
  slice_out = grpc_byte_buffer_reader_readall(&reader);

  ASSERT_EQ(GRPC_SLICE_LENGTH(slice_out), 512 + 1024);
  ASSERT_EQ(memcmp(GRPC_SLICE_START_PTR(slice_out), lotsa_as, 512), 0);
  ASSERT_EQ(memcmp(&(GRPC_SLICE_START_PTR(slice_out)[512]), lotsa_bs, 1024), 0);
  grpc_slice_unref(slice_out);
  grpc_byte_buffer_destroy(buffer);
  grpc_byte_buffer_destroy(copied_buffer);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
