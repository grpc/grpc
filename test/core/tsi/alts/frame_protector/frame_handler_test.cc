/*
 *
 * Copyright 2018 gRPC authors.
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/tsi/alts/frame_protector/frame_handler.h"
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
  GPR_ASSERT(alts_reset_frame_writer(handler->writer, payload, payload_length));
  size_t offset = 0;
  while (offset < handler->buffer_size &&
         !alts_is_frame_writer_done(handler->writer)) {
    size_t bytes_written = GPR_MIN(write_length, handler->buffer_size - offset);
    GPR_ASSERT(alts_write_frame_bytes(handler->writer, handler->buffer + offset,
                                      &bytes_written));
    offset += bytes_written;
  }
  GPR_ASSERT(alts_is_frame_writer_done(handler->writer));
  GPR_ASSERT(handler->buffer_size == offset);
}

static size_t deframe(frame_handler* handler, unsigned char* bytes,
                      size_t read_length) {
  GPR_ASSERT(alts_reset_frame_reader(handler->reader, bytes));
  size_t offset = 0;
  while (offset < handler->buffer_size &&
         !alts_is_frame_reader_done(handler->reader)) {
    size_t bytes_read = GPR_MIN(read_length, handler->buffer_size - offset);
    GPR_ASSERT(alts_read_frame_bytes(handler->reader, handler->buffer + offset,
                                     &bytes_read));
    offset += bytes_read;
  }
  GPR_ASSERT(alts_is_frame_reader_done(handler->reader));
  GPR_ASSERT(handler->buffer_size == offset);
  return offset - handler->reader->header_bytes_read;
}

static void frame_n_deframe(frame_handler* handler, unsigned char* payload,
                            size_t payload_length, size_t write_length,
                            size_t read_length) {
  frame(handler, payload, payload_length, write_length);
  unsigned char* bytes =
      static_cast<unsigned char*>(gpr_malloc(kFrameHandlerTestBufferSize));
  size_t deframed_payload_length = deframe(handler, bytes, read_length);
  GPR_ASSERT(payload_length == deframed_payload_length);
  GPR_ASSERT(memcmp(payload, bytes, payload_length) == 0);
  gpr_free(bytes);
}

static void frame_handler_test_frame_deframe() {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen((char*)payload) + 1;
  frame_handler* handler = create_frame_handler();
  frame_n_deframe(handler, payload, payload_length,
                  frame_length(payload_length), frame_length(payload_length));
  destroy_frame_handler(handler);
}

static void frame_handler_test_small_buffer() {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  frame_n_deframe(handler, payload, payload_length, 1, 1);
  destroy_frame_handler(handler);
}

static void frame_handler_test_null_input_stream() {
  frame_handler* handler = create_frame_handler();
  GPR_ASSERT(!alts_reset_frame_writer(handler->writer, nullptr, 0));
  destroy_frame_handler(handler);
}

static void frame_handler_test_bad_input_length() {
  unsigned char payload[] = "hello world";
  frame_handler* handler = create_frame_handler();
  GPR_ASSERT(!alts_reset_frame_writer(handler->writer, payload, SIZE_MAX));
  destroy_frame_handler(handler);
}

static void frame_handler_test_null_writer_byte_length() {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  GPR_ASSERT(alts_reset_frame_writer(handler->writer, payload, payload_length));
  GPR_ASSERT(
      !alts_write_frame_bytes(handler->writer, handler->buffer, nullptr));
  destroy_frame_handler(handler);
}

static void frame_handler_test_null_writer_bytes() {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  GPR_ASSERT(alts_reset_frame_writer(handler->writer, payload, payload_length));
  GPR_ASSERT(
      !alts_write_frame_bytes(handler->writer, nullptr, &payload_length));
  destroy_frame_handler(handler);
}

static void frame_handler_test_bad_frame_length() {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  frame(handler, payload, payload_length, payload_length);
  memset(handler->buffer, 0x00, kFrameLengthFieldSize);
  unsigned char* bytes =
      static_cast<unsigned char*>(gpr_malloc(kFrameHandlerTestBufferSize));
  GPR_ASSERT(alts_reset_frame_reader(handler->reader, bytes));
  size_t bytes_read = handler->buffer_size;
  GPR_ASSERT(
      !alts_read_frame_bytes(handler->reader, handler->buffer, &bytes_read));
  GPR_ASSERT(alts_is_frame_reader_done(handler->reader));
  GPR_ASSERT(bytes_read == 0);
  gpr_free(bytes);
  destroy_frame_handler(handler);
}

static void frame_handler_test_unsupported_message_type() {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  frame(handler, payload, payload_length, payload_length);
  memset(handler->buffer + kFrameLengthFieldSize, 0x00,
         kFrameMessageTypeFieldSize);
  unsigned char* bytes =
      static_cast<unsigned char*>(gpr_malloc(kFrameHandlerTestBufferSize));
  GPR_ASSERT(alts_reset_frame_reader(handler->reader, bytes));
  size_t bytes_read = handler->buffer_size;
  GPR_ASSERT(
      !alts_read_frame_bytes(handler->reader, handler->buffer, &bytes_read));
  GPR_ASSERT(alts_is_frame_reader_done(handler->reader));
  GPR_ASSERT(bytes_read == 0);
  gpr_free(bytes);
  destroy_frame_handler(handler);
}

static void frame_handler_test_null_output_stream() {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  frame(handler, payload, payload_length, payload_length);
  GPR_ASSERT(!alts_reset_frame_reader(handler->reader, nullptr));
  destroy_frame_handler(handler);
}

static void frame_handler_test_null_reader_byte_length() {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  frame(handler, payload, payload_length, payload_length);
  unsigned char* bytes =
      static_cast<unsigned char*>(gpr_malloc(kFrameHandlerTestBufferSize));
  GPR_ASSERT(alts_reset_frame_reader(handler->reader, bytes));
  GPR_ASSERT(!alts_read_frame_bytes(handler->reader, handler->buffer, nullptr));
  gpr_free(bytes);
  destroy_frame_handler(handler);
}

static void frame_handler_test_null_reader_bytes() {
  unsigned char payload[] = "hello world";
  size_t payload_length = strlen(reinterpret_cast<char*>(payload)) + 1;
  frame_handler* handler = create_frame_handler();
  frame(handler, payload, payload_length, payload_length);
  unsigned char* bytes =
      static_cast<unsigned char*>(gpr_malloc(kFrameHandlerTestBufferSize));
  GPR_ASSERT(alts_reset_frame_reader(handler->reader, bytes));
  size_t bytes_read = handler->buffer_size;
  GPR_ASSERT(!alts_read_frame_bytes(handler->reader, nullptr, &bytes_read));
  gpr_free(bytes);
  destroy_frame_handler(handler);
}

int main(int argc, char** argv) {
  frame_handler_test_frame_deframe();
  frame_handler_test_small_buffer();
  frame_handler_test_null_input_stream();
  frame_handler_test_bad_input_length();
  frame_handler_test_null_writer_byte_length();
  frame_handler_test_null_writer_bytes();
  frame_handler_test_bad_frame_length();
  frame_handler_test_unsupported_message_type();
  frame_handler_test_null_output_stream();
  frame_handler_test_null_reader_byte_length();
  frame_handler_test_null_reader_bytes();
  return 0;
}
