/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/lib/tsi/fake_transport_security.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>
#include "src/core/lib/tsi/transport_security.h"

/* --- Constants. ---*/
#define TSI_FAKE_FRAME_HEADER_SIZE 4
#define TSI_FAKE_FRAME_INITIAL_ALLOCATED_SIZE 64
#define TSI_FAKE_DEFAULT_FRAME_SIZE 16384

/* --- Structure definitions. ---*/

/* a frame is encoded like this:
   | size |     data    |
   where the size field value is the size of the size field plus the size of
   the data encoded in little endian on 4 bytes.  */
typedef struct {
  unsigned char *data;
  size_t size;
  size_t allocated_size;
  size_t offset;
  int needs_draining;
} tsi_fake_frame;

typedef enum {
  TSI_FAKE_CLIENT_INIT = 0,
  TSI_FAKE_SERVER_INIT = 1,
  TSI_FAKE_CLIENT_FINISHED = 2,
  TSI_FAKE_SERVER_FINISHED = 3,
  TSI_FAKE_HANDSHAKE_MESSAGE_MAX = 4
} tsi_fake_handshake_message;

typedef struct {
  tsi_handshaker base;
  int is_client;
  tsi_fake_handshake_message next_message_to_send;
  int needs_incoming_message;
  tsi_fake_frame incoming;
  tsi_fake_frame outgoing;
  tsi_result result;
} tsi_fake_handshaker;

typedef struct {
  tsi_frame_protector base;
  tsi_fake_frame protect_frame;
  tsi_fake_frame unprotect_frame;
  size_t max_frame_size;
} tsi_fake_frame_protector;

/* --- Utils. ---*/

static const char *tsi_fake_handshake_message_strings[] = {
    "CLIENT_INIT", "SERVER_INIT", "CLIENT_FINISHED", "SERVER_FINISHED"};

static const char *tsi_fake_handshake_message_to_string(int msg) {
  if (msg < 0 || msg >= TSI_FAKE_HANDSHAKE_MESSAGE_MAX) {
    gpr_log(GPR_ERROR, "Invalid message %d", msg);
    return "UNKNOWN";
  }
  return tsi_fake_handshake_message_strings[msg];
}

static tsi_result tsi_fake_handshake_message_from_string(
    const char *msg_string, tsi_fake_handshake_message *msg) {
  tsi_fake_handshake_message i;
  for (i = 0; i < TSI_FAKE_HANDSHAKE_MESSAGE_MAX; i++) {
    if (strncmp(msg_string, tsi_fake_handshake_message_strings[i],
                strlen(tsi_fake_handshake_message_strings[i])) == 0) {
      *msg = i;
      return TSI_OK;
    }
  }
  gpr_log(GPR_ERROR, "Invalid handshake message.");
  return TSI_DATA_CORRUPTED;
}

static uint32_t load32_little_endian(const unsigned char *buf) {
  return ((uint32_t)(buf[0]) | (uint32_t)(buf[1] << 8) |
          (uint32_t)(buf[2] << 16) | (uint32_t)(buf[3] << 24));
}

static void store32_little_endian(uint32_t value, unsigned char *buf) {
  buf[3] = (unsigned char)((value >> 24) & 0xFF);
  buf[2] = (unsigned char)((value >> 16) & 0xFF);
  buf[1] = (unsigned char)((value >> 8) & 0xFF);
  buf[0] = (unsigned char)((value)&0xFF);
}

static void tsi_fake_frame_reset(tsi_fake_frame *frame, int needs_draining) {
  frame->offset = 0;
  frame->needs_draining = needs_draining;
  if (!needs_draining) frame->size = 0;
}

/* Returns 1 if successful, 0 otherwise. */
static int tsi_fake_frame_ensure_size(tsi_fake_frame *frame) {
  if (frame->data == NULL) {
    frame->allocated_size = frame->size;
    frame->data = gpr_malloc(frame->allocated_size);
    if (frame->data == NULL) return 0;
  } else if (frame->size > frame->allocated_size) {
    unsigned char *new_data = gpr_realloc(frame->data, frame->size);
    if (new_data == NULL) {
      gpr_free(frame->data);
      frame->data = NULL;
      return 0;
    }
    frame->data = new_data;
    frame->allocated_size = frame->size;
  }
  return 1;
}

/* This method should not be called if frame->needs_framing is not 0.  */
static tsi_result fill_frame_from_bytes(const unsigned char *incoming_bytes,
                                        size_t *incoming_bytes_size,
                                        tsi_fake_frame *frame) {
  size_t available_size = *incoming_bytes_size;
  size_t to_read_size = 0;
  const unsigned char *bytes_cursor = incoming_bytes;

  if (frame->needs_draining) return TSI_INTERNAL_ERROR;
  if (frame->data == NULL) {
    frame->allocated_size = TSI_FAKE_FRAME_INITIAL_ALLOCATED_SIZE;
    frame->data = gpr_malloc(frame->allocated_size);
    if (frame->data == NULL) return TSI_OUT_OF_RESOURCES;
  }

  if (frame->offset < TSI_FAKE_FRAME_HEADER_SIZE) {
    to_read_size = TSI_FAKE_FRAME_HEADER_SIZE - frame->offset;
    if (to_read_size > available_size) {
      /* Just fill what we can and exit. */
      memcpy(frame->data + frame->offset, bytes_cursor, available_size);
      bytes_cursor += available_size;
      frame->offset += available_size;
      *incoming_bytes_size = (size_t)(bytes_cursor - incoming_bytes);
      return TSI_INCOMPLETE_DATA;
    }
    memcpy(frame->data + frame->offset, bytes_cursor, to_read_size);
    bytes_cursor += to_read_size;
    frame->offset += to_read_size;
    available_size -= to_read_size;
    frame->size = load32_little_endian(frame->data);
    if (!tsi_fake_frame_ensure_size(frame)) return TSI_OUT_OF_RESOURCES;
  }

  to_read_size = frame->size - frame->offset;
  if (to_read_size > available_size) {
    memcpy(frame->data + frame->offset, bytes_cursor, available_size);
    frame->offset += available_size;
    bytes_cursor += available_size;
    *incoming_bytes_size = (size_t)(bytes_cursor - incoming_bytes);
    return TSI_INCOMPLETE_DATA;
  }
  memcpy(frame->data + frame->offset, bytes_cursor, to_read_size);
  bytes_cursor += to_read_size;
  *incoming_bytes_size = (size_t)(bytes_cursor - incoming_bytes);
  tsi_fake_frame_reset(frame, 1 /* needs_draining */);
  return TSI_OK;
}

/* This method should not be called if frame->needs_framing is 0.  */
static tsi_result drain_frame_to_bytes(unsigned char *outgoing_bytes,
                                       size_t *outgoing_bytes_size,
                                       tsi_fake_frame *frame) {
  size_t to_write_size = frame->size - frame->offset;
  if (!frame->needs_draining) return TSI_INTERNAL_ERROR;
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

static tsi_result bytes_to_frame(unsigned char *bytes, size_t bytes_size,
                                 tsi_fake_frame *frame) {
  frame->offset = 0;
  frame->size = bytes_size + TSI_FAKE_FRAME_HEADER_SIZE;
  if (!tsi_fake_frame_ensure_size(frame)) return TSI_OUT_OF_RESOURCES;
  store32_little_endian((uint32_t)frame->size, frame->data);
  memcpy(frame->data + TSI_FAKE_FRAME_HEADER_SIZE, bytes, bytes_size);
  tsi_fake_frame_reset(frame, 1 /* needs draining */);
  return TSI_OK;
}

static void tsi_fake_frame_destruct(tsi_fake_frame *frame) {
  if (frame->data != NULL) gpr_free(frame->data);
}

/* --- tsi_frame_protector methods implementation. ---*/

static tsi_result fake_protector_protect(tsi_frame_protector *self,
                                         const unsigned char *unprotected_bytes,
                                         size_t *unprotected_bytes_size,
                                         unsigned char *protected_output_frames,
                                         size_t *protected_output_frames_size) {
  tsi_result result = TSI_OK;
  tsi_fake_frame_protector *impl = (tsi_fake_frame_protector *)self;
  unsigned char frame_header[TSI_FAKE_FRAME_HEADER_SIZE];
  tsi_fake_frame *frame = &impl->protect_frame;
  size_t saved_output_size = *protected_output_frames_size;
  size_t drained_size = 0;
  size_t *num_bytes_written = protected_output_frames_size;
  *num_bytes_written = 0;

  /* Try to drain first. */
  if (frame->needs_draining) {
    drained_size = saved_output_size - *num_bytes_written;
    result =
        drain_frame_to_bytes(protected_output_frames, &drained_size, frame);
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

  /* Now process the unprotected_bytes. */
  if (frame->needs_draining) return TSI_INTERNAL_ERROR;
  if (frame->size == 0) {
    /* New frame, create a header. */
    size_t written_in_frame_size = 0;
    store32_little_endian((uint32_t)impl->max_frame_size, frame_header);
    written_in_frame_size = TSI_FAKE_FRAME_HEADER_SIZE;
    result = fill_frame_from_bytes(frame_header, &written_in_frame_size, frame);
    if (result != TSI_INCOMPLETE_DATA) {
      gpr_log(GPR_ERROR, "fill_frame_from_bytes returned %s",
              tsi_result_to_string(result));
      return result;
    }
  }
  result =
      fill_frame_from_bytes(unprotected_bytes, unprotected_bytes_size, frame);
  if (result != TSI_OK) {
    if (result == TSI_INCOMPLETE_DATA) result = TSI_OK;
    return result;
  }

  /* Try to drain again. */
  if (!frame->needs_draining) return TSI_INTERNAL_ERROR;
  if (frame->offset != 0) return TSI_INTERNAL_ERROR;
  drained_size = saved_output_size - *num_bytes_written;
  result = drain_frame_to_bytes(protected_output_frames, &drained_size, frame);
  *num_bytes_written += drained_size;
  if (result == TSI_INCOMPLETE_DATA) result = TSI_OK;
  return result;
}

static tsi_result fake_protector_protect_flush(
    tsi_frame_protector *self, unsigned char *protected_output_frames,
    size_t *protected_output_frames_size, size_t *still_pending_size) {
  tsi_result result = TSI_OK;
  tsi_fake_frame_protector *impl = (tsi_fake_frame_protector *)self;
  tsi_fake_frame *frame = &impl->protect_frame;
  if (!frame->needs_draining) {
    /* Create a short frame. */
    frame->size = frame->offset;
    frame->offset = 0;
    frame->needs_draining = 1;
    store32_little_endian((uint32_t)frame->size,
                          frame->data); /* Overwrite header. */
  }
  result = drain_frame_to_bytes(protected_output_frames,
                                protected_output_frames_size, frame);
  if (result == TSI_INCOMPLETE_DATA) result = TSI_OK;
  *still_pending_size = frame->size - frame->offset;
  return result;
}

static tsi_result fake_protector_unprotect(
    tsi_frame_protector *self, const unsigned char *protected_frames_bytes,
    size_t *protected_frames_bytes_size, unsigned char *unprotected_bytes,
    size_t *unprotected_bytes_size) {
  tsi_result result = TSI_OK;
  tsi_fake_frame_protector *impl = (tsi_fake_frame_protector *)self;
  tsi_fake_frame *frame = &impl->unprotect_frame;
  size_t saved_output_size = *unprotected_bytes_size;
  size_t drained_size = 0;
  size_t *num_bytes_written = unprotected_bytes_size;
  *num_bytes_written = 0;

  /* Try to drain first. */
  if (frame->needs_draining) {
    /* Go past the header if needed. */
    if (frame->offset == 0) frame->offset = TSI_FAKE_FRAME_HEADER_SIZE;
    drained_size = saved_output_size - *num_bytes_written;
    result = drain_frame_to_bytes(unprotected_bytes, &drained_size, frame);
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

  /* Now process the protected_bytes. */
  if (frame->needs_draining) return TSI_INTERNAL_ERROR;
  result = fill_frame_from_bytes(protected_frames_bytes,
                                 protected_frames_bytes_size, frame);
  if (result != TSI_OK) {
    if (result == TSI_INCOMPLETE_DATA) result = TSI_OK;
    return result;
  }

  /* Try to drain again. */
  if (!frame->needs_draining) return TSI_INTERNAL_ERROR;
  if (frame->offset != 0) return TSI_INTERNAL_ERROR;
  frame->offset = TSI_FAKE_FRAME_HEADER_SIZE; /* Go past the header. */
  drained_size = saved_output_size - *num_bytes_written;
  result = drain_frame_to_bytes(unprotected_bytes, &drained_size, frame);
  *num_bytes_written += drained_size;
  if (result == TSI_INCOMPLETE_DATA) result = TSI_OK;
  return result;
}

static void fake_protector_destroy(tsi_frame_protector *self) {
  tsi_fake_frame_protector *impl = (tsi_fake_frame_protector *)self;
  tsi_fake_frame_destruct(&impl->protect_frame);
  tsi_fake_frame_destruct(&impl->unprotect_frame);
  gpr_free(self);
}

static const tsi_frame_protector_vtable frame_protector_vtable = {
    fake_protector_protect, fake_protector_protect_flush,
    fake_protector_unprotect, fake_protector_destroy,
};

/* --- tsi_handshaker methods implementation. ---*/

static tsi_result fake_handshaker_get_bytes_to_send_to_peer(
    tsi_handshaker *self, unsigned char *bytes, size_t *bytes_size) {
  tsi_fake_handshaker *impl = (tsi_fake_handshaker *)self;
  tsi_result result = TSI_OK;
  if (impl->needs_incoming_message || impl->result == TSI_OK) {
    *bytes_size = 0;
    return TSI_OK;
  }
  if (!impl->outgoing.needs_draining) {
    tsi_fake_handshake_message next_message_to_send =
        impl->next_message_to_send + 2;
    const char *msg_string =
        tsi_fake_handshake_message_to_string(impl->next_message_to_send);
    result = bytes_to_frame((unsigned char *)msg_string, strlen(msg_string),
                            &impl->outgoing);
    if (result != TSI_OK) return result;
    if (next_message_to_send > TSI_FAKE_HANDSHAKE_MESSAGE_MAX) {
      next_message_to_send = TSI_FAKE_HANDSHAKE_MESSAGE_MAX;
    }
    if (tsi_tracing_enabled) {
      gpr_log(GPR_INFO, "%s prepared %s.",
              impl->is_client ? "Client" : "Server",
              tsi_fake_handshake_message_to_string(impl->next_message_to_send));
    }
    impl->next_message_to_send = next_message_to_send;
  }
  result = drain_frame_to_bytes(bytes, bytes_size, &impl->outgoing);
  if (result != TSI_OK) return result;
  if (!impl->is_client &&
      impl->next_message_to_send == TSI_FAKE_HANDSHAKE_MESSAGE_MAX) {
    /* We're done. */
    if (tsi_tracing_enabled) {
      gpr_log(GPR_INFO, "Server is done.");
    }
    impl->result = TSI_OK;
  } else {
    impl->needs_incoming_message = 1;
  }
  return TSI_OK;
}

static tsi_result fake_handshaker_process_bytes_from_peer(
    tsi_handshaker *self, const unsigned char *bytes, size_t *bytes_size) {
  tsi_result result = TSI_OK;
  tsi_fake_handshaker *impl = (tsi_fake_handshaker *)self;
  tsi_fake_handshake_message expected_msg = impl->next_message_to_send - 1;
  tsi_fake_handshake_message received_msg;

  if (!impl->needs_incoming_message || impl->result == TSI_OK) {
    *bytes_size = 0;
    return TSI_OK;
  }
  result = fill_frame_from_bytes(bytes, bytes_size, &impl->incoming);
  if (result != TSI_OK) return result;

  /* We now have a complete frame. */
  result = tsi_fake_handshake_message_from_string(
      (const char *)impl->incoming.data + TSI_FAKE_FRAME_HEADER_SIZE,
      &received_msg);
  if (result != TSI_OK) {
    impl->result = result;
    return result;
  }
  if (received_msg != expected_msg) {
    gpr_log(GPR_ERROR, "Invalid received message (%s instead of %s)",
            tsi_fake_handshake_message_to_string(received_msg),
            tsi_fake_handshake_message_to_string(expected_msg));
  }
  if (tsi_tracing_enabled) {
    gpr_log(GPR_INFO, "%s received %s.", impl->is_client ? "Client" : "Server",
            tsi_fake_handshake_message_to_string(received_msg));
  }
  tsi_fake_frame_reset(&impl->incoming, 0 /* needs_draining */);
  impl->needs_incoming_message = 0;
  if (impl->next_message_to_send == TSI_FAKE_HANDSHAKE_MESSAGE_MAX) {
    /* We're done. */
    if (tsi_tracing_enabled) {
      gpr_log(GPR_INFO, "%s is done.", impl->is_client ? "Client" : "Server");
    }
    impl->result = TSI_OK;
  }
  return TSI_OK;
}

static tsi_result fake_handshaker_get_result(tsi_handshaker *self) {
  tsi_fake_handshaker *impl = (tsi_fake_handshaker *)self;
  return impl->result;
}

static tsi_result fake_handshaker_extract_peer(tsi_handshaker *self,
                                               tsi_peer *peer) {
  tsi_result result = tsi_construct_peer(1, peer);
  if (result != TSI_OK) return result;
  result = tsi_construct_string_peer_property_from_cstring(
      TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_FAKE_CERTIFICATE_TYPE,
      &peer->properties[0]);
  if (result != TSI_OK) tsi_peer_destruct(peer);
  return result;
}

static tsi_result fake_handshaker_create_frame_protector(
    tsi_handshaker *self, size_t *max_protected_frame_size,
    tsi_frame_protector **protector) {
  *protector = tsi_create_fake_protector(max_protected_frame_size);
  if (*protector == NULL) return TSI_OUT_OF_RESOURCES;
  return TSI_OK;
}

static void fake_handshaker_destroy(tsi_handshaker *self) {
  tsi_fake_handshaker *impl = (tsi_fake_handshaker *)self;
  tsi_fake_frame_destruct(&impl->incoming);
  tsi_fake_frame_destruct(&impl->outgoing);
  gpr_free(self);
}

static const tsi_handshaker_vtable handshaker_vtable = {
    fake_handshaker_get_bytes_to_send_to_peer,
    fake_handshaker_process_bytes_from_peer,
    fake_handshaker_get_result,
    fake_handshaker_extract_peer,
    fake_handshaker_create_frame_protector,
    fake_handshaker_destroy,
};

tsi_handshaker *tsi_create_fake_handshaker(int is_client) {
  tsi_fake_handshaker *impl = gpr_zalloc(sizeof(*impl));
  impl->base.vtable = &handshaker_vtable;
  impl->is_client = is_client;
  impl->result = TSI_HANDSHAKE_IN_PROGRESS;
  if (is_client) {
    impl->needs_incoming_message = 0;
    impl->next_message_to_send = TSI_FAKE_CLIENT_INIT;
  } else {
    impl->needs_incoming_message = 1;
    impl->next_message_to_send = TSI_FAKE_SERVER_INIT;
  }
  return &impl->base;
}

tsi_frame_protector *tsi_create_fake_protector(
    size_t *max_protected_frame_size) {
  tsi_fake_frame_protector *impl = gpr_zalloc(sizeof(*impl));
  impl->max_frame_size = (max_protected_frame_size == NULL)
                             ? TSI_FAKE_DEFAULT_FRAME_SIZE
                             : *max_protected_frame_size;
  impl->base.vtable = &frame_protector_vtable;
  return &impl->base;
}
