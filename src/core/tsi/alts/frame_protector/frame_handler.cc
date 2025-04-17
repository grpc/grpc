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
#include <grpc/support/port_platform.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>

#include "absl/log/log.h"
#include "src/core/util/crash.h"
#include "src/core/util/memory.h"

// Use little endian to interpret a string of bytes as uint32_t.
static uint32_t load_32_le(const unsigned char* buffer) {
  return (static_cast<uint32_t>(buffer[3]) << 24) |
         (static_cast<uint32_t>(buffer[2]) << 16) |
         (static_cast<uint32_t>(buffer[1]) << 8) |
         static_cast<uint32_t>(buffer[0]);
}

// Store uint32_t as a string of little endian bytes.
static void store_32_le(uint32_t value, unsigned char* buffer) {
  buffer[3] = static_cast<unsigned char>(value >> 24) & 0xFF;
  buffer[2] = static_cast<unsigned char>(value >> 16) & 0xFF;
  buffer[1] = static_cast<unsigned char>(value >> 8) & 0xFF;
  buffer[0] = static_cast<unsigned char>(value) & 0xFF;
}

// Frame writer implementation.
alts_frame_writer* alts_create_frame_writer() {
  return grpc_core::Zalloc<alts_frame_writer>();
}

bool alts_reset_frame_writer(alts_frame_writer* writer,
                             const unsigned char* buffer, size_t length) {
  if (buffer == nullptr) return false;
  size_t max_input_size = SIZE_MAX - kFrameLengthFieldSize;
  if (length > max_input_size) {
    LOG(ERROR) << "length must be at most " << max_input_size;
    return false;
  }
  writer->input_buffer = buffer;
  writer->input_size = length;
  writer->input_bytes_written = 0;
  writer->header_bytes_written = 0;
  store_32_le(
      static_cast<uint32_t>(writer->input_size + kFrameMessageTypeFieldSize),
      writer->header_buffer);
  store_32_le(kFrameMessageType, writer->header_buffer + kFrameLengthFieldSize);
  return true;
}

bool alts_write_frame_bytes(alts_frame_writer* writer, unsigned char* output,
                            size_t* bytes_size) {
  if (bytes_size == nullptr || output == nullptr) return false;
  if (alts_is_frame_writer_done(writer)) {
    *bytes_size = 0;
    return true;
  }
  size_t bytes_written = 0;
  // Write some header bytes, if needed.
  if (writer->header_bytes_written != sizeof(writer->header_buffer)) {
    size_t bytes_to_write =
        std::min(*bytes_size,
                 sizeof(writer->header_buffer) - writer->header_bytes_written);
    memcpy(output, writer->header_buffer + writer->header_bytes_written,
           bytes_to_write);
    bytes_written += bytes_to_write;
    *bytes_size -= bytes_to_write;
    writer->header_bytes_written += bytes_to_write;
    output += bytes_to_write;
    if (writer->header_bytes_written != sizeof(writer->header_buffer)) {
      *bytes_size = bytes_written;
      return true;
    }
  }
  // Write some non-header bytes.
  size_t bytes_to_write =
      std::min(writer->input_size - writer->input_bytes_written, *bytes_size);
  memcpy(output, writer->input_buffer, bytes_to_write);
  writer->input_buffer += bytes_to_write;
  bytes_written += bytes_to_write;
  writer->input_bytes_written += bytes_to_write;
  *bytes_size = bytes_written;
  return true;
}

bool alts_is_frame_writer_done(alts_frame_writer* writer) {
  return writer->input_buffer == nullptr ||
         writer->input_size == writer->input_bytes_written;
}

size_t alts_get_num_writer_bytes_remaining(alts_frame_writer* writer) {
  return (sizeof(writer->header_buffer) - writer->header_bytes_written) +
         (writer->input_size - writer->input_bytes_written);
}

void alts_destroy_frame_writer(alts_frame_writer* writer) { gpr_free(writer); }

// Frame reader implementation.
alts_frame_reader* alts_create_frame_reader() {
  alts_frame_reader* reader = grpc_core::Zalloc<alts_frame_reader>();
  return reader;
}

bool alts_is_frame_reader_done(alts_frame_reader* reader) {
  return reader->output_buffer == nullptr ||
         (reader->header_bytes_read == sizeof(reader->header_buffer) &&
          reader->bytes_remaining == 0);
}

bool alts_has_read_frame_length(alts_frame_reader* reader) {
  return sizeof(reader->header_buffer) == reader->header_bytes_read;
}

size_t alts_get_reader_bytes_remaining(alts_frame_reader* reader) {
  return alts_has_read_frame_length(reader) ? reader->bytes_remaining : 0;
}

void alts_reset_reader_output_buffer(alts_frame_reader* reader,
                                     unsigned char* buffer) {
  reader->output_buffer = buffer;
}

bool alts_reset_frame_reader(alts_frame_reader* reader, unsigned char* buffer) {
  if (buffer == nullptr) return false;
  reader->output_buffer = buffer;
  reader->bytes_remaining = 0;
  reader->header_bytes_read = 0;
  reader->output_bytes_read = 0;
  return true;
}

bool alts_read_frame_bytes(alts_frame_reader* reader,
                           const unsigned char* bytes, size_t* bytes_size) {
  if (bytes_size == nullptr) return false;
  if (bytes == nullptr) {
    *bytes_size = 0;
    return false;
  }
  if (alts_is_frame_reader_done(reader)) {
    *bytes_size = 0;
    return true;
  }
  size_t bytes_processed = 0;
  // Process the header, if needed.
  if (reader->header_bytes_read != sizeof(reader->header_buffer)) {
    size_t bytes_to_write = std::min(
        *bytes_size, sizeof(reader->header_buffer) - reader->header_bytes_read);
    memcpy(reader->header_buffer + reader->header_bytes_read, bytes,
           bytes_to_write);
    reader->header_bytes_read += bytes_to_write;
    bytes_processed += bytes_to_write;
    bytes += bytes_to_write;
    *bytes_size -= bytes_to_write;
    if (reader->header_bytes_read != sizeof(reader->header_buffer)) {
      *bytes_size = bytes_processed;
      return true;
    }
    size_t frame_length = load_32_le(reader->header_buffer);
    if (frame_length < kFrameMessageTypeFieldSize ||
        frame_length > kFrameMaxSize) {
      LOG(ERROR) << "Bad frame length (should be at least "
                 << kFrameMessageTypeFieldSize << ", and at most "
                 << kFrameMaxSize << ")";
      *bytes_size = 0;
      return false;
    }
    size_t message_type =
        load_32_le(reader->header_buffer + kFrameLengthFieldSize);
    if (message_type != kFrameMessageType) {
      LOG(ERROR) << "Unsupported message type " << message_type
                 << " (should be " << kFrameMessageType << ")";
      *bytes_size = 0;
      return false;
    }
    reader->bytes_remaining = frame_length - kFrameMessageTypeFieldSize;
  }
  // Process the non-header bytes.
  size_t bytes_to_write = std::min(*bytes_size, reader->bytes_remaining);
  memcpy(reader->output_buffer, bytes, bytes_to_write);
  reader->output_buffer += bytes_to_write;
  bytes_processed += bytes_to_write;
  reader->bytes_remaining -= bytes_to_write;
  reader->output_bytes_read += bytes_to_write;
  *bytes_size = bytes_processed;
  return true;
}

size_t alts_get_output_bytes_read(alts_frame_reader* reader) {
  return reader->output_bytes_read;
}

unsigned char* alts_get_output_buffer(alts_frame_reader* reader) {
  return reader->output_buffer;
}

void alts_destroy_frame_reader(alts_frame_reader* reader) { gpr_free(reader); }
