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

#include "src/core/security/secure_endpoint.h"
#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/slice.h>
#include <grpc/support/sync.h>
#include "src/core/tsi/transport_security_interface.h"
#include "src/core/debug/trace.h"

#define STAGING_BUFFER_SIZE 8192

typedef struct {
  grpc_endpoint base;
  grpc_endpoint *wrapped_ep;
  struct tsi_frame_protector *protector;
  gpr_mu protector_mu;
  /* saved upper level callbacks and user_data. */
  grpc_endpoint_read_cb read_cb;
  void *read_user_data;
  grpc_endpoint_write_cb write_cb;
  void *write_user_data;
  /* saved handshaker leftover data to unprotect. */
  gpr_slice_buffer leftover_bytes;
  /* buffers for read and write */
  gpr_slice read_staging_buffer;
  gpr_slice_buffer input_buffer;

  gpr_slice write_staging_buffer;
  gpr_slice_buffer output_buffer;

  gpr_refcount ref;
} secure_endpoint;

int grpc_trace_secure_endpoint = 0;

static void secure_endpoint_ref(secure_endpoint *ep) { gpr_ref(&ep->ref); }

static void destroy(secure_endpoint *secure_ep) {
  secure_endpoint *ep = secure_ep;
  grpc_endpoint_destroy(ep->wrapped_ep);
  tsi_frame_protector_destroy(ep->protector);
  gpr_slice_buffer_destroy(&ep->leftover_bytes);
  gpr_slice_unref(ep->read_staging_buffer);
  gpr_slice_buffer_destroy(&ep->input_buffer);
  gpr_slice_unref(ep->write_staging_buffer);
  gpr_slice_buffer_destroy(&ep->output_buffer);
  gpr_mu_destroy(&ep->protector_mu);
  gpr_free(ep);
}

static void secure_endpoint_unref(secure_endpoint *ep) {
  if (gpr_unref(&ep->ref)) {
    destroy(ep);
  }
}

static void flush_read_staging_buffer(secure_endpoint *ep, gpr_uint8 **cur,
                                      gpr_uint8 **end) {
  gpr_slice_buffer_add(&ep->input_buffer, ep->read_staging_buffer);
  ep->read_staging_buffer = gpr_slice_malloc(STAGING_BUFFER_SIZE);
  *cur = GPR_SLICE_START_PTR(ep->read_staging_buffer);
  *end = GPR_SLICE_END_PTR(ep->read_staging_buffer);
}

static void call_read_cb(secure_endpoint *ep, gpr_slice *slices, size_t nslices,
                         grpc_endpoint_cb_status error) {
  if (grpc_trace_secure_endpoint) {
    size_t i;
    for (i = 0; i < nslices; i++) {
      char *data = gpr_dump_slice(slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "READ %p: %s", ep, data);
      gpr_free(data);
    }
  }
  ep->read_cb(ep->read_user_data, slices, nslices, error);
  secure_endpoint_unref(ep);
}

static void on_read(void *user_data, gpr_slice *slices, size_t nslices,
                    grpc_endpoint_cb_status error) {
  unsigned i;
  gpr_uint8 keep_looping = 0;
  size_t input_buffer_count = 0;
  tsi_result result = TSI_OK;
  secure_endpoint *ep = (secure_endpoint *)user_data;
  gpr_uint8 *cur = GPR_SLICE_START_PTR(ep->read_staging_buffer);
  gpr_uint8 *end = GPR_SLICE_END_PTR(ep->read_staging_buffer);

  /* TODO(yangg) check error, maybe bail out early */
  for (i = 0; i < nslices; i++) {
    gpr_slice encrypted = slices[i];
    gpr_uint8 *message_bytes = GPR_SLICE_START_PTR(encrypted);
    size_t message_size = GPR_SLICE_LENGTH(encrypted);

    while (message_size > 0 || keep_looping) {
      size_t unprotected_buffer_size_written = (size_t)(end - cur);
      size_t processed_message_size = message_size;
      gpr_mu_lock(&ep->protector_mu);
      result = tsi_frame_protector_unprotect(ep->protector, message_bytes,
                                             &processed_message_size, cur,
                                             &unprotected_buffer_size_written);
      gpr_mu_unlock(&ep->protector_mu);
      if (result != TSI_OK) {
        gpr_log(GPR_ERROR, "Decryption error: %s",
                tsi_result_to_string(result));
        break;
      }
      message_bytes += processed_message_size;
      message_size -= processed_message_size;
      cur += unprotected_buffer_size_written;

      if (cur == end) {
        flush_read_staging_buffer(ep, &cur, &end);
        /* Force to enter the loop again to extract buffered bytes in protector.
           The bytes could be buffered because of running out of staging_buffer.
           If this happens at the end of all slices, doing another unprotect
           avoids leaving data in the protector. */
        keep_looping = 1;
      } else if (unprotected_buffer_size_written > 0) {
        keep_looping = 1;
      } else {
        keep_looping = 0;
      }
    }
    if (result != TSI_OK) break;
  }

  if (cur != GPR_SLICE_START_PTR(ep->read_staging_buffer)) {
    gpr_slice_buffer_add(
        &ep->input_buffer,
        gpr_slice_split_head(
            &ep->read_staging_buffer,
            (size_t)(cur - GPR_SLICE_START_PTR(ep->read_staging_buffer))));
  }

  /* TODO(yangg) experiment with moving this block after read_cb to see if it
     helps latency */
  for (i = 0; i < nslices; i++) {
    gpr_slice_unref(slices[i]);
  }

  if (result != TSI_OK) {
    gpr_slice_buffer_reset_and_unref(&ep->input_buffer);
    call_read_cb(ep, NULL, 0, GRPC_ENDPOINT_CB_ERROR);
    return;
  }
  /* The upper level will unref the slices. */
  input_buffer_count = ep->input_buffer.count;
  ep->input_buffer.count = 0;
  call_read_cb(ep, ep->input_buffer.slices, input_buffer_count, error);
}

static void endpoint_notify_on_read(grpc_endpoint *secure_ep,
                                    grpc_endpoint_read_cb cb, void *user_data) {
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  ep->read_cb = cb;
  ep->read_user_data = user_data;

  secure_endpoint_ref(ep);

  if (ep->leftover_bytes.count) {
    size_t leftover_nslices = ep->leftover_bytes.count;
    ep->leftover_bytes.count = 0;
    on_read(ep, ep->leftover_bytes.slices, leftover_nslices,
            GRPC_ENDPOINT_CB_OK);
    return;
  }

  grpc_endpoint_notify_on_read(ep->wrapped_ep, on_read, ep);
}

static void flush_write_staging_buffer(secure_endpoint *ep, gpr_uint8 **cur,
                                       gpr_uint8 **end) {
  gpr_slice_buffer_add(&ep->output_buffer, ep->write_staging_buffer);
  ep->write_staging_buffer = gpr_slice_malloc(STAGING_BUFFER_SIZE);
  *cur = GPR_SLICE_START_PTR(ep->write_staging_buffer);
  *end = GPR_SLICE_END_PTR(ep->write_staging_buffer);
}

static void on_write(void *data, grpc_endpoint_cb_status error) {
  secure_endpoint *ep = data;
  ep->write_cb(ep->write_user_data, error);
  secure_endpoint_unref(ep);
}

static grpc_endpoint_write_status endpoint_write(grpc_endpoint *secure_ep,
                                                 gpr_slice *slices,
                                                 size_t nslices,
                                                 grpc_endpoint_write_cb cb,
                                                 void *user_data) {
  unsigned i;
  size_t output_buffer_count = 0;
  tsi_result result = TSI_OK;
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  gpr_uint8 *cur = GPR_SLICE_START_PTR(ep->write_staging_buffer);
  gpr_uint8 *end = GPR_SLICE_END_PTR(ep->write_staging_buffer);
  grpc_endpoint_write_status status;
  GPR_ASSERT(ep->output_buffer.count == 0);

  if (grpc_trace_secure_endpoint) {
    for (i = 0; i < nslices; i++) {
      char *data = gpr_dump_slice(slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "WRITE %p: %s", ep, data);
      gpr_free(data);
    }
  }

  for (i = 0; i < nslices; i++) {
    gpr_slice plain = slices[i];
    gpr_uint8 *message_bytes = GPR_SLICE_START_PTR(plain);
    size_t message_size = GPR_SLICE_LENGTH(plain);
    while (message_size > 0) {
      size_t protected_buffer_size_to_send = (size_t)(end - cur);
      size_t processed_message_size = message_size;
      gpr_mu_lock(&ep->protector_mu);
      result = tsi_frame_protector_protect(ep->protector, message_bytes,
                                           &processed_message_size, cur,
                                           &protected_buffer_size_to_send);
      gpr_mu_unlock(&ep->protector_mu);
      if (result != TSI_OK) {
        gpr_log(GPR_ERROR, "Encryption error: %s",
                tsi_result_to_string(result));
        break;
      }
      message_bytes += processed_message_size;
      message_size -= processed_message_size;
      cur += protected_buffer_size_to_send;

      if (cur == end) {
        flush_write_staging_buffer(ep, &cur, &end);
      }
    }
    if (result != TSI_OK) break;
  }
  if (result == TSI_OK) {
    size_t still_pending_size;
    do {
      size_t protected_buffer_size_to_send = (size_t)(end - cur);
      gpr_mu_lock(&ep->protector_mu);
      result = tsi_frame_protector_protect_flush(ep->protector, cur,
                                                 &protected_buffer_size_to_send,
                                                 &still_pending_size);
      gpr_mu_unlock(&ep->protector_mu);
      if (result != TSI_OK) break;
      cur += protected_buffer_size_to_send;
      if (cur == end) {
        flush_write_staging_buffer(ep, &cur, &end);
      }
    } while (still_pending_size > 0);
    if (cur != GPR_SLICE_START_PTR(ep->write_staging_buffer)) {
      gpr_slice_buffer_add(
          &ep->output_buffer,
          gpr_slice_split_head(
              &ep->write_staging_buffer,
              (size_t)(cur - GPR_SLICE_START_PTR(ep->write_staging_buffer))));
    }
  }

  for (i = 0; i < nslices; i++) {
    gpr_slice_unref(slices[i]);
  }

  if (result != TSI_OK) {
    /* TODO(yangg) do different things according to the error type? */
    gpr_slice_buffer_reset_and_unref(&ep->output_buffer);
    return GRPC_ENDPOINT_WRITE_ERROR;
  }

  /* clear output_buffer and let the lower level handle its slices. */
  output_buffer_count = ep->output_buffer.count;
  ep->output_buffer.count = 0;
  ep->write_cb = cb;
  ep->write_user_data = user_data;
  /* Need to keep the endpoint alive across a transport */
  secure_endpoint_ref(ep);
  status = grpc_endpoint_write(ep->wrapped_ep, ep->output_buffer.slices,
                               output_buffer_count, on_write, ep);
  if (status != GRPC_ENDPOINT_WRITE_PENDING) {
    secure_endpoint_unref(ep);
  }
  return status;
}

static void endpoint_shutdown(grpc_endpoint *secure_ep) {
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  grpc_endpoint_shutdown(ep->wrapped_ep);
}

static void endpoint_unref(grpc_endpoint *secure_ep) {
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  secure_endpoint_unref(ep);
}

static void endpoint_add_to_pollset(grpc_endpoint *secure_ep,
                                    grpc_pollset *pollset) {
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  grpc_endpoint_add_to_pollset(ep->wrapped_ep, pollset);
}

static const grpc_endpoint_vtable vtable = {
    endpoint_notify_on_read, endpoint_write, endpoint_add_to_pollset,
    endpoint_shutdown, endpoint_unref};

grpc_endpoint *grpc_secure_endpoint_create(
    struct tsi_frame_protector *protector, grpc_endpoint *transport,
    gpr_slice *leftover_slices, size_t leftover_nslices) {
  size_t i;
  secure_endpoint *ep = (secure_endpoint *)gpr_malloc(sizeof(secure_endpoint));
  ep->base.vtable = &vtable;
  ep->wrapped_ep = transport;
  ep->protector = protector;
  gpr_slice_buffer_init(&ep->leftover_bytes);
  for (i = 0; i < leftover_nslices; i++) {
    gpr_slice_buffer_add(&ep->leftover_bytes,
                         gpr_slice_ref(leftover_slices[i]));
  }
  ep->write_staging_buffer = gpr_slice_malloc(STAGING_BUFFER_SIZE);
  ep->read_staging_buffer = gpr_slice_malloc(STAGING_BUFFER_SIZE);
  gpr_slice_buffer_init(&ep->input_buffer);
  gpr_slice_buffer_init(&ep->output_buffer);
  gpr_mu_init(&ep->protector_mu);
  gpr_ref_init(&ep->ref, 1);
  return &ep->base;
}
