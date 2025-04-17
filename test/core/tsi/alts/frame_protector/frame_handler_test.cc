//
//
// Copyright 2018 gRPC authors.
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

#include "src/core/tsi/alts/frame_protector/frame_handler.h"

#include <grpc/support/alloc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "gtest/gtest.h"
#include "src/core/util/crash.h"
#include "src/core/util/useful.h"
#include "test/core/tsi/alts/crypt/gsec_test_util.h"

const size_t kFrameHandlerTestBufferSize = 1024;

typedef struct frame_handler {
  alts_frame_writer* writer;
  alts_frame_reader* reader;
  unsigned char* buffer;
  size_t buffer_size;
} frame_handler;

static size_t frame_length(size_t payload_length) {
  return payload_length + kFrameHeaderSize;
}

static frame_handler* create_frame_handler() {
  frame_handler* handler =
      static_cast<frame_handler*>(gpr_malloc(sizeof(frame_handler)));
  handler->writer = alts_create_frame_writer();
  handler->reader = alts_create_frame_reader();
  handler->buffer = nullptr;
  handler->buffer_size = 0;
  return handler;
}

static void destroy_frame_handler(frame_handler* handler) {
  if (handler != nullptr) {
    alts_destroy_frame_reader(handler->reader);
    alts_destroy_frame_writer(handler->writer);
    if (handler->buffer != nullptr) gpr_free(handler->buffer);
    gpr_free(handler);
  }
}

static void frame(frame_handler* handler, unsigned char* payload,
                  size_t payload_length, size_t write_length) {
  handler->buffer_size = frame_length(payload_length);
  handler->buffer =
      static_cast<unsigned char*>(gpr_malloc(handler->buffer_size));
  ASSERT_TRUE(
      alts_reset_frame_writer(handler->writer, payload, payload_length));
  size_t offset = 0;
  while (offset < handler->buffer_size &&
         !alts_is_frame_writer_done(handler->writer)) {
    size_t bytes_written =
        std::min(write_length, handler->buffer_size - offset);
    ASSERT_TRUE(alts_write_frame_bytes(
        handler->writer, handler->buffer + offset, &bytes_written));
    offset += bytes_written;
  }
  ASSERT_TRUE(alts_is_frame_writer_done(handler->writer));
  ASSERT_EQ(handler->buffer_size, offset);
}

static size_t deframe(frame_handler* handler, unsigned char* bytes,
                      size_t read_length) {
  EXPECT_TRUE(alts_reset_frame_reader(handler->reader, bytes));
  size_t offset = 0;
  while (offset < handler->buffer_size &&
         !alts_is_frame_reader_done(handler->reader)) {
    size_t bytes_read = std::min(read_length, handler->buffer_size - offset);
    EXPECT_TRUE(alts_read_frame_bytes(handler->reader, handler->buffer + offset,
                                      &bytes_read));
    offset += bytes_read;
  }
  EXPECT_TRUE(alts_is_frame_reader_done(handler->reader));
  EXPECT_EQ(handler->buffer_size, offset);
  return offset - handler->reader->header_bytes_read;
}

static void frame_n_deframe(frame_handler* handler, unsigned char* payload,
                            size_t payload_length, size_t write_length,
                            size_t read_length) {
  frame(handler, payload, payload_length, write_length);
  unsigned char* bytes =
      static_cast<unsigned char*>(gpr_malloc(kFrameHandlerTestBufferSize));
  size_t deframed_payload_length = deframe(handler, bytes, read_length);
  ASSERT_EQ(payload_length, deframed_payload_length);
  ASSERT_EQ(memcmp(payload, bytes, payload_length), 0);
  gpr_free(bytes);
}

TEST(FrameHandlerTest, FrameHandlerTestFrameDeframe) {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  frame_n_deframe(handler, payload, payload_length,
                  frame_length(payload_length), frame_length(payload_length));
  destroy_frame_handler(handler);
}

TEST(FrameHandlerTest, FrameHandlerTestSmallBuffer) {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  frame_n_deframe(handler, payload, payload_length, 1, 1);
  destroy_frame_handler(handler);
}

TEST(FrameHandlerTest, FrameHandlerTestNullInputStream) {
  frame_handler* handler = create_frame_handler();
  ASSERT_FALSE(alts_reset_frame_writer(handler->writer, nullptr, 0));
  destroy_frame_handler(handler);
}

TEST(FrameHandlerTest, FrameHandlerTestBadInputLength) {
  unsigned char payload[] = "hello world";
  frame_handler* handler = create_frame_handler();
  ASSERT_FALSE(alts_reset_frame_writer(handler->writer, payload, SIZE_MAX));
  destroy_frame_handler(handler);
}

TEST(FrameHandlerTest, FrameHandlerTestNullWriterByteLength) {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  ASSERT_TRUE(
      alts_reset_frame_writer(handler->writer, payload, payload_length));
  ASSERT_TRUE(
      !alts_write_frame_bytes(handler->writer, handler->buffer, nullptr));
  destroy_frame_handler(handler);
}

TEST(FrameHandlerTest, FrameHandlerTestNullWriterBytes) {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  ASSERT_TRUE(
      alts_reset_frame_writer(handler->writer, payload, payload_length));
  ASSERT_TRUE(
      !alts_write_frame_bytes(handler->writer, nullptr, &payload_length));
  destroy_frame_handler(handler);
}

TEST(FrameHandlerTest, FrameHandlerTestBadFrameLength) {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  frame(handler, payload, payload_length, payload_length);
  memset(handler->buffer, 0x00, kFrameLengthFieldSize);
  unsigned char* bytes =
      static_cast<unsigned char*>(gpr_malloc(kFrameHandlerTestBufferSize));
  ASSERT_TRUE(alts_reset_frame_reader(handler->reader, bytes));
  size_t bytes_read = handler->buffer_size;
  ASSERT_TRUE(
      !alts_read_frame_bytes(handler->reader, handler->buffer, &bytes_read));
  ASSERT_TRUE(alts_is_frame_reader_done(handler->reader));
  ASSERT_EQ(bytes_read, 0);
  gpr_free(bytes);
  destroy_frame_handler(handler);
}

TEST(FrameHandlerTest, FrameHandlerTestUnsupportedMessageType) {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  frame(handler, payload, payload_length, payload_length);
  memset(handler->buffer + kFrameLengthFieldSize, 0x00,
         kFrameMessageTypeFieldSize);
  unsigned char* bytes =
      static_cast<unsigned char*>(gpr_malloc(kFrameHandlerTestBufferSize));
  ASSERT_TRUE(alts_reset_frame_reader(handler->reader, bytes));
  size_t bytes_read = handler->buffer_size;
  ASSERT_TRUE(
      !alts_read_frame_bytes(handler->reader, handler->buffer, &bytes_read));
  ASSERT_TRUE(alts_is_frame_reader_done(handler->reader));
  ASSERT_EQ(bytes_read, 0);
  gpr_free(bytes);
  destroy_frame_handler(handler);
}

TEST(FrameHandlerTest, FrameHandlerTestNullOutputStream) {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  frame(handler, payload, payload_length, payload_length);
  ASSERT_FALSE(alts_reset_frame_reader(handler->reader, nullptr));
  destroy_frame_handler(handler);
}

TEST(FrameHandlerTest, FrameHandlerTestNullReaderByteLength) {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  frame(handler, payload, payload_length, payload_length);
  unsigned char* bytes =
      static_cast<unsigned char*>(gpr_malloc(kFrameHandlerTestBufferSize));
  ASSERT_TRUE(alts_reset_frame_reader(handler->reader, bytes));
  ASSERT_FALSE(
      alts_read_frame_bytes(handler->reader, handler->buffer, nullptr));
  gpr_free(bytes);
  destroy_frame_handler(handler);
}

TEST(FrameHandlerTest, FrameHandlerTestNullReaderBytes) {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  frame(handler, payload, payload_length, payload_length);
  unsigned char* bytes =
      static_cast<unsigned char*>(gpr_malloc(kFrameHandlerTestBufferSize));
  ASSERT_TRUE(alts_reset_frame_reader(handler->reader, bytes));
  size_t bytes_read = handler->buffer_size;
  ASSERT_FALSE(alts_read_frame_bytes(handler->reader, nullptr, &bytes_read));
  gpr_free(bytes);
  destroy_frame_handler(handler);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
