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

#ifndef GRPC_CORE_TSI_TRANSPORT_SECURITY_H
#define GRPC_CORE_TSI_TRANSPORT_SECURITY_H

#include <stdbool.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/tsi/transport_security_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

extern grpc_tracer_flag tsi_tracing_enabled;

/* Base for tsi_frame_protector implementations.
   See transport_security_interface.h for documentation. */
typedef struct {
  tsi_result (*protect)(tsi_frame_protector *self,
                        const unsigned char *unprotected_bytes,
                        size_t *unprotected_bytes_size,
                        unsigned char *protected_output_frames,
                        size_t *protected_output_frames_size);
  tsi_result (*protect_flush)(tsi_frame_protector *self,
                              unsigned char *protected_output_frames,
                              size_t *protected_output_frames_size,
                              size_t *still_pending_size);
  tsi_result (*unprotect)(tsi_frame_protector *self,
                          const unsigned char *protected_frames_bytes,
                          size_t *protected_frames_bytes_size,
                          unsigned char *unprotected_bytes,
                          size_t *unprotected_bytes_size);
  void (*destroy)(tsi_frame_protector *self);
} tsi_frame_protector_vtable;

struct tsi_frame_protector {
  const tsi_frame_protector_vtable *vtable;
};

/* Base for tsi_handshaker implementations.
   See transport_security_interface.h for documentation. */
typedef struct {
  tsi_result (*get_bytes_to_send_to_peer)(tsi_handshaker *self,
                                          unsigned char *bytes,
                                          size_t *bytes_size);
  tsi_result (*process_bytes_from_peer)(tsi_handshaker *self,
                                        const unsigned char *bytes,
                                        size_t *bytes_size);
  tsi_result (*get_result)(tsi_handshaker *self);
  tsi_result (*extract_peer)(tsi_handshaker *self, tsi_peer *peer);
  tsi_result (*create_frame_protector)(tsi_handshaker *self,
                                       size_t *max_protected_frame_size,
                                       tsi_frame_protector **protector);
  void (*destroy)(tsi_handshaker *self);
  tsi_result (*next)(tsi_handshaker *self, const unsigned char *received_bytes,
                     size_t received_bytes_size, unsigned char **bytes_to_send,
                     size_t *bytes_to_send_size,
                     tsi_handshaker_result **handshaker_result,
                     tsi_handshaker_on_next_done_cb cb, void *user_data);
} tsi_handshaker_vtable;

struct tsi_handshaker {
  const tsi_handshaker_vtable *vtable;
  bool frame_protector_created;
  bool handshaker_result_created;
};

/* Base for tsi_handshaker_result implementations.
   See transport_security_interface.h for documentation. */
typedef struct {
  tsi_result (*extract_peer)(const tsi_handshaker_result *self, tsi_peer *peer);
  tsi_result (*create_frame_protector)(const tsi_handshaker_result *self,
                                       size_t *max_output_protected_frame_size,
                                       tsi_frame_protector **protector);
  tsi_result (*get_unused_bytes)(const tsi_handshaker_result *self,
                                 unsigned char **bytes, size_t *bytes_size);
  void (*destroy)(tsi_handshaker_result *self);
} tsi_handshaker_result_vtable;

struct tsi_handshaker_result {
  const tsi_handshaker_result_vtable *vtable;
};

/* Peer and property construction/destruction functions. */
tsi_result tsi_construct_peer(size_t property_count, tsi_peer *peer);
tsi_peer_property tsi_init_peer_property(void);
void tsi_peer_property_destruct(tsi_peer_property *property);
tsi_result tsi_construct_string_peer_property(const char *name,
                                              const char *value,
                                              size_t value_length,
                                              tsi_peer_property *property);
tsi_result tsi_construct_allocated_string_peer_property(
    const char *name, size_t value_length, tsi_peer_property *property);
tsi_result tsi_construct_string_peer_property_from_cstring(
    const char *name, const char *value, tsi_peer_property *property);

/* Utils. */
char *tsi_strdup(const char *src); /* Sadly, no strdup in C89. */

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_TSI_TRANSPORT_SECURITY_H */
