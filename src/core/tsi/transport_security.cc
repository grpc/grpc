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

#include <grpc/support/port_platform.h>

#include "src/core/tsi/transport_security.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include <stdlib.h>
#include <string.h>

/* --- Tracing. --- */

grpc_core::TraceFlag tsi_tracing_enabled(false, "tsi");

/* --- tsi_result common implementation. --- */

const char* tsi_result_to_string(tsi_result result) {
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
    case TSI_ASYNC:
      return "TSI_ASYNC";
    default:
      return "UNKNOWN";
  }
}

const char* tsi_security_level_to_string(tsi_security_level security_level) {
  switch (security_level) {
    case TSI_SECURITY_NONE:
      return "TSI_SECURITY_NONE";
    case TSI_INTEGRITY_ONLY:
      return "TSI_INTEGRITY_ONLY";
    case TSI_PRIVACY_AND_INTEGRITY:
      return "TSI_PRIVACY_AND_INTEGRITY";
    default:
      return "UNKNOWN";
  }
}

/* --- tsi_frame_protector common implementation. ---

   Calls specific implementation after state/input validation. */

tsi_result tsi_frame_protector_protect(tsi_frame_protector* self,
                                       const unsigned char* unprotected_bytes,
                                       size_t* unprotected_bytes_size,
                                       unsigned char* protected_output_frames,
                                       size_t* protected_output_frames_size) {
  if (self == nullptr || self->vtable == nullptr ||
      unprotected_bytes == nullptr || unprotected_bytes_size == nullptr ||
      protected_output_frames == nullptr ||
      protected_output_frames_size == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->vtable->protect == nullptr) return TSI_UNIMPLEMENTED;
  return self->vtable->protect(self, unprotected_bytes, unprotected_bytes_size,
                               protected_output_frames,
                               protected_output_frames_size);
}

tsi_result tsi_frame_protector_protect_flush(
    tsi_frame_protector* self, unsigned char* protected_output_frames,
    size_t* protected_output_frames_size, size_t* still_pending_size) {
  if (self == nullptr || self->vtable == nullptr ||
      protected_output_frames == nullptr ||
      protected_output_frames_size == nullptr ||
      still_pending_size == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->vtable->protect_flush == nullptr) return TSI_UNIMPLEMENTED;
  return self->vtable->protect_flush(self, protected_output_frames,
                                     protected_output_frames_size,
                                     still_pending_size);
}

tsi_result tsi_frame_protector_unprotect(
    tsi_frame_protector* self, const unsigned char* protected_frames_bytes,
    size_t* protected_frames_bytes_size, unsigned char* unprotected_bytes,
    size_t* unprotected_bytes_size) {
  if (self == nullptr || self->vtable == nullptr ||
      protected_frames_bytes == nullptr ||
      protected_frames_bytes_size == nullptr || unprotected_bytes == nullptr ||
      unprotected_bytes_size == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->vtable->unprotect == nullptr) return TSI_UNIMPLEMENTED;
  return self->vtable->unprotect(self, protected_frames_bytes,
                                 protected_frames_bytes_size, unprotected_bytes,
                                 unprotected_bytes_size);
}

void tsi_frame_protector_destroy(tsi_frame_protector* self) {
  if (self == nullptr) return;
  self->vtable->destroy(self);
}

/* --- tsi_handshaker common implementation. ---

   Calls specific implementation after state/input validation. */

tsi_result tsi_handshaker_get_bytes_to_send_to_peer(tsi_handshaker* self,
                                                    unsigned char* bytes,
                                                    size_t* bytes_size) {
  if (self == nullptr || self->vtable == nullptr || bytes == nullptr ||
      bytes_size == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->frame_protector_created) return TSI_FAILED_PRECONDITION;
  if (self->handshake_shutdown) return TSI_HANDSHAKE_SHUTDOWN;
  if (self->vtable->get_bytes_to_send_to_peer == nullptr) {
    return TSI_UNIMPLEMENTED;
  }
  return self->vtable->get_bytes_to_send_to_peer(self, bytes, bytes_size);
}

tsi_result tsi_handshaker_process_bytes_from_peer(tsi_handshaker* self,
                                                  const unsigned char* bytes,
                                                  size_t* bytes_size) {
  if (self == nullptr || self->vtable == nullptr || bytes == nullptr ||
      bytes_size == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->frame_protector_created) return TSI_FAILED_PRECONDITION;
  if (self->handshake_shutdown) return TSI_HANDSHAKE_SHUTDOWN;
  if (self->vtable->process_bytes_from_peer == nullptr) {
    return TSI_UNIMPLEMENTED;
  }
  return self->vtable->process_bytes_from_peer(self, bytes, bytes_size);
}

tsi_result tsi_handshaker_get_result(tsi_handshaker* self) {
  if (self == nullptr || self->vtable == nullptr) return TSI_INVALID_ARGUMENT;
  if (self->frame_protector_created) return TSI_FAILED_PRECONDITION;
  if (self->handshake_shutdown) return TSI_HANDSHAKE_SHUTDOWN;
  if (self->vtable->get_result == nullptr) return TSI_UNIMPLEMENTED;
  return self->vtable->get_result(self);
}

tsi_result tsi_handshaker_extract_peer(tsi_handshaker* self, tsi_peer* peer) {
  if (self == nullptr || self->vtable == nullptr || peer == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  memset(peer, 0, sizeof(tsi_peer));
  if (self->frame_protector_created) return TSI_FAILED_PRECONDITION;
  if (self->handshake_shutdown) return TSI_HANDSHAKE_SHUTDOWN;
  if (tsi_handshaker_get_result(self) != TSI_OK) {
    return TSI_FAILED_PRECONDITION;
  }
  if (self->vtable->extract_peer == nullptr) return TSI_UNIMPLEMENTED;
  return self->vtable->extract_peer(self, peer);
}

tsi_result tsi_handshaker_create_frame_protector(
    tsi_handshaker* self, size_t* max_output_protected_frame_size,
    tsi_frame_protector** protector) {
  tsi_result result;
  if (self == nullptr || self->vtable == nullptr || protector == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->frame_protector_created) return TSI_FAILED_PRECONDITION;
  if (self->handshake_shutdown) return TSI_HANDSHAKE_SHUTDOWN;
  if (tsi_handshaker_get_result(self) != TSI_OK) return TSI_FAILED_PRECONDITION;
  if (self->vtable->create_frame_protector == nullptr) return TSI_UNIMPLEMENTED;
  result = self->vtable->create_frame_protector(
      self, max_output_protected_frame_size, protector);
  if (result == TSI_OK) {
    self->frame_protector_created = true;
  }
  return result;
}

tsi_result tsi_handshaker_next(
    tsi_handshaker* self, const unsigned char* received_bytes,
    size_t received_bytes_size, const unsigned char** bytes_to_send,
    size_t* bytes_to_send_size, tsi_handshaker_result** handshaker_result,
    tsi_handshaker_on_next_done_cb cb, void* user_data) {
  if (self == nullptr || self->vtable == nullptr) return TSI_INVALID_ARGUMENT;
  if (self->handshaker_result_created) return TSI_FAILED_PRECONDITION;
  if (self->handshake_shutdown) return TSI_HANDSHAKE_SHUTDOWN;
  if (self->vtable->next == nullptr) return TSI_UNIMPLEMENTED;
  return self->vtable->next(self, received_bytes, received_bytes_size,
                            bytes_to_send, bytes_to_send_size,
                            handshaker_result, cb, user_data);
}

void tsi_handshaker_shutdown(tsi_handshaker* self) {
  if (self == nullptr || self->vtable == nullptr) return;
  if (self->vtable->shutdown != nullptr) {
    self->vtable->shutdown(self);
  }
  self->handshake_shutdown = true;
}

void tsi_handshaker_destroy(tsi_handshaker* self) {
  if (self == nullptr) return;
  self->vtable->destroy(self);
}

/* --- tsi_handshaker_result implementation. --- */

tsi_result tsi_handshaker_result_extract_peer(const tsi_handshaker_result* self,
                                              tsi_peer* peer) {
  if (self == nullptr || self->vtable == nullptr || peer == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  memset(peer, 0, sizeof(tsi_peer));
  if (self->vtable->extract_peer == nullptr) return TSI_UNIMPLEMENTED;
  return self->vtable->extract_peer(self, peer);
}

tsi_result tsi_handshaker_result_create_frame_protector(
    const tsi_handshaker_result* self, size_t* max_output_protected_frame_size,
    tsi_frame_protector** protector) {
  if (self == nullptr || self->vtable == nullptr || protector == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->vtable->create_frame_protector == nullptr) return TSI_UNIMPLEMENTED;
  return self->vtable->create_frame_protector(
      self, max_output_protected_frame_size, protector);
}

tsi_result tsi_handshaker_result_get_unused_bytes(
    const tsi_handshaker_result* self, const unsigned char** bytes,
    size_t* bytes_size) {
  if (self == nullptr || self->vtable == nullptr || bytes == nullptr ||
      bytes_size == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->vtable->get_unused_bytes == nullptr) return TSI_UNIMPLEMENTED;
  return self->vtable->get_unused_bytes(self, bytes, bytes_size);
}

void tsi_handshaker_result_destroy(tsi_handshaker_result* self) {
  if (self == nullptr) return;
  self->vtable->destroy(self);
}

/* --- tsi_peer implementation. --- */

tsi_peer_property tsi_init_peer_property(void) {
  tsi_peer_property property;
  memset(&property, 0, sizeof(tsi_peer_property));
  return property;
}

static void tsi_peer_destroy_list_property(tsi_peer_property* children,
                                           size_t child_count) {
  size_t i;
  for (i = 0; i < child_count; i++) {
    tsi_peer_property_destruct(&children[i]);
  }
  gpr_free(children);
}

void tsi_peer_property_destruct(tsi_peer_property* property) {
  if (property->name != nullptr) {
    gpr_free(property->name);
  }
  if (property->value.data != nullptr) {
    gpr_free(property->value.data);
  }
  *property = tsi_init_peer_property(); /* Reset everything to 0. */
}

void tsi_peer_destruct(tsi_peer* self) {
  if (self == nullptr) return;
  if (self->properties != nullptr) {
    tsi_peer_destroy_list_property(self->properties, self->property_count);
    self->properties = nullptr;
  }
  self->property_count = 0;
}

tsi_result tsi_construct_allocated_string_peer_property(
    const char* name, size_t value_length, tsi_peer_property* property) {
  *property = tsi_init_peer_property();
  if (name != nullptr) property->name = gpr_strdup(name);
  if (value_length > 0) {
    property->value.data = static_cast<char*>(gpr_zalloc(value_length));
    property->value.length = value_length;
  }
  return TSI_OK;
}

tsi_result tsi_construct_string_peer_property_from_cstring(
    const char* name, const char* value, tsi_peer_property* property) {
  return tsi_construct_string_peer_property(name, value, strlen(value),
                                            property);
}

tsi_result tsi_construct_string_peer_property(const char* name,
                                              const char* value,
                                              size_t value_length,
                                              tsi_peer_property* property) {
  tsi_result result = tsi_construct_allocated_string_peer_property(
      name, value_length, property);
  if (result != TSI_OK) return result;
  if (value_length > 0) {
    memcpy(property->value.data, value, value_length);
  }
  return TSI_OK;
}

tsi_result tsi_construct_peer(size_t property_count, tsi_peer* peer) {
  memset(peer, 0, sizeof(tsi_peer));
  if (property_count > 0) {
    peer->properties = static_cast<tsi_peer_property*>(
        gpr_zalloc(property_count * sizeof(tsi_peer_property)));
    peer->property_count = property_count;
  }
  return TSI_OK;
}

const tsi_peer_property* tsi_peer_get_property_by_name(const tsi_peer* peer,
                                                       const char* name) {
  size_t i;
  if (peer == nullptr) return nullptr;
  for (i = 0; i < peer->property_count; i++) {
    const tsi_peer_property* property = &peer->properties[i];
    if (name == nullptr && property->name == nullptr) {
      return property;
    }
    if (name != nullptr && property->name != nullptr &&
        strcmp(property->name, name) == 0) {
      return property;
    }
  }
  return nullptr;
}
