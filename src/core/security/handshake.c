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

#include "src/core/security/handshake.h"

#include <string.h>

#include "src/core/security/secure_endpoint.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>

#define GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE 256

typedef struct {
  grpc_security_connector *connector;
  unsigned char *handshake_buffer;
  size_t handshake_buffer_size;
  grpc_endpoint *wrapped_endpoint;
  grpc_endpoint *secure_endpoint;
  gpr_slice_buffer left_overs;
  grpc_security_handshake_done_cb cb;
  void *user_data;
} grpc_security_handshake;

static void on_handshake_data_received_from_peer(void *handshake,
                                                 gpr_slice *slices,
                                                 size_t nslices,
                                                 grpc_endpoint_cb_status error);

static void on_handshake_data_sent_to_peer(void *handshake,
                                           grpc_endpoint_cb_status error);

static void security_handshake_done(grpc_security_handshake *h,
                                    int is_success) {
  if (is_success) {
    h->cb(h->user_data, GRPC_SECURITY_OK, h->wrapped_endpoint,
          h->secure_endpoint);
  } else {
    if (h->secure_endpoint != NULL) {
      grpc_endpoint_shutdown(h->secure_endpoint);
      grpc_endpoint_destroy(h->secure_endpoint);
    } else {
      grpc_endpoint_destroy(h->wrapped_endpoint);
    }
    h->cb(h->user_data, GRPC_SECURITY_ERROR, h->wrapped_endpoint, NULL);
  }
  if (h->handshake_buffer != NULL) gpr_free(h->handshake_buffer);
  gpr_slice_buffer_destroy(&h->left_overs);
  gpr_free(h);
}

static void on_peer_checked(void *user_data, grpc_security_status status) {
  grpc_security_handshake *h = user_data;
  tsi_frame_protector *protector;
  tsi_result result;
  if (status != GRPC_SECURITY_OK) {
    gpr_log(GPR_ERROR, "Error checking peer.");
    security_handshake_done(h, 0);
    return;
  }
  result = tsi_handshaker_create_frame_protector(h->connector->handshaker, NULL,
                                                 &protector);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Frame protector creation failed with error %s.",
            tsi_result_to_string(result));
    security_handshake_done(h, 0);
    return;
  }
  h->secure_endpoint =
      grpc_secure_endpoint_create(protector, h->wrapped_endpoint,
                                  h->left_overs.slices, h->left_overs.count);
  security_handshake_done(h, 1);
  return;
}

static void check_peer(grpc_security_handshake *h) {
  grpc_security_status peer_status;
  tsi_peer peer;
  tsi_result result =
      tsi_handshaker_extract_peer(h->connector->handshaker, &peer);

  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Peer extraction failed with error %s",
            tsi_result_to_string(result));
    security_handshake_done(h, 0);
    return;
  }
  peer_status = grpc_security_connector_check_peer(h->connector, peer,
                                                   on_peer_checked, h);
  if (peer_status == GRPC_SECURITY_ERROR) {
    gpr_log(GPR_ERROR, "Peer check failed.");
    security_handshake_done(h, 0);
    return;
  } else if (peer_status == GRPC_SECURITY_OK) {
    on_peer_checked(h, peer_status);
  }
}

static void send_handshake_bytes_to_peer(grpc_security_handshake *h) {
  size_t offset = 0;
  tsi_result result = TSI_OK;
  gpr_slice to_send;
  grpc_endpoint_write_status write_status;

  do {
    size_t to_send_size = h->handshake_buffer_size - offset;
    result = tsi_handshaker_get_bytes_to_send_to_peer(
        h->connector->handshaker, h->handshake_buffer + offset, &to_send_size);
    offset += to_send_size;
    if (result == TSI_INCOMPLETE_DATA) {
      h->handshake_buffer_size *= 2;
      h->handshake_buffer =
          gpr_realloc(h->handshake_buffer, h->handshake_buffer_size);
    }
  } while (result == TSI_INCOMPLETE_DATA);

  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshake failed with error %s",
            tsi_result_to_string(result));
    security_handshake_done(h, 0);
    return;
  }

  to_send =
      gpr_slice_from_copied_buffer((const char *)h->handshake_buffer, offset);
  /* TODO(klempner,jboeuf): This should probably use the client setup
         deadline */
  write_status = grpc_endpoint_write(h->wrapped_endpoint, &to_send, 1,
                                     on_handshake_data_sent_to_peer, h);
  if (write_status == GRPC_ENDPOINT_WRITE_ERROR) {
    gpr_log(GPR_ERROR, "Could not send handshake data to peer.");
    security_handshake_done(h, 0);
  } else if (write_status == GRPC_ENDPOINT_WRITE_DONE) {
    on_handshake_data_sent_to_peer(h, GRPC_ENDPOINT_CB_OK);
  }
}

static void cleanup_slices(gpr_slice *slices, size_t num_slices) {
  size_t i;
  for (i = 0; i < num_slices; i++) {
    gpr_slice_unref(slices[i]);
  }
}

static void on_handshake_data_received_from_peer(
    void *handshake, gpr_slice *slices, size_t nslices,
    grpc_endpoint_cb_status error) {
  grpc_security_handshake *h = handshake;
  size_t consumed_slice_size = 0;
  tsi_result result = TSI_OK;
  size_t i;
  size_t num_left_overs;
  int has_left_overs_in_current_slice = 0;

  if (error != GRPC_ENDPOINT_CB_OK) {
    gpr_log(GPR_ERROR, "Read failed.");
    cleanup_slices(slices, nslices);
    security_handshake_done(h, 0);
    return;
  }

  for (i = 0; i < nslices; i++) {
    consumed_slice_size = GPR_SLICE_LENGTH(slices[i]);
    result = tsi_handshaker_process_bytes_from_peer(
        h->connector->handshaker, GPR_SLICE_START_PTR(slices[i]),
        &consumed_slice_size);
    if (!tsi_handshaker_is_in_progress(h->connector->handshaker)) break;
  }

  if (tsi_handshaker_is_in_progress(h->connector->handshaker)) {
    /* We may need more data. */
    if (result == TSI_INCOMPLETE_DATA) {
      /* TODO(klempner,jboeuf): This should probably use the client setup
         deadline */
      grpc_endpoint_notify_on_read(
          h->wrapped_endpoint, on_handshake_data_received_from_peer, handshake);
      cleanup_slices(slices, nslices);
      return;
    } else {
      send_handshake_bytes_to_peer(h);
      cleanup_slices(slices, nslices);
      return;
    }
  }

  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshake failed with error %s",
            tsi_result_to_string(result));
    cleanup_slices(slices, nslices);
    security_handshake_done(h, 0);
    return;
  }

  /* Handshake is done and successful this point. */
  has_left_overs_in_current_slice =
      (consumed_slice_size < GPR_SLICE_LENGTH(slices[i]));
  num_left_overs = (has_left_overs_in_current_slice ? 1 : 0) + nslices - i - 1;
  if (num_left_overs == 0) {
    cleanup_slices(slices, nslices);
    check_peer(h);
    return;
  }
  cleanup_slices(slices, nslices - num_left_overs);

  /* Put the leftovers in our buffer (ownership transfered). */
  if (has_left_overs_in_current_slice) {
    gpr_slice_buffer_add(&h->left_overs,
                         gpr_slice_split_tail(&slices[i], consumed_slice_size));
    gpr_slice_unref(slices[i]); /* split_tail above increments refcount. */
  }
  gpr_slice_buffer_addn(
      &h->left_overs, &slices[i + 1],
      num_left_overs - (size_t)has_left_overs_in_current_slice);
  check_peer(h);
}

/* If handshake is NULL, the handshake is done. */
static void on_handshake_data_sent_to_peer(void *handshake,
                                           grpc_endpoint_cb_status error) {
  grpc_security_handshake *h = handshake;

  /* Make sure that write is OK. */
  if (error != GRPC_ENDPOINT_CB_OK) {
    gpr_log(GPR_ERROR, "Write failed with error %d.", error);
    if (handshake != NULL) security_handshake_done(h, 0);
    return;
  }

  /* We may be done. */
  if (tsi_handshaker_is_in_progress(h->connector->handshaker)) {
    /* TODO(klempner,jboeuf): This should probably use the client setup
       deadline */
    grpc_endpoint_notify_on_read(
        h->wrapped_endpoint, on_handshake_data_received_from_peer, handshake);
  } else {
    check_peer(h);
  }
}

void grpc_do_security_handshake(grpc_security_connector *connector,
                                grpc_endpoint *nonsecure_endpoint,
                                grpc_security_handshake_done_cb cb,
                                void *user_data) {
  grpc_security_handshake *h = gpr_malloc(sizeof(grpc_security_handshake));
  memset(h, 0, sizeof(grpc_security_handshake));
  h->connector = connector;
  h->handshake_buffer_size = GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE;
  h->handshake_buffer = gpr_malloc(h->handshake_buffer_size);
  h->wrapped_endpoint = nonsecure_endpoint;
  h->user_data = user_data;
  h->cb = cb;
  gpr_slice_buffer_init(&h->left_overs);
  send_handshake_bytes_to_peer(h);
}
