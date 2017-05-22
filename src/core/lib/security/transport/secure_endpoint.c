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

/* With the addition of a libuv endpoint, sockaddr.h now includes uv.h when
   using that endpoint. Because of various transitive includes in uv.h,
   including windows.h on Windows, uv.h must be included before other system
   headers. Therefore, sockaddr.h must always be included first */
#include "src/core/lib/iomgr/sockaddr.h"

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/security/transport/secure_endpoint.h"
#include "src/core/lib/security/transport/tsi_error.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "src/core/tsi/transport_security_interface.h"

#define STAGING_BUFFER_SIZE 8192

typedef struct {
  grpc_endpoint base;
  grpc_endpoint *wrapped_ep;
  struct tsi_frame_protector *protector;
  gpr_mu protector_mu;
  /* saved upper level callbacks and user_data. */
  grpc_closure *read_cb;
  grpc_closure *write_cb;
  grpc_closure on_read;
  grpc_slice_buffer *read_buffer;
  grpc_slice_buffer source_buffer;
  /* saved handshaker leftover data to unprotect. */
  grpc_slice_buffer leftover_bytes;
  /* buffers for read and write */
  grpc_slice read_staging_buffer;

  grpc_slice write_staging_buffer;
  grpc_slice_buffer output_buffer;

  gpr_refcount ref;
} secure_endpoint;

grpc_tracer_flag grpc_trace_secure_endpoint = GRPC_TRACER_INITIALIZER(false);

static void destroy(grpc_exec_ctx *exec_ctx, secure_endpoint *secure_ep) {
  secure_endpoint *ep = secure_ep;
  grpc_endpoint_destroy(exec_ctx, ep->wrapped_ep);
  tsi_frame_protector_destroy(ep->protector);
  grpc_slice_buffer_destroy_internal(exec_ctx, &ep->leftover_bytes);
  grpc_slice_unref_internal(exec_ctx, ep->read_staging_buffer);
  grpc_slice_unref_internal(exec_ctx, ep->write_staging_buffer);
  grpc_slice_buffer_destroy_internal(exec_ctx, &ep->output_buffer);
  grpc_slice_buffer_destroy_internal(exec_ctx, &ep->source_buffer);
  gpr_mu_destroy(&ep->protector_mu);
  gpr_free(ep);
}

/*#define GRPC_SECURE_ENDPOINT_REFCOUNT_DEBUG*/
#ifdef GRPC_SECURE_ENDPOINT_REFCOUNT_DEBUG
#define SECURE_ENDPOINT_UNREF(exec_ctx, ep, reason) \
  secure_endpoint_unref((exec_ctx), (ep), (reason), __FILE__, __LINE__)
#define SECURE_ENDPOINT_REF(ep, reason) \
  secure_endpoint_ref((ep), (reason), __FILE__, __LINE__)
static void secure_endpoint_unref(secure_endpoint *ep,
                                  grpc_closure_list *closure_list,
                                  const char *reason, const char *file,
                                  int line) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "SECENDP unref %p : %s %d -> %d",
          ep, reason, ep->ref.count, ep->ref.count - 1);
  if (gpr_unref(&ep->ref)) {
    destroy(exec_ctx, ep);
  }
}

static void secure_endpoint_ref(secure_endpoint *ep, const char *reason,
                                const char *file, int line) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "SECENDP   ref %p : %s %d -> %d",
          ep, reason, ep->ref.count, ep->ref.count + 1);
  gpr_ref(&ep->ref);
}
#else
#define SECURE_ENDPOINT_UNREF(exec_ctx, ep, reason) \
  secure_endpoint_unref((exec_ctx), (ep))
#define SECURE_ENDPOINT_REF(ep, reason) secure_endpoint_ref((ep))
static void secure_endpoint_unref(grpc_exec_ctx *exec_ctx,
                                  secure_endpoint *ep) {
  if (gpr_unref(&ep->ref)) {
    destroy(exec_ctx, ep);
  }
}

static void secure_endpoint_ref(secure_endpoint *ep) { gpr_ref(&ep->ref); }
#endif

static void flush_read_staging_buffer(secure_endpoint *ep, uint8_t **cur,
                                      uint8_t **end) {
  grpc_slice_buffer_add(ep->read_buffer, ep->read_staging_buffer);
  ep->read_staging_buffer = GRPC_SLICE_MALLOC(STAGING_BUFFER_SIZE);
  *cur = GRPC_SLICE_START_PTR(ep->read_staging_buffer);
  *end = GRPC_SLICE_END_PTR(ep->read_staging_buffer);
}

static void call_read_cb(grpc_exec_ctx *exec_ctx, secure_endpoint *ep,
                         grpc_error *error) {
  if (GRPC_TRACER_ON(grpc_trace_secure_endpoint)) {
    size_t i;
    for (i = 0; i < ep->read_buffer->count; i++) {
      char *data = grpc_dump_slice(ep->read_buffer->slices[i],
                                   GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "READ %p: %s", ep, data);
      gpr_free(data);
    }
  }
  ep->read_buffer = NULL;
  grpc_closure_sched(exec_ctx, ep->read_cb, error);
  SECURE_ENDPOINT_UNREF(exec_ctx, ep, "read");
}

static void on_read(grpc_exec_ctx *exec_ctx, void *user_data,
                    grpc_error *error) {
  unsigned i;
  uint8_t keep_looping = 0;
  tsi_result result = TSI_OK;
  secure_endpoint *ep = (secure_endpoint *)user_data;
  uint8_t *cur = GRPC_SLICE_START_PTR(ep->read_staging_buffer);
  uint8_t *end = GRPC_SLICE_END_PTR(ep->read_staging_buffer);

  if (error != GRPC_ERROR_NONE) {
    grpc_slice_buffer_reset_and_unref_internal(exec_ctx, ep->read_buffer);
    call_read_cb(exec_ctx, ep, GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                   "Secure read failed", &error, 1));
    return;
  }

  /* TODO(yangg) check error, maybe bail out early */
  for (i = 0; i < ep->source_buffer.count; i++) {
    grpc_slice encrypted = ep->source_buffer.slices[i];
    uint8_t *message_bytes = GRPC_SLICE_START_PTR(encrypted);
    size_t message_size = GRPC_SLICE_LENGTH(encrypted);

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

  if (cur != GRPC_SLICE_START_PTR(ep->read_staging_buffer)) {
    grpc_slice_buffer_add(
        ep->read_buffer,
        grpc_slice_split_head(
            &ep->read_staging_buffer,
            (size_t)(cur - GRPC_SLICE_START_PTR(ep->read_staging_buffer))));
  }

  /* TODO(yangg) experiment with moving this block after read_cb to see if it
     helps latency */
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &ep->source_buffer);

  if (result != TSI_OK) {
    grpc_slice_buffer_reset_and_unref_internal(exec_ctx, ep->read_buffer);
    call_read_cb(
        exec_ctx, ep,
        grpc_set_tsi_error_result(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unwrap failed"), result));
    return;
  }

  call_read_cb(exec_ctx, ep, GRPC_ERROR_NONE);
}

static void endpoint_read(grpc_exec_ctx *exec_ctx, grpc_endpoint *secure_ep,
                          grpc_slice_buffer *slices, grpc_closure *cb) {
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  ep->read_cb = cb;
  ep->read_buffer = slices;
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, ep->read_buffer);

  SECURE_ENDPOINT_REF(ep, "read");
  if (ep->leftover_bytes.count) {
    grpc_slice_buffer_swap(&ep->leftover_bytes, &ep->source_buffer);
    GPR_ASSERT(ep->leftover_bytes.count == 0);
    on_read(exec_ctx, ep, GRPC_ERROR_NONE);
    return;
  }

  grpc_endpoint_read(exec_ctx, ep->wrapped_ep, &ep->source_buffer,
                     &ep->on_read);
}

static void flush_write_staging_buffer(secure_endpoint *ep, uint8_t **cur,
                                       uint8_t **end) {
  grpc_slice_buffer_add(&ep->output_buffer, ep->write_staging_buffer);
  ep->write_staging_buffer = GRPC_SLICE_MALLOC(STAGING_BUFFER_SIZE);
  *cur = GRPC_SLICE_START_PTR(ep->write_staging_buffer);
  *end = GRPC_SLICE_END_PTR(ep->write_staging_buffer);
}

static void endpoint_write(grpc_exec_ctx *exec_ctx, grpc_endpoint *secure_ep,
                           grpc_slice_buffer *slices, grpc_closure *cb) {
  GPR_TIMER_BEGIN("secure_endpoint.endpoint_write", 0);

  unsigned i;
  tsi_result result = TSI_OK;
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  uint8_t *cur = GRPC_SLICE_START_PTR(ep->write_staging_buffer);
  uint8_t *end = GRPC_SLICE_END_PTR(ep->write_staging_buffer);

  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &ep->output_buffer);

  if (GRPC_TRACER_ON(grpc_trace_secure_endpoint)) {
    for (i = 0; i < slices->count; i++) {
      char *data =
          grpc_dump_slice(slices->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "WRITE %p: %s", ep, data);
      gpr_free(data);
    }
  }

  for (i = 0; i < slices->count; i++) {
    grpc_slice plain = slices->slices[i];
    uint8_t *message_bytes = GRPC_SLICE_START_PTR(plain);
    size_t message_size = GRPC_SLICE_LENGTH(plain);
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
    if (cur != GRPC_SLICE_START_PTR(ep->write_staging_buffer)) {
      grpc_slice_buffer_add(
          &ep->output_buffer,
          grpc_slice_split_head(
              &ep->write_staging_buffer,
              (size_t)(cur - GRPC_SLICE_START_PTR(ep->write_staging_buffer))));
    }
  }

  if (result != TSI_OK) {
    /* TODO(yangg) do different things according to the error type? */
    grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &ep->output_buffer);
    grpc_closure_sched(
        exec_ctx, cb,
        grpc_set_tsi_error_result(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Wrap failed"), result));
    GPR_TIMER_END("secure_endpoint.endpoint_write", 0);
    return;
  }

  grpc_endpoint_write(exec_ctx, ep->wrapped_ep, &ep->output_buffer, cb);
  GPR_TIMER_END("secure_endpoint.endpoint_write", 0);
}

static void endpoint_shutdown(grpc_exec_ctx *exec_ctx, grpc_endpoint *secure_ep,
                              grpc_error *why) {
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  grpc_endpoint_shutdown(exec_ctx, ep->wrapped_ep, why);
}

static void endpoint_destroy(grpc_exec_ctx *exec_ctx,
                             grpc_endpoint *secure_ep) {
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  SECURE_ENDPOINT_UNREF(exec_ctx, ep, "destroy");
}

static void endpoint_add_to_pollset(grpc_exec_ctx *exec_ctx,
                                    grpc_endpoint *secure_ep,
                                    grpc_pollset *pollset) {
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  grpc_endpoint_add_to_pollset(exec_ctx, ep->wrapped_ep, pollset);
}

static void endpoint_add_to_pollset_set(grpc_exec_ctx *exec_ctx,
                                        grpc_endpoint *secure_ep,
                                        grpc_pollset_set *pollset_set) {
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  grpc_endpoint_add_to_pollset_set(exec_ctx, ep->wrapped_ep, pollset_set);
}

static char *endpoint_get_peer(grpc_endpoint *secure_ep) {
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  return grpc_endpoint_get_peer(ep->wrapped_ep);
}

static int endpoint_get_fd(grpc_endpoint *secure_ep) {
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  return grpc_endpoint_get_fd(ep->wrapped_ep);
}

static grpc_workqueue *endpoint_get_workqueue(grpc_endpoint *secure_ep) {
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  return grpc_endpoint_get_workqueue(ep->wrapped_ep);
}

static grpc_resource_user *endpoint_get_resource_user(
    grpc_endpoint *secure_ep) {
  secure_endpoint *ep = (secure_endpoint *)secure_ep;
  return grpc_endpoint_get_resource_user(ep->wrapped_ep);
}

static const grpc_endpoint_vtable vtable = {endpoint_read,
                                            endpoint_write,
                                            endpoint_get_workqueue,
                                            endpoint_add_to_pollset,
                                            endpoint_add_to_pollset_set,
                                            endpoint_shutdown,
                                            endpoint_destroy,
                                            endpoint_get_resource_user,
                                            endpoint_get_peer,
                                            endpoint_get_fd};

grpc_endpoint *grpc_secure_endpoint_create(
    struct tsi_frame_protector *protector, grpc_endpoint *transport,
    grpc_slice *leftover_slices, size_t leftover_nslices) {
  size_t i;
  secure_endpoint *ep = (secure_endpoint *)gpr_malloc(sizeof(secure_endpoint));
  ep->base.vtable = &vtable;
  ep->wrapped_ep = transport;
  ep->protector = protector;
  grpc_slice_buffer_init(&ep->leftover_bytes);
  for (i = 0; i < leftover_nslices; i++) {
    grpc_slice_buffer_add(&ep->leftover_bytes,
                          grpc_slice_ref_internal(leftover_slices[i]));
  }
  ep->write_staging_buffer = GRPC_SLICE_MALLOC(STAGING_BUFFER_SIZE);
  ep->read_staging_buffer = GRPC_SLICE_MALLOC(STAGING_BUFFER_SIZE);
  grpc_slice_buffer_init(&ep->output_buffer);
  grpc_slice_buffer_init(&ep->source_buffer);
  ep->read_buffer = NULL;
  grpc_closure_init(&ep->on_read, on_read, ep, grpc_schedule_on_exec_ctx);
  gpr_mu_init(&ep->protector_mu);
  gpr_ref_init(&ep->ref, 1);
  return &ep->base;
}
