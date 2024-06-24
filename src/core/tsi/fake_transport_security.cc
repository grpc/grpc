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

#include "src/core/tsi/fake_transport_security.h"

#include <stdlib.h>
#include <string.h>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/transport_security_grpc.h"
#include "src/core/tsi/transport_security_interface.h"

// --- Constants. ---
#define TSI_FAKE_FRAME_HEADER_SIZE 4
#define TSI_FAKE_FRAME_INITIAL_ALLOCATED_SIZE 64
#define TSI_FAKE_DEFAULT_FRAME_SIZE 16384
#define TSI_FAKE_HANDSHAKER_OUTGOING_BUFFER_INITIAL_SIZE 256

// --- Structure definitions. ---

// a frame is encoded like this:
// | size |     data    |
// where the size field value is the size of the size field plus the size of
// the data encoded in little endian on 4 bytes.
struct tsi_fake_frame {
  unsigned char* data;
  size_t size;
  size_t allocated_size;
  size_t offset;
  int needs_draining;
};
typedef enum {
  TSI_FAKE_CLIENT_INIT = 0,
  TSI_FAKE_SERVER_INIT = 1,
  TSI_FAKE_CLIENT_FINISHED = 2,
  TSI_FAKE_SERVER_FINISHED = 3,
  TSI_FAKE_HANDSHAKE_MESSAGE_MAX = 4
} tsi_fake_handshake_message;

struct tsi_fake_handshaker {
  tsi_handshaker base;
  int is_client;
  tsi_fake_handshake_message next_message_to_send;
  int needs_incoming_message;
  tsi_fake_frame incoming_frame;
  tsi_fake_frame outgoing_frame;
  unsigned char* outgoing_bytes_buffer;
  size_t outgoing_bytes_buffer_size;
  tsi_result result;
};
struct tsi_fake_frame_protector {
  tsi_frame_protector base;
  tsi_fake_frame protect_frame;
  tsi_fake_frame unprotect_frame;
  size_t max_frame_size;
};
struct tsi_fake_zero_copy_grpc_protector {
  tsi_zero_copy_grpc_protector base;
  grpc_slice_buffer header_sb;
  grpc_slice_buffer protected_sb;
  size_t max_frame_size;
  size_t parsed_frame_size;
};
// --- Utils. ---

static const char* tsi_fake_handshake_message_strings[] = {
    "CLIENT_INIT", "SERVER_INIT", "CLIENT_FINISHED", "SERVER_FINISHED"};

static const char* tsi_fake_handshake_message_to_string(int msg) {
  if (msg < 0 || msg >= TSI_FAKE_HANDSHAKE_MESSAGE_MAX) {
    LOG(ERROR) << "Invalid message " << msg;
    return "UNKNOWN";
  }
  return tsi_fake_handshake_message_strings[msg];
}

static tsi_result tsi_fake_handshake_message_from_string(
    const char* msg_string, tsi_fake_handshake_message* msg,
    std::string* error) {
  for (int i = 0; i < TSI_FAKE_HANDSHAKE_MESSAGE_MAX; i++) {
    if (strncmp(msg_string, tsi_fake_handshake_message_strings[i],
                strlen(tsi_fake_handshake_message_strings[i])) == 0) {
      *msg = static_cast<tsi_fake_handshake_message>(i);
      return TSI_OK;
    }
  }
  LOG(ERROR) << "Invalid handshake message.";
  if (error != nullptr) *error = "invalid handshake message";
  return TSI_DATA_CORRUPTED;
}

static uint32_t load32_little_endian(const unsigned char* buf) {
  return (static_cast<uint32_t>(buf[0]) | static_cast<uint32_t>(buf[1] << 8) |
          static_cast<uint32_t>(buf[2] << 16) |
          static_cast<uint32_t>(buf[3] << 24));
}

static void store32_little_endian(uint32_t value, unsigned char* buf) {
  buf[3] = static_cast<unsigned char>((value >> 24) & 0xFF);
  buf[2] = static_cast<unsigned char>((value >> 16) & 0xFF);
  buf[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
  buf[0] = static_cast<unsigned char>((value) & 0xFF);
}

static uint32_t read_frame_size(const grpc_slice_buffer* sb) {
  CHECK(sb != nullptr);
  CHECK(sb->length >= TSI_FAKE_FRAME_HEADER_SIZE);
  uint8_t frame_size_buffer[TSI_FAKE_FRAME_HEADER_SIZE];
  uint8_t* buf = frame_size_buffer;
  // Copies the first 4 bytes to a temporary buffer.
  size_t remaining = TSI_FAKE_FRAME_HEADER_SIZE;
  for (size_t i = 0; i < sb->count; i++) {
    size_t slice_length = GRPC_SLICE_LENGTH(sb->slices[i]);
    if (remaining <= slice_length) {
      memcpy(buf, GRPC_SLICE_START_PTR(sb->slices[i]), remaining);
      remaining = 0;
      break;
    } else {
      memcpy(buf, GRPC_SLICE_START_PTR(sb->slices[i]), slice_length);
      buf += slice_length;
      remaining -= slice_length;
    }
  }
  CHECK_EQ(remaining, 0u);
  return load32_little_endian(frame_size_buffer);
}

uint32_t tsi_fake_zero_copy_grpc_protector_next_frame_size(
    const grpc_slice_buffer* protected_slices) {
  return read_frame_size(protected_slices);
}

static void tsi_fake_frame_reset(tsi_fake_frame* frame, int needs_draining) {
  frame->offset = 0;
  frame->needs_draining = needs_draining;
  if (!needs_draining) frame->size = 0;
}

// Checks if the frame's allocated size is at least frame->size, and reallocs
// more memory if necessary.
static void tsi_fake_frame_ensure_size(tsi_fake_frame* frame) {
  if (frame->data == nullptr) {
    frame->allocated_size = frame->size;
    frame->data =
        static_cast<unsigned char*>(gpr_malloc(frame->allocated_size));
  } else if (frame->size > frame->allocated_size) {
    unsigned char* new_data =
        static_cast<unsigned char*>(gpr_realloc(frame->data, frame->size));
    frame->data = new_data;
    frame->allocated_size = frame->size;
  }
}

// Decodes the serialized fake frame contained in incoming_bytes, and fills
// frame with the contents of the decoded frame.
// This method should not be called if frame->needs_framing is not 0.
static tsi_result tsi_fake_frame_decode(const unsigned char* incoming_bytes,
                                        size_t* incoming_bytes_size,
                                        tsi_fake_frame* frame,
                                        std::string* error) {
  size_t available_size = *incoming_bytes_size;
  size_t to_read_size = 0;
  const unsigned char* bytes_cursor = incoming_bytes;

  if (frame->needs_draining) {
    if (error != nullptr) *error = "fake handshaker frame needs draining";
    return TSI_INTERNAL_ERROR;
  }
  if (frame->data == nullptr) {
    frame->allocated_size = TSI_FAKE_FRAME_INITIAL_ALLOCATED_SIZE;
    frame->data =
        static_cast<unsigned char*>(gpr_malloc(frame->allocated_size));
  }

  if (frame->offset < TSI_FAKE_FRAME_HEADER_SIZE) {
    to_read_size = TSI_FAKE_FRAME_HEADER_SIZE - frame->offset;
    if (to_read_size > available_size) {
      // Just fill what we can and exit.
      memcpy(frame->data + frame->offset, bytes_cursor, available_size);
      bytes_cursor += available_size;
      frame->offset += available_size;
      *incoming_bytes_size = static_cast<size_t>(bytes_cursor - incoming_bytes);
      return TSI_INCOMPLETE_DATA;
    }
    memcpy(frame->data + frame->offset, bytes_cursor, to_read_size);
    bytes_cursor += to_read_size;
    frame->offset += to_read_size;
    available_size -= to_read_size;
    frame->size = load32_little_endian(frame->data);
    tsi_fake_frame_ensure_size(frame);
  }

  to_read_size = frame->size - frame->offset;
  if (to_read_size > available_size) {
    memcpy(frame->data + frame->offset, bytes_cursor, available_size);
    frame->offset += available_size;
    bytes_cursor += available_size;
    *incoming_bytes_size = static_cast<size_t>(bytes_cursor - incoming_bytes);
    return TSI_INCOMPLETE_DATA;
  }
  memcpy(frame->data + frame->offset, bytes_cursor, to_read_size);
  bytes_cursor += to_read_size;
  *incoming_bytes_size = static_cast<size_t>(bytes_cursor - incoming_bytes);
  tsi_fake_frame_reset(frame, 1 /* needs_draining */);
  return TSI_OK;
}

// Encodes a fake frame into its wire format and places the result in
// outgoing_bytes. outgoing_bytes_size indicates the size of the encoded frame.
// This method should not be called if frame->needs_framing is 0.
static tsi_result tsi_fake_frame_encode(unsigned char* outgoing_bytes,
                                        size_t* outgoing_bytes_size,
                                        tsi_fake_frame* frame,
                                        std::string* error) {
  size_t to_write_size = frame->size - frame->offset;
  if (!frame->needs_draining) {
    if (error != nullptr) *error = "fake frame needs draining";
    return TSI_INTERNAL_ERROR;
  }
  if (*outgoing_bytes_size < to_write_size) {
    memcpy(outgoing_bytes, frame->data + frame->offset, *outgoing_bytes_size);
    frame->offset += *outgoing_bytes_size;
    return TSI_INCOMPLETE_DATA;
  }
  memcpy(outgoing_bytes, frame->data + frame->offset, to_write_size);
  *outgoing_bytes_size = to_write_size;
  tsi_fake_frame_reset(frame, 0 /* needs_draining */);
  return TSI_OK;
}

// Sets the payload of a fake frame to contain the given data blob, where
// data_size indicates the size of data.
static void tsi_fake_frame_set_data(unsigned char* data, size_t data_size,
                                    tsi_fake_frame* frame) {
  frame->offset = 0;
  frame->size = data_size + TSI_FAKE_FRAME_HEADER_SIZE;
  tsi_fake_frame_ensure_size(frame);
  store32_little_endian(static_cast<uint32_t>(frame->size), frame->data);
  memcpy(frame->data + TSI_FAKE_FRAME_HEADER_SIZE, data, data_size);
  tsi_fake_frame_reset(frame, 1 /* needs draining */);
}

// Destroys the contents of a fake frame.
static void tsi_fake_frame_destruct(tsi_fake_frame* frame) {
  if (frame->data != nullptr) gpr_free(frame->data);
}

// --- tsi_frame_protector methods implementation. ---

static tsi_result fake_protector_protect(tsi_frame_protector* self,
                                         const unsigned char* unprotected_bytes,
                                         size_t* unprotected_bytes_size,
                                         unsigned char* protected_output_frames,
                                         size_t* protected_output_frames_size) {
  tsi_result result = TSI_OK;
  tsi_fake_frame_protector* impl =
      reinterpret_cast<tsi_fake_frame_protector*>(self);
  unsigned char frame_header[TSI_FAKE_FRAME_HEADER_SIZE];
  tsi_fake_frame* frame = &impl->protect_frame;
  size_t saved_output_size = *protected_output_frames_size;
  size_t drained_size = 0;
  size_t* num_bytes_written = protected_output_frames_size;
  *num_bytes_written = 0;

  // Try to drain first.
  if (frame->needs_draining) {
    drained_size = saved_output_size - *num_bytes_written;
    result = tsi_fake_frame_encode(protected_output_frames, &drained_size,
                                   frame, /*error=*/nullptr);
    *num_bytes_written += drained_size;
    protected_output_frames += drained_size;
    if (result != TSI_OK) {
      if (result == TSI_INCOMPLETE_DATA) {
        *unprotected_bytes_size = 0;
        result = TSI_OK;
      }
      return result;
    }
  }

  // Now process the unprotected_bytes.
  if (frame->needs_draining) return TSI_INTERNAL_ERROR;
  if (frame->size == 0) {
    // New frame, create a header.
    size_t written_in_frame_size = 0;
    store32_little_endian(static_cast<uint32_t>(impl->max_frame_size),
                          frame_header);
    written_in_frame_size = TSI_FAKE_FRAME_HEADER_SIZE;
    result = tsi_fake_frame_decode(frame_header, &written_in_frame_size, frame,
                                   /*error=*/nullptr);
    if (result != TSI_INCOMPLETE_DATA) {
      LOG(ERROR) << "tsi_fake_frame_decode returned "
                 << tsi_result_to_string(result);
      return result;
    }
  }
  result =
      tsi_fake_frame_decode(unprotected_bytes, unprotected_bytes_size, frame,
                            /*error=*/nullptr);
  if (result != TSI_OK) {
    if (result == TSI_INCOMPLETE_DATA) result = TSI_OK;
    return result;
  }

  // Try to drain again.
  if (!frame->needs_draining) return TSI_INTERNAL_ERROR;
  if (frame->offset != 0) return TSI_INTERNAL_ERROR;
  drained_size = saved_output_size - *num_bytes_written;
  result = tsi_fake_frame_encode(protected_output_frames, &drained_size, frame,
                                 /*error=*/nullptr);
  *num_bytes_written += drained_size;
  if (result == TSI_INCOMPLETE_DATA) result = TSI_OK;
  return result;
}

static tsi_result fake_protector_protect_flush(
    tsi_frame_protector* self, unsigned char* protected_output_frames,
    size_t* protected_output_frames_size, size_t* still_pending_size) {
  tsi_result result = TSI_OK;
  tsi_fake_frame_protector* impl =
      reinterpret_cast<tsi_fake_frame_protector*>(self);
  tsi_fake_frame* frame = &impl->protect_frame;
  if (!frame->needs_draining) {
    // Create a short frame.
    frame->size = frame->offset;
    frame->offset = 0;
    frame->needs_draining = 1;
    store32_little_endian(static_cast<uint32_t>(frame->size),
                          frame->data);  // Overwrite header.
  }
  result = tsi_fake_frame_encode(protected_output_frames,
                                 protected_output_frames_size, frame,
                                 /*error=*/nullptr);
  if (result == TSI_INCOMPLETE_DATA) result = TSI_OK;
  *still_pending_size = frame->size - frame->offset;
  return result;
}

static tsi_result fake_protector_unprotect(
    tsi_frame_protector* self, const unsigned char* protected_frames_bytes,
    size_t* protected_frames_bytes_size, unsigned char* unprotected_bytes,
    size_t* unprotected_bytes_size) {
  tsi_result result = TSI_OK;
  tsi_fake_frame_protector* impl =
      reinterpret_cast<tsi_fake_frame_protector*>(self);
  tsi_fake_frame* frame = &impl->unprotect_frame;
  size_t saved_output_size = *unprotected_bytes_size;
  size_t drained_size = 0;
  size_t* num_bytes_written = unprotected_bytes_size;
  *num_bytes_written = 0;

  // Try to drain first.
  if (frame->needs_draining) {
    // Go past the header if needed.
    if (frame->offset == 0) frame->offset = TSI_FAKE_FRAME_HEADER_SIZE;
    drained_size = saved_output_size - *num_bytes_written;
    result = tsi_fake_frame_encode(unprotected_bytes, &drained_size, frame,
                                   /*error=*/nullptr);
    unprotected_bytes += drained_size;
    *num_bytes_written += drained_size;
    if (result != TSI_OK) {
      if (result == TSI_INCOMPLETE_DATA) {
        *protected_frames_bytes_size = 0;
        result = TSI_OK;
      }
      return result;
    }
  }

  // Now process the protected_bytes.
  if (frame->needs_draining) return TSI_INTERNAL_ERROR;
  result = tsi_fake_frame_decode(protected_frames_bytes,
                                 protected_frames_bytes_size, frame,
                                 /*error=*/nullptr);
  if (result != TSI_OK) {
    if (result == TSI_INCOMPLETE_DATA) result = TSI_OK;
    return result;
  }

  // Try to drain again.
  if (!frame->needs_draining) return TSI_INTERNAL_ERROR;
  if (frame->offset != 0) return TSI_INTERNAL_ERROR;
  frame->offset = TSI_FAKE_FRAME_HEADER_SIZE;  // Go past the header.
  drained_size = saved_output_size - *num_bytes_written;
  result = tsi_fake_frame_encode(unprotected_bytes, &drained_size, frame,
                                 /*error=*/nullptr);
  *num_bytes_written += drained_size;
  if (result == TSI_INCOMPLETE_DATA) result = TSI_OK;
  return result;
}

static void fake_protector_destroy(tsi_frame_protector* self) {
  tsi_fake_frame_protector* impl =
      reinterpret_cast<tsi_fake_frame_protector*>(self);
  tsi_fake_frame_destruct(&impl->protect_frame);
  tsi_fake_frame_destruct(&impl->unprotect_frame);
  gpr_free(self);
}

static const tsi_frame_protector_vtable frame_protector_vtable = {
    fake_protector_protect,
    fake_protector_protect_flush,
    fake_protector_unprotect,
    fake_protector_destroy,
};

// --- tsi_zero_copy_grpc_protector methods implementation. ---

static tsi_result fake_zero_copy_grpc_protector_protect(
    tsi_zero_copy_grpc_protector* self, grpc_slice_buffer* unprotected_slices,
    grpc_slice_buffer* protected_slices) {
  if (self == nullptr || unprotected_slices == nullptr ||
      protected_slices == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  tsi_fake_zero_copy_grpc_protector* impl =
      reinterpret_cast<tsi_fake_zero_copy_grpc_protector*>(self);
  // Protects each frame.
  while (unprotected_slices->length > 0) {
    size_t frame_length =
        std::min(impl->max_frame_size,
                 unprotected_slices->length + TSI_FAKE_FRAME_HEADER_SIZE);
    grpc_slice slice = GRPC_SLICE_MALLOC(TSI_FAKE_FRAME_HEADER_SIZE);
    store32_little_endian(static_cast<uint32_t>(frame_length),
                          GRPC_SLICE_START_PTR(slice));
    grpc_slice_buffer_add(protected_slices, slice);
    size_t data_length = frame_length - TSI_FAKE_FRAME_HEADER_SIZE;
    grpc_slice_buffer_move_first(unprotected_slices, data_length,
                                 protected_slices);
  }
  return TSI_OK;
}

static tsi_result fake_zero_copy_grpc_protector_unprotect(
    tsi_zero_copy_grpc_protector* self, grpc_slice_buffer* protected_slices,
    grpc_slice_buffer* unprotected_slices, int* min_progress_size) {
  if (self == nullptr || unprotected_slices == nullptr ||
      protected_slices == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  tsi_fake_zero_copy_grpc_protector* impl =
      reinterpret_cast<tsi_fake_zero_copy_grpc_protector*>(self);
  grpc_slice_buffer_move_into(protected_slices, &impl->protected_sb);
  // Unprotect each frame, if we get a full frame.
  while (impl->protected_sb.length >= TSI_FAKE_FRAME_HEADER_SIZE) {
    if (impl->parsed_frame_size == 0) {
      impl->parsed_frame_size = read_frame_size(&impl->protected_sb);
      if (impl->parsed_frame_size <= 4) {
        LOG(ERROR) << "Invalid frame size.";
        return TSI_DATA_CORRUPTED;
      }
    }
    // If we do not have a full frame, return with OK status.
    if (impl->protected_sb.length < impl->parsed_frame_size) break;
    // Strips header bytes.
    grpc_slice_buffer_move_first(&impl->protected_sb,
                                 TSI_FAKE_FRAME_HEADER_SIZE, &impl->header_sb);
    // Moves data to unprotected slices.
    grpc_slice_buffer_move_first(
        &impl->protected_sb,
        impl->parsed_frame_size - TSI_FAKE_FRAME_HEADER_SIZE,
        unprotected_slices);
    impl->parsed_frame_size = 0;
    grpc_slice_buffer_reset_and_unref(&impl->header_sb);
  }
  if (min_progress_size != nullptr) {
    if (impl->parsed_frame_size > TSI_FAKE_FRAME_HEADER_SIZE) {
      *min_progress_size = impl->parsed_frame_size - impl->protected_sb.length;
    } else {
      *min_progress_size = 1;
    }
  }
  return TSI_OK;
}

static void fake_zero_copy_grpc_protector_destroy(
    tsi_zero_copy_grpc_protector* self) {
  if (self == nullptr) return;
  tsi_fake_zero_copy_grpc_protector* impl =
      reinterpret_cast<tsi_fake_zero_copy_grpc_protector*>(self);
  grpc_slice_buffer_destroy(&impl->header_sb);
  grpc_slice_buffer_destroy(&impl->protected_sb);
  gpr_free(impl);
}

static tsi_result fake_zero_copy_grpc_protector_max_frame_size(
    tsi_zero_copy_grpc_protector* self, size_t* max_frame_size) {
  if (self == nullptr || max_frame_size == nullptr) return TSI_INVALID_ARGUMENT;
  tsi_fake_zero_copy_grpc_protector* impl =
      reinterpret_cast<tsi_fake_zero_copy_grpc_protector*>(self);
  *max_frame_size = impl->max_frame_size;
  return TSI_OK;
}

static const tsi_zero_copy_grpc_protector_vtable
    zero_copy_grpc_protector_vtable = {
        fake_zero_copy_grpc_protector_protect,
        fake_zero_copy_grpc_protector_unprotect,
        fake_zero_copy_grpc_protector_destroy,
        fake_zero_copy_grpc_protector_max_frame_size,
};

// --- tsi_handshaker_result methods implementation. ---

struct fake_handshaker_result {
  tsi_handshaker_result base;
  unsigned char* unused_bytes;
  size_t unused_bytes_size;
};

static tsi_result fake_handshaker_result_extract_peer(
    const tsi_handshaker_result* /*self*/, tsi_peer* peer) {
  // Construct a tsi_peer with 1 property: certificate type, security_level.
  tsi_result result = tsi_construct_peer(2, peer);
  if (result != TSI_OK) return result;
  result = tsi_construct_string_peer_property_from_cstring(
      TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_FAKE_CERTIFICATE_TYPE,
      &peer->properties[0]);
  if (result != TSI_OK) tsi_peer_destruct(peer);
  result = tsi_construct_string_peer_property_from_cstring(
      TSI_SECURITY_LEVEL_PEER_PROPERTY,
      tsi_security_level_to_string(TSI_SECURITY_NONE), &peer->properties[1]);
  if (result != TSI_OK) tsi_peer_destruct(peer);
  return result;
}

static tsi_result fake_handshaker_result_get_frame_protector_type(
    const tsi_handshaker_result* /*self*/,
    tsi_frame_protector_type* frame_protector_type) {
  *frame_protector_type = TSI_FRAME_PROTECTOR_NORMAL_OR_ZERO_COPY;
  return TSI_OK;
}

static tsi_result fake_handshaker_result_create_zero_copy_grpc_protector(
    const tsi_handshaker_result* /*self*/,
    size_t* max_output_protected_frame_size,
    tsi_zero_copy_grpc_protector** protector) {
  *protector =
      tsi_create_fake_zero_copy_grpc_protector(max_output_protected_frame_size);
  return TSI_OK;
}

static tsi_result fake_handshaker_result_create_frame_protector(
    const tsi_handshaker_result* /*self*/,
    size_t* max_output_protected_frame_size, tsi_frame_protector** protector) {
  *protector = tsi_create_fake_frame_protector(max_output_protected_frame_size);
  return TSI_OK;
}

static tsi_result fake_handshaker_result_get_unused_bytes(
    const tsi_handshaker_result* self, const unsigned char** bytes,
    size_t* bytes_size) {
  fake_handshaker_result* result = reinterpret_cast<fake_handshaker_result*>(
      const_cast<tsi_handshaker_result*>(self));
  *bytes_size = result->unused_bytes_size;
  *bytes = result->unused_bytes;
  return TSI_OK;
}

static void fake_handshaker_result_destroy(tsi_handshaker_result* self) {
  fake_handshaker_result* result =
      reinterpret_cast<fake_handshaker_result*>(self);
  gpr_free(result->unused_bytes);
  gpr_free(self);
}

static const tsi_handshaker_result_vtable handshaker_result_vtable = {
    fake_handshaker_result_extract_peer,
    fake_handshaker_result_get_frame_protector_type,
    fake_handshaker_result_create_zero_copy_grpc_protector,
    fake_handshaker_result_create_frame_protector,
    fake_handshaker_result_get_unused_bytes,
    fake_handshaker_result_destroy,
};

static tsi_result fake_handshaker_result_create(
    const unsigned char* unused_bytes, size_t unused_bytes_size,
    tsi_handshaker_result** handshaker_result, std::string* error) {
  if ((unused_bytes_size > 0 && unused_bytes == nullptr) ||
      handshaker_result == nullptr) {
    if (error != nullptr) *error = "invalid argument";
    return TSI_INVALID_ARGUMENT;
  }
  fake_handshaker_result* result = grpc_core::Zalloc<fake_handshaker_result>();
  result->base.vtable = &handshaker_result_vtable;
  if (unused_bytes_size > 0) {
    result->unused_bytes =
        static_cast<unsigned char*>(gpr_malloc(unused_bytes_size));
    memcpy(result->unused_bytes, unused_bytes, unused_bytes_size);
  }
  result->unused_bytes_size = unused_bytes_size;
  *handshaker_result = &result->base;
  return TSI_OK;
}

// --- tsi_handshaker methods implementation. ---

static tsi_result fake_handshaker_get_bytes_to_send_to_peer(
    tsi_handshaker* self, unsigned char* bytes, size_t* bytes_size,
    std::string* error) {
  tsi_fake_handshaker* impl = reinterpret_cast<tsi_fake_handshaker*>(self);
  tsi_result result = TSI_OK;
  if (impl->needs_incoming_message || impl->result == TSI_OK) {
    *bytes_size = 0;
    return TSI_OK;
  }
  if (!impl->outgoing_frame.needs_draining) {
    tsi_fake_handshake_message next_message_to_send =
        // NOLINTNEXTLINE(bugprone-misplaced-widening-cast)
        static_cast<tsi_fake_handshake_message>(impl->next_message_to_send + 2);
    const char* msg_string =
        tsi_fake_handshake_message_to_string(impl->next_message_to_send);
    tsi_fake_frame_set_data(
        reinterpret_cast<unsigned char*>(const_cast<char*>(msg_string)),
        strlen(msg_string), &impl->outgoing_frame);
    if (next_message_to_send > TSI_FAKE_HANDSHAKE_MESSAGE_MAX) {
      next_message_to_send = TSI_FAKE_HANDSHAKE_MESSAGE_MAX;
    }
    if (GRPC_TRACE_FLAG_ENABLED(tsi)) {
      gpr_log(GPR_INFO, "%s prepared %s.",
              impl->is_client ? "Client" : "Server",
              tsi_fake_handshake_message_to_string(impl->next_message_to_send));
    }
    impl->next_message_to_send = next_message_to_send;
  }
  result =
      tsi_fake_frame_encode(bytes, bytes_size, &impl->outgoing_frame, error);
  if (result != TSI_OK) return result;
  if (!impl->is_client &&
      impl->next_message_to_send == TSI_FAKE_HANDSHAKE_MESSAGE_MAX) {
    // We're done.
    GRPC_TRACE_LOG(tsi, INFO) << "Server is done.";
    impl->result = TSI_OK;
  } else {
    impl->needs_incoming_message = 1;
  }
  return TSI_OK;
}

static tsi_result fake_handshaker_process_bytes_from_peer(
    tsi_handshaker* self, const unsigned char* bytes, size_t* bytes_size,
    std::string* error) {
  tsi_result result = TSI_OK;
  tsi_fake_handshaker* impl = reinterpret_cast<tsi_fake_handshaker*>(self);
  tsi_fake_handshake_message expected_msg =
      static_cast<tsi_fake_handshake_message>(impl->next_message_to_send - 1);
  tsi_fake_handshake_message received_msg;

  if (!impl->needs_incoming_message || impl->result == TSI_OK) {
    *bytes_size = 0;
    return TSI_OK;
  }
  result =
      tsi_fake_frame_decode(bytes, bytes_size, &impl->incoming_frame, error);
  if (result != TSI_OK) return result;

  // We now have a complete frame.
  result = tsi_fake_handshake_message_from_string(
      reinterpret_cast<const char*>(impl->incoming_frame.data) +
          TSI_FAKE_FRAME_HEADER_SIZE,
      &received_msg, error);
  if (result != TSI_OK) {
    impl->result = result;
    return result;
  }
  if (received_msg != expected_msg) {
    gpr_log(GPR_ERROR, "Invalid received message (%s instead of %s)",
            tsi_fake_handshake_message_to_string(received_msg),
            tsi_fake_handshake_message_to_string(expected_msg));
  }
  GRPC_TRACE_LOG(tsi, INFO)
      << (impl->is_client ? "Client" : "Server") << " received "
      << tsi_fake_handshake_message_to_string(received_msg);
  tsi_fake_frame_reset(&impl->incoming_frame, 0 /* needs_draining */);
  impl->needs_incoming_message = 0;
  if (impl->next_message_to_send == TSI_FAKE_HANDSHAKE_MESSAGE_MAX) {
    // We're done.
    GRPC_TRACE_LOG(tsi, INFO)
        << (impl->is_client ? "Client" : "Server") << " is done.";
    impl->result = TSI_OK;
  }
  return TSI_OK;
}

static tsi_result fake_handshaker_get_result(tsi_handshaker* self) {
  tsi_fake_handshaker* impl = reinterpret_cast<tsi_fake_handshaker*>(self);
  return impl->result;
}

static void fake_handshaker_destroy(tsi_handshaker* self) {
  tsi_fake_handshaker* impl = reinterpret_cast<tsi_fake_handshaker*>(self);
  tsi_fake_frame_destruct(&impl->incoming_frame);
  tsi_fake_frame_destruct(&impl->outgoing_frame);
  gpr_free(impl->outgoing_bytes_buffer);
  gpr_free(self);
}

static tsi_result fake_handshaker_next(
    tsi_handshaker* self, const unsigned char* received_bytes,
    size_t received_bytes_size, const unsigned char** bytes_to_send,
    size_t* bytes_to_send_size, tsi_handshaker_result** handshaker_result,
    tsi_handshaker_on_next_done_cb /*cb*/, void* /*user_data*/,
    std::string* error) {
  // Sanity check the arguments.
  if ((received_bytes_size > 0 && received_bytes == nullptr) ||
      bytes_to_send == nullptr || bytes_to_send_size == nullptr ||
      handshaker_result == nullptr) {
    if (error != nullptr) *error = "invalid argument";
    return TSI_INVALID_ARGUMENT;
  }
  tsi_fake_handshaker* handshaker =
      reinterpret_cast<tsi_fake_handshaker*>(self);
  tsi_result result = TSI_OK;

  // Decode and process a handshake frame from the peer.
  size_t consumed_bytes_size = received_bytes_size;
  if (received_bytes_size > 0) {
    result = fake_handshaker_process_bytes_from_peer(
        self, received_bytes, &consumed_bytes_size, error);
    if (result != TSI_OK) return result;
  }

  // Create a handshake message to send to the peer and encode it as a fake
  // frame.
  size_t offset = 0;
  do {
    size_t sent_bytes_size = handshaker->outgoing_bytes_buffer_size - offset;
    result = fake_handshaker_get_bytes_to_send_to_peer(
        self, handshaker->outgoing_bytes_buffer + offset, &sent_bytes_size,
        error);
    offset += sent_bytes_size;
    if (result == TSI_INCOMPLETE_DATA) {
      handshaker->outgoing_bytes_buffer_size *= 2;
      handshaker->outgoing_bytes_buffer = static_cast<unsigned char*>(
          gpr_realloc(handshaker->outgoing_bytes_buffer,
                      handshaker->outgoing_bytes_buffer_size));
    }
  } while (result == TSI_INCOMPLETE_DATA);
  if (result != TSI_OK) return result;
  *bytes_to_send = handshaker->outgoing_bytes_buffer;
  *bytes_to_send_size = offset;

  // Check if the handshake was completed.
  if (fake_handshaker_get_result(self) == TSI_HANDSHAKE_IN_PROGRESS) {
    *handshaker_result = nullptr;
  } else {
    // Calculate the unused bytes.
    const unsigned char* unused_bytes = nullptr;
    size_t unused_bytes_size = received_bytes_size - consumed_bytes_size;
    if (unused_bytes_size > 0) {
      unused_bytes = received_bytes + consumed_bytes_size;
    }

    // Create a handshaker_result containing the unused bytes.
    result = fake_handshaker_result_create(unused_bytes, unused_bytes_size,
                                           handshaker_result, error);
    if (result == TSI_OK) {
      // Indicate that the handshake has completed and that a handshaker_result
      // has been created.
      self->handshaker_result_created = true;
    }
  }
  return result;
}

static const tsi_handshaker_vtable handshaker_vtable = {
    nullptr,  // get_bytes_to_send_to_peer -- deprecated
    nullptr,  // process_bytes_from_peer   -- deprecated
    nullptr,  // get_result                -- deprecated
    nullptr,  // extract_peer              -- deprecated
    nullptr,  // create_frame_protector    -- deprecated
    fake_handshaker_destroy,
    fake_handshaker_next,
    nullptr,  // shutdown
};

tsi_handshaker* tsi_create_fake_handshaker(int is_client) {
  tsi_fake_handshaker* impl = grpc_core::Zalloc<tsi_fake_handshaker>();
  impl->base.vtable = &handshaker_vtable;
  impl->is_client = is_client;
  impl->result = TSI_HANDSHAKE_IN_PROGRESS;
  impl->outgoing_bytes_buffer_size =
      TSI_FAKE_HANDSHAKER_OUTGOING_BUFFER_INITIAL_SIZE;
  impl->outgoing_bytes_buffer =
      static_cast<unsigned char*>(gpr_malloc(impl->outgoing_bytes_buffer_size));
  if (is_client) {
    impl->needs_incoming_message = 0;
    impl->next_message_to_send = TSI_FAKE_CLIENT_INIT;
  } else {
    impl->needs_incoming_message = 1;
    impl->next_message_to_send = TSI_FAKE_SERVER_INIT;
  }
  return &impl->base;
}

tsi_frame_protector* tsi_create_fake_frame_protector(
    size_t* max_protected_frame_size) {
  tsi_fake_frame_protector* impl =
      grpc_core::Zalloc<tsi_fake_frame_protector>();
  impl->max_frame_size = (max_protected_frame_size == nullptr)
                             ? TSI_FAKE_DEFAULT_FRAME_SIZE
                             : *max_protected_frame_size;
  impl->base.vtable = &frame_protector_vtable;
  return &impl->base;
}

tsi_zero_copy_grpc_protector* tsi_create_fake_zero_copy_grpc_protector(
    size_t* max_protected_frame_size) {
  tsi_fake_zero_copy_grpc_protector* impl =
      static_cast<tsi_fake_zero_copy_grpc_protector*>(
          gpr_zalloc(sizeof(*impl)));
  grpc_slice_buffer_init(&impl->header_sb);
  grpc_slice_buffer_init(&impl->protected_sb);
  impl->max_frame_size = (max_protected_frame_size == nullptr)
                             ? TSI_FAKE_DEFAULT_FRAME_SIZE
                             : *max_protected_frame_size;
  impl->parsed_frame_size = 0;
  impl->base.vtable = &zero_copy_grpc_protector_vtable;
  return &impl->base;
}
