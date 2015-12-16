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

#include "src/core/tsi/transport_security.h"

#include <stdlib.h>
#include <string.h>

/* --- Tracing. --- */

int tsi_tracing_enabled = 0;

/* --- Utils. --- */

char *tsi_strdup(const char *src) {
  char *dst;
  size_t len;
  if (!src) return NULL;
  len = strlen(src) + 1;
  dst = malloc(len);
  if (!dst) return NULL;
  memcpy(dst, src, len);
  return dst;
}

/* --- tsi_result common implementation. --- */

const char *tsi_result_to_string(tsi_result result) {
  switch (result) {
    case TSI_OK:
      return "TSI_OK";
    case TSI_UNKNOWN_ERROR:
      return "TSI_UNKNOWN_ERROR";
    case TSI_INVALID_ARGUMENT:
      return "TSI_INVALID_ARGUMENT";
    case TSI_PERMISSION_DENIED:
      return "TSI_PERMISSION_DENIED";
    case TSI_INCOMPLETE_DATA:
      return "TSI_INCOMPLETE_DATA";
    case TSI_FAILED_PRECONDITION:
      return "TSI_FAILED_PRECONDITION";
    case TSI_UNIMPLEMENTED:
      return "TSI_UNIMPLEMENTED";
    case TSI_INTERNAL_ERROR:
      return "TSI_INTERNAL_ERROR";
    case TSI_DATA_CORRUPTED:
      return "TSI_DATA_CORRUPTED";
    case TSI_NOT_FOUND:
      return "TSI_NOT_FOUND";
    case TSI_PROTOCOL_FAILURE:
      return "TSI_PROTOCOL_FAILURE";
    case TSI_HANDSHAKE_IN_PROGRESS:
      return "TSI_HANDSHAKE_IN_PROGRESS";
    case TSI_OUT_OF_RESOURCES:
      return "TSI_OUT_OF_RESOURCES";
    default:
      return "UNKNOWN";
  }
}

/* --- tsi_frame_protector common implementation. ---

   Calls specific implementation after state/input validation. */

tsi_result tsi_frame_protector_protect(tsi_frame_protector *self,
                                       const unsigned char *unprotected_bytes,
                                       size_t *unprotected_bytes_size,
                                       unsigned char *protected_output_frames,
                                       size_t *protected_output_frames_size) {
  if (self == NULL || unprotected_bytes == NULL ||
      unprotected_bytes_size == NULL || protected_output_frames == NULL ||
      protected_output_frames_size == NULL) {
    return TSI_INVALID_ARGUMENT;
  }
  return self->vtable->protect(self, unprotected_bytes, unprotected_bytes_size,
                               protected_output_frames,
                               protected_output_frames_size);
}

tsi_result tsi_frame_protector_protect_flush(
    tsi_frame_protector *self, unsigned char *protected_output_frames,
    size_t *protected_output_frames_size, size_t *still_pending_size) {
  if (self == NULL || protected_output_frames == NULL ||
      protected_output_frames == NULL || still_pending_size == NULL) {
    return TSI_INVALID_ARGUMENT;
  }
  return self->vtable->protect_flush(self, protected_output_frames,
                                     protected_output_frames_size,
                                     still_pending_size);
}

tsi_result tsi_frame_protector_unprotect(
    tsi_frame_protector *self, const unsigned char *protected_frames_bytes,
    size_t *protected_frames_bytes_size, unsigned char *unprotected_bytes,
    size_t *unprotected_bytes_size) {
  if (self == NULL || protected_frames_bytes == NULL ||
      protected_frames_bytes_size == NULL || unprotected_bytes == NULL ||
      unprotected_bytes_size == NULL) {
    return TSI_INVALID_ARGUMENT;
  }
  return self->vtable->unprotect(self, protected_frames_bytes,
                                 protected_frames_bytes_size, unprotected_bytes,
                                 unprotected_bytes_size);
}

void tsi_frame_protector_destroy(tsi_frame_protector *self) {
  if (self == NULL) return;
  self->vtable->destroy(self);
}

/* --- tsi_handshaker common implementation. ---

   Calls specific implementation after state/input validation. */

tsi_result tsi_handshaker_get_bytes_to_send_to_peer(tsi_handshaker *self,
                                                    unsigned char *bytes,
                                                    size_t *bytes_size) {
  if (self == NULL || bytes == NULL || bytes_size == NULL) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->frame_protector_created) return TSI_FAILED_PRECONDITION;
  return self->vtable->get_bytes_to_send_to_peer(self, bytes, bytes_size);
}

tsi_result tsi_handshaker_process_bytes_from_peer(tsi_handshaker *self,
                                                  const unsigned char *bytes,
                                                  size_t *bytes_size) {
  if (self == NULL || bytes == NULL || bytes_size == NULL) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->frame_protector_created) return TSI_FAILED_PRECONDITION;
  return self->vtable->process_bytes_from_peer(self, bytes, bytes_size);
}

tsi_result tsi_handshaker_get_result(tsi_handshaker *self) {
  if (self == NULL) return TSI_INVALID_ARGUMENT;
  if (self->frame_protector_created) return TSI_FAILED_PRECONDITION;
  return self->vtable->get_result(self);
}

tsi_result tsi_handshaker_extract_peer(tsi_handshaker *self, tsi_peer *peer) {
  if (self == NULL || peer == NULL) return TSI_INVALID_ARGUMENT;
  memset(peer, 0, sizeof(tsi_peer));
  if (self->frame_protector_created) return TSI_FAILED_PRECONDITION;
  if (tsi_handshaker_get_result(self) != TSI_OK) {
    return TSI_FAILED_PRECONDITION;
  }
  return self->vtable->extract_peer(self, peer);
}

tsi_result tsi_handshaker_create_frame_protector(
    tsi_handshaker *self, size_t *max_protected_frame_size,
    tsi_frame_protector **protector) {
  tsi_result result;
  if (self == NULL || protector == NULL) return TSI_INVALID_ARGUMENT;
  if (self->frame_protector_created) return TSI_FAILED_PRECONDITION;
  if (tsi_handshaker_get_result(self) != TSI_OK) {
    return TSI_FAILED_PRECONDITION;
  }
  result = self->vtable->create_frame_protector(self, max_protected_frame_size,
                                                protector);
  if (result == TSI_OK) {
    self->frame_protector_created = 1;
  }
  return result;
}

void tsi_handshaker_destroy(tsi_handshaker *self) {
  if (self == NULL) return;
  self->vtable->destroy(self);
}

/* --- tsi_peer implementation. --- */

tsi_peer_property tsi_init_peer_property(void) {
  tsi_peer_property property;
  memset(&property, 0, sizeof(tsi_peer_property));
  return property;
}

static void tsi_peer_destroy_list_property(tsi_peer_property *children,
                                           size_t child_count) {
  size_t i;
  for (i = 0; i < child_count; i++) {
    tsi_peer_property_destruct(&children[i]);
  }
  free(children);
}

void tsi_peer_property_destruct(tsi_peer_property *property) {
  if (property->name != NULL) {
    free(property->name);
  }
  if (property->value.data != NULL) {
    free(property->value.data);
  }
  *property = tsi_init_peer_property(); /* Reset everything to 0. */
}

void tsi_peer_destruct(tsi_peer *self) {
  if (self == NULL) return;
  if (self->properties != NULL) {
    tsi_peer_destroy_list_property(self->properties, self->property_count);
    self->properties = NULL;
  }
  self->property_count = 0;
}

tsi_result tsi_construct_allocated_string_peer_property(
    const char *name, size_t value_length, tsi_peer_property *property) {
  *property = tsi_init_peer_property();
  if (name != NULL) {
    property->name = tsi_strdup(name);
    if (property->name == NULL) return TSI_OUT_OF_RESOURCES;
  }
  if (value_length > 0) {
    property->value.data = calloc(1, value_length);
    if (property->value.data == NULL) {
      tsi_peer_property_destruct(property);
      return TSI_OUT_OF_RESOURCES;
    }
    property->value.length = value_length;
  }
  return TSI_OK;
}

tsi_result tsi_construct_string_peer_property_from_cstring(
    const char *name, const char *value, tsi_peer_property *property) {
  return tsi_construct_string_peer_property(name, value, strlen(value),
                                            property);
}

tsi_result tsi_construct_string_peer_property(const char *name,
                                              const char *value,
                                              size_t value_length,
                                              tsi_peer_property *property) {
  tsi_result result = tsi_construct_allocated_string_peer_property(
      name, value_length, property);
  if (result != TSI_OK) return result;
  if (value_length > 0) {
    memcpy(property->value.data, value, value_length);
  }
  return TSI_OK;
}

tsi_result tsi_construct_peer(size_t property_count, tsi_peer *peer) {
  memset(peer, 0, sizeof(tsi_peer));
  if (property_count > 0) {
    peer->properties = calloc(property_count, sizeof(tsi_peer_property));
    if (peer->properties == NULL) return TSI_OUT_OF_RESOURCES;
    peer->property_count = property_count;
  }
  return TSI_OK;
}
