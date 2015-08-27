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

#include "src/core/security/secure_transport_setup.h"

#include <string.h>

#include "src/core/security/secure_endpoint.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>

#define GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE 256

typedef struct {
  grpc_security_connector *connector;
  tsi_handshaker *handshaker;
  unsigned char *handshake_buffer;
  size_t handshake_buffer_size;
  grpc_endpoint *wrapped_endpoint;
  grpc_endpoint *secure_endpoint;
  gpr_slice_buffer left_overs;
  grpc_secure_transport_setup_done_cb cb;
  void *user_data;
} grpc_secure_transport_setup;

static void on_handshake_data_received_from_peer(void *setup, gpr_slice *slices,
                                                 size_t nslices,
                                                 grpc_endpoint_cb_status error);

static void on_handshake_data_sent_to_peer(void *setup,
                                           grpc_endpoint_cb_status error);

static void secure_transport_setup_done(grpc_secure_transport_setup *s,
                                        int is_success) {
  if (is_success) {
    s->cb(s->user_data, GRPC_SECURITY_OK, s->wrapped_endpoint,
          s->secure_endpoint);
  } else {
    if (s->secure_endpoint != NULL) {
      grpc_endpoint_shutdown(s->secure_endpoint);
      grpc_endpoint_destroy(s->secure_endpoint);
    } else {
      grpc_endpoint_destroy(s->wrapped_endpoint);
    }
    s->cb(s->user_data, GRPC_SECURITY_ERROR, s->wrapped_endpoint, NULL);
  }
  if (s->handshaker != NULL) tsi_handshaker_destroy(s->handshaker);
  if (s->handshake_buffer != NULL) gpr_free(s->handshake_buffer);
  gpr_slice_buffer_destroy(&s->left_overs);
  GRPC_SECURITY_CONNECTOR_UNREF(s->connector, "secure_transport_setup");
  gpr_free(s);
}

static void on_peer_checked(void *user_data, grpc_security_status status) {
  grpc_secure_transport_setup *s = user_data;
  tsi_frame_protector *protector;
  tsi_result result;
  if (status != GRPC_SECURITY_OK) {
    gpr_log(GPR_ERROR, "Error checking peer.");
    secure_transport_setup_done(s, 0);
    return;
  }
  result =
      tsi_handshaker_create_frame_protector(s->handshaker, NULL, &protector);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Frame protector creation failed with error %s.",
            tsi_result_to_string(result));
    secure_transport_setup_done(s, 0);
    return;
  }
  s->secure_endpoint =
      grpc_secure_endpoint_create(protector, s->wrapped_endpoint,
                                  s->left_overs.slices, s->left_overs.count);
  secure_transport_setup_done(s, 1);
  return;
}

static void check_peer(grpc_secure_transport_setup *s) {
  grpc_security_status peer_status;
  tsi_peer peer;
  tsi_result result = tsi_handshaker_extract_peer(s->handshaker, &peer);

  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Peer extraction failed with error %s",
            tsi_result_to_string(result));
    secure_transport_setup_done(s, 0);
    return;
  }
  peer_status = grpc_security_connector_check_peer(s->connector, peer,
                                                   on_peer_checked, s);
  if (peer_status == GRPC_SECURITY_ERROR) {
    gpr_log(GPR_ERROR, "Peer check failed.");
    secure_transport_setup_done(s, 0);
    return;
  } else if (peer_status == GRPC_SECURITY_OK) {
    on_peer_checked(s, peer_status);
  }
}

static void send_handshake_bytes_to_peer(grpc_secure_transport_setup *s) {
  size_t offset = 0;
  tsi_result result = TSI_OK;
  gpr_slice to_send;
  grpc_endpoint_write_status write_status;

  do {
    size_t to_send_size = s->handshake_buffer_size - offset;
    result = tsi_handshaker_get_bytes_to_send_to_peer(
        s->handshaker, s->handshake_buffer + offset, &to_send_size);
    offset += to_send_size;
    if (result == TSI_INCOMPLETE_DATA) {
      s->handshake_buffer_size *= 2;
      s->handshake_buffer =
          gpr_realloc(s->handshake_buffer, s->handshake_buffer_size);
    }
  } while (result == TSI_INCOMPLETE_DATA);

  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshake failed with error %s",
            tsi_result_to_string(result));
    secure_transport_setup_done(s, 0);
    return;
  }

  to_send =
      gpr_slice_from_copied_buffer((const char *)s->handshake_buffer, offset);
  /* TODO(klempner,jboeuf): This should probably use the client setup
         deadline */
  write_status = grpc_endpoint_write(s->wrapped_endpoint, &to_send, 1,
                                     on_handshake_data_sent_to_peer, s);
  if (write_status == GRPC_ENDPOINT_WRITE_ERROR) {
    gpr_log(GPR_ERROR, "Could not send handshake data to peer.");
    secure_transport_setup_done(s, 0);
  } else if (write_status == GRPC_ENDPOINT_WRITE_DONE) {
    on_handshake_data_sent_to_peer(s, GRPC_ENDPOINT_CB_OK);
  }
}

static void cleanup_slices(gpr_slice *slices, size_t num_slices) {
  size_t i;
  for (i = 0; i < num_slices; i++) {
    gpr_slice_unref(slices[i]);
  }
}

static void on_handshake_data_received_from_peer(
    void *setup, gpr_slice *slices, size_t nslices,
    grpc_endpoint_cb_status error) {
  grpc_secure_transport_setup *s = setup;
  size_t consumed_slice_size = 0;
  tsi_result result = TSI_OK;
  size_t i;
  size_t num_left_overs;
  int has_left_overs_in_current_slice = 0;

  if (error != GRPC_ENDPOINT_CB_OK) {
    gpr_log(GPR_ERROR, "Read failed.");
    cleanup_slices(slices, nslices);
    secure_transport_setup_done(s, 0);
    return;
  }

  for (i = 0; i < nslices; i++) {
    consumed_slice_size = GPR_SLICE_LENGTH(slices[i]);
    result = tsi_handshaker_process_bytes_from_peer(
        s->handshaker, GPR_SLICE_START_PTR(slices[i]), &consumed_slice_size);
    if (!tsi_handshaker_is_in_progress(s->handshaker)) break;
  }

  if (tsi_handshaker_is_in_progress(s->handshaker)) {
    /* We may need more data. */
    if (result == TSI_INCOMPLETE_DATA) {
      /* TODO(klempner,jboeuf): This should probably use the client setup
         deadline */
      grpc_endpoint_notify_on_read(s->wrapped_endpoint,
                                   on_handshake_data_received_from_peer, setup);
      cleanup_slices(slices, nslices);
      return;
    } else {
      send_handshake_bytes_to_peer(s);
      cleanup_slices(slices, nslices);
      return;
    }
  }

  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshake failed with error %s",
            tsi_result_to_string(result));
    cleanup_slices(slices, nslices);
    secure_transport_setup_done(s, 0);
    return;
  }

  /* Handshake is done and successful this point. */
  has_left_overs_in_current_slice =
      (consumed_slice_size < GPR_SLICE_LENGTH(slices[i]));
  num_left_overs = (has_left_overs_in_current_slice ? 1 : 0) + nslices - i - 1;
  if (num_left_overs == 0) {
    cleanup_slices(slices, nslices);
    check_peer(s);
    return;
  }
  cleanup_slices(slices, nslices - num_left_overs);

  /* Put the leftovers in our buffer (ownership transfered). */
  if (has_left_overs_in_current_slice) {
    gpr_slice_buffer_add(&s->left_overs,
                         gpr_slice_split_tail(&slices[i], consumed_slice_size));
    gpr_slice_unref(slices[i]); /* split_tail above increments refcount. */
  }
  gpr_slice_buffer_addn(
      &s->left_overs, &slices[i + 1],
      num_left_overs - (size_t)has_left_overs_in_current_slice);
  check_peer(s);
}

/* If setup is NULL, the setup is done. */
static void on_handshake_data_sent_to_peer(void *setup,
                                           grpc_endpoint_cb_status error) {
  grpc_secure_transport_setup *s = setup;

  /* Make sure that write is OK. */
  if (error != GRPC_ENDPOINT_CB_OK) {
    gpr_log(GPR_ERROR, "Write failed with error %d.", error);
    if (setup != NULL) secure_transport_setup_done(s, 0);
    return;
  }

  /* We may be done. */
  if (tsi_handshaker_is_in_progress(s->handshaker)) {
    /* TODO(klempner,jboeuf): This should probably use the client setup
       deadline */
    grpc_endpoint_notify_on_read(s->wrapped_endpoint,
                                 on_handshake_data_received_from_peer, setup);
  } else {
    check_peer(s);
  }
}

void grpc_setup_secure_transport(grpc_security_connector *connector,
                                 grpc_endpoint *nonsecure_endpoint,
                                 grpc_secure_transport_setup_done_cb cb,
                                 void *user_data) {
  grpc_security_status result = GRPC_SECURITY_OK;
  grpc_secure_transport_setup *s =
      gpr_malloc(sizeof(grpc_secure_transport_setup));
  memset(s, 0, sizeof(grpc_secure_transport_setup));
  result = grpc_security_connector_create_handshaker(connector, &s->handshaker);
  if (result != GRPC_SECURITY_OK) {
    secure_transport_setup_done(s, 0);
    return;
  }
  s->connector =
      GRPC_SECURITY_CONNECTOR_REF(connector, "secure_transport_setup");
  s->handshake_buffer_size = GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE;
  s->handshake_buffer = gpr_malloc(s->handshake_buffer_size);
  s->wrapped_endpoint = nonsecure_endpoint;
  s->user_data = user_data;
  s->cb = cb;
  gpr_slice_buffer_init(&s->left_overs);
  send_handshake_bytes_to_peer(s);
}
