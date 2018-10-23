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

/* With the addition of a libuv endpoint, sockaddr.h now includes uv.h when
   using that endpoint. Because of various transitive includes in uv.h,
   including windows.h on Windows, uv.h must be included before other system
   headers. Therefore, sockaddr.h must always be included first */
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/sockaddr.h"

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/security/transport/secure_endpoint.h"
#include "src/core/lib/security/transport/tsi_error.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/tsi/transport_security_grpc.h"

#define STAGING_BUFFER_SIZE 8192

typedef struct {
  grpc_endpoint base;
  grpc_endpoint* wrapped_ep;
  struct tsi_frame_protector* protector;
  struct tsi_zero_copy_grpc_protector* zero_copy_protector;
  gpr_mu protector_mu;
  /* saved upper level callbacks and user_data. */
  grpc_closure* read_cb;
  grpc_closure* write_cb;
  grpc_closure on_read;
  grpc_slice_buffer* read_buffer;
  grpc_slice_buffer source_buffer;
  /* saved handshaker leftover data to unprotect. */
  grpc_slice_buffer leftover_bytes;
  /* buffers for read and write */
  grpc_slice read_staging_buffer;

  grpc_slice write_staging_buffer;
  grpc_slice_buffer output_buffer;

  gpr_refcount ref;
} secure_endpoint;

grpc_core::TraceFlag grpc_trace_secure_endpoint(false, "secure_endpoint");

static void destroy(secure_endpoint* secure_ep) {
  secure_endpoint* ep = secure_ep;
  grpc_endpoint_destroy(ep->wrapped_ep);
  tsi_frame_protector_destroy(ep->protector);
  tsi_zero_copy_grpc_protector_destroy(ep->zero_copy_protector);
  grpc_slice_buffer_destroy_internal(&ep->leftover_bytes);
  grpc_slice_unref_internal(ep->read_staging_buffer);
  grpc_slice_unref_internal(ep->write_staging_buffer);
  grpc_slice_buffer_destroy_internal(&ep->output_buffer);
  grpc_slice_buffer_destroy_internal(&ep->source_buffer);
  gpr_mu_destroy(&ep->protector_mu);
  gpr_free(ep);
}

#ifndef NDEBUG
#define SECURE_ENDPOINT_UNREF(ep, reason) \
  secure_endpoint_unref((ep), (reason), __FILE__, __LINE__)
#define SECURE_ENDPOINT_REF(ep, reason) \
  secure_endpoint_ref((ep), (reason), __FILE__, __LINE__)
static void secure_endpoint_unref(secure_endpoint* ep, const char* reason,
                                  const char* file, int line) {
  if (grpc_trace_secure_endpoint.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&ep->ref.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "SECENDP unref %p : %s %" PRIdPTR " -> %" PRIdPTR, ep, reason, val,
            val - 1);
  }
  if (gpr_unref(&ep->ref)) {
    destroy(ep);
  }
}

static void secure_endpoint_ref(secure_endpoint* ep, const char* reason,
                                const char* file, int line) {
  if (grpc_trace_secure_endpoint.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&ep->ref.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "SECENDP   ref %p : %s %" PRIdPTR " -> %" PRIdPTR, ep, reason, val,
            val + 1);
  }
  gpr_ref(&ep->ref);
}
#else
#define SECURE_ENDPOINT_UNREF(ep, reason) secure_endpoint_unref((ep))
#define SECURE_ENDPOINT_REF(ep, reason) secure_endpoint_ref((ep))
static void secure_endpoint_unref(secure_endpoint* ep) {
  if (gpr_unref(&ep->ref)) {
    destroy(ep);
  }
}

static void secure_endpoint_ref(secure_endpoint* ep) { gpr_ref(&ep->ref); }
#endif

static void flush_read_staging_buffer(secure_endpoint* ep, uint8_t** cur,
                                      uint8_t** end) {
  grpc_slice_buffer_add(ep->read_buffer, ep->read_staging_buffer);
  ep->read_staging_buffer = GRPC_SLICE_MALLOC(STAGING_BUFFER_SIZE);
  *cur = GRPC_SLICE_START_PTR(ep->read_staging_buffer);
  *end = GRPC_SLICE_END_PTR(ep->read_staging_buffer);
}

static void call_read_cb(secure_endpoint* ep, grpc_error* error) {
  if (grpc_trace_secure_endpoint.enabled()) {
    size_t i;
    for (i = 0; i < ep->read_buffer->count; i++) {
      char* data = grpc_dump_slice(ep->read_buffer->slices[i],
                                   GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_INFO, "READ %p: %s", ep, data);
      gpr_free(data);
    }
  }
  ep->read_buffer = nullptr;
  GRPC_CLOSURE_SCHED(ep->read_cb, error);
  SECURE_ENDPOINT_UNREF(ep, "read");
}

static void on_read(void* user_data, grpc_error* error) {
  unsigned i;
  uint8_t keep_looping = 0;
  tsi_result result = TSI_OK;
  secure_endpoint* ep = static_cast<secure_endpoint*>(user_data);
  uint8_t* cur = GRPC_SLICE_START_PTR(ep->read_staging_buffer);
  uint8_t* end = GRPC_SLICE_END_PTR(ep->read_staging_buffer);

  if (error != GRPC_ERROR_NONE) {
    grpc_slice_buffer_reset_and_unref_internal(ep->read_buffer);
    call_read_cb(ep, GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                         "Secure read failed", &error, 1));
    return;
  }

  if (ep->zero_copy_protector != nullptr) {
    // Use zero-copy grpc protector to unprotect.
    result = tsi_zero_copy_grpc_protector_unprotect(
        ep->zero_copy_protector, &ep->source_buffer, ep->read_buffer);
  } else {
    // Use frame protector to unprotect.
    /* TODO(yangg) check error, maybe bail out early */
    for (i = 0; i < ep->source_buffer.count; i++) {
      grpc_slice encrypted = ep->source_buffer.slices[i];
      uint8_t* message_bytes = GRPC_SLICE_START_PTR(encrypted);
      size_t message_size = GRPC_SLICE_LENGTH(encrypted);

      while (message_size > 0 || keep_looping) {
        size_t unprotected_buffer_size_written = static_cast<size_t>(end - cur);
        size_t processed_message_size = message_size;
        gpr_mu_lock(&ep->protector_mu);
        result = tsi_frame_protector_unprotect(
            ep->protector, message_bytes, &processed_message_size, cur,
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
          /* Force to enter the loop again to extract buffered bytes in
             protector. The bytes could be buffered because of running out of
             staging_buffer. If this happens at the end of all slices, doing
             another unprotect avoids leaving data in the protector. */
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
              static_cast<size_t>(
                  cur - GRPC_SLICE_START_PTR(ep->read_staging_buffer))));
    }
  }

  /* TODO(yangg) experiment with moving this block after read_cb to see if it
     helps latency */
  grpc_slice_buffer_reset_and_unref_internal(&ep->source_buffer);

  if (result != TSI_OK) {
    grpc_slice_buffer_reset_and_unref_internal(ep->read_buffer);
    call_read_cb(
        ep, grpc_set_tsi_error_result(
                GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unwrap failed"), result));
    return;
  }

  call_read_cb(ep, GRPC_ERROR_NONE);
}

static void endpoint_read(grpc_endpoint* secure_ep, grpc_slice_buffer* slices,
                          grpc_closure* cb) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  ep->read_cb = cb;
  ep->read_buffer = slices;
  grpc_slice_buffer_reset_and_unref_internal(ep->read_buffer);

  SECURE_ENDPOINT_REF(ep, "read");
  if (ep->leftover_bytes.count) {
    grpc_slice_buffer_swap(&ep->leftover_bytes, &ep->source_buffer);
    GPR_ASSERT(ep->leftover_bytes.count == 0);
    on_read(ep, GRPC_ERROR_NONE);
    return;
  }

  grpc_endpoint_read(ep->wrapped_ep, &ep->source_buffer, &ep->on_read);
}

static void flush_write_staging_buffer(secure_endpoint* ep, uint8_t** cur,
                                       uint8_t** end) {
  grpc_slice_buffer_add(&ep->output_buffer, ep->write_staging_buffer);
  ep->write_staging_buffer = GRPC_SLICE_MALLOC(STAGING_BUFFER_SIZE);
  *cur = GRPC_SLICE_START_PTR(ep->write_staging_buffer);
  *end = GRPC_SLICE_END_PTR(ep->write_staging_buffer);
}

static void endpoint_write(grpc_endpoint* secure_ep, grpc_slice_buffer* slices,
                           grpc_closure* cb, void* arg) {
  GPR_TIMER_SCOPE("secure_endpoint.endpoint_write", 0);

  unsigned i;
  tsi_result result = TSI_OK;
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  uint8_t* cur = GRPC_SLICE_START_PTR(ep->write_staging_buffer);
  uint8_t* end = GRPC_SLICE_END_PTR(ep->write_staging_buffer);

  grpc_slice_buffer_reset_and_unref_internal(&ep->output_buffer);

  if (grpc_trace_secure_endpoint.enabled()) {
    for (i = 0; i < slices->count; i++) {
      char* data =
          grpc_dump_slice(slices->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_INFO, "WRITE %p: %s", ep, data);
      gpr_free(data);
    }
  }

  if (ep->zero_copy_protector != nullptr) {
    // Use zero-copy grpc protector to protect.
    result = tsi_zero_copy_grpc_protector_protect(ep->zero_copy_protector,
                                                  slices, &ep->output_buffer);
  } else {
    // Use frame protector to protect.
    for (i = 0; i < slices->count; i++) {
      grpc_slice plain = slices->slices[i];
      uint8_t* message_bytes = GRPC_SLICE_START_PTR(plain);
      size_t message_size = GRPC_SLICE_LENGTH(plain);
      while (message_size > 0) {
        size_t protected_buffer_size_to_send = static_cast<size_t>(end - cur);
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
        size_t protected_buffer_size_to_send = static_cast<size_t>(end - cur);
        gpr_mu_lock(&ep->protector_mu);
        result = tsi_frame_protector_protect_flush(
            ep->protector, cur, &protected_buffer_size_to_send,
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
                static_cast<size_t>(
                    cur - GRPC_SLICE_START_PTR(ep->write_staging_buffer))));
      }
    }
  }

  if (result != TSI_OK) {
    /* TODO(yangg) do different things according to the error type? */
    grpc_slice_buffer_reset_and_unref_internal(&ep->output_buffer);
    GRPC_CLOSURE_SCHED(
        cb, grpc_set_tsi_error_result(
                GRPC_ERROR_CREATE_FROM_STATIC_STRING("Wrap failed"), result));
    return;
  }

  grpc_endpoint_write(ep->wrapped_ep, &ep->output_buffer, cb, arg);
}

static void endpoint_shutdown(grpc_endpoint* secure_ep, grpc_error* why) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  grpc_endpoint_shutdown(ep->wrapped_ep, why);
}

static void endpoint_destroy(grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  SECURE_ENDPOINT_UNREF(ep, "destroy");
}

static void endpoint_add_to_pollset(grpc_endpoint* secure_ep,
                                    grpc_pollset* pollset) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  grpc_endpoint_add_to_pollset(ep->wrapped_ep, pollset);
}

static void endpoint_add_to_pollset_set(grpc_endpoint* secure_ep,
                                        grpc_pollset_set* pollset_set) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  grpc_endpoint_add_to_pollset_set(ep->wrapped_ep, pollset_set);
}

static void endpoint_delete_from_pollset_set(grpc_endpoint* secure_ep,
                                             grpc_pollset_set* pollset_set) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  grpc_endpoint_delete_from_pollset_set(ep->wrapped_ep, pollset_set);
}

static char* endpoint_get_peer(grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  return grpc_endpoint_get_peer(ep->wrapped_ep);
}

static int endpoint_get_fd(grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  return grpc_endpoint_get_fd(ep->wrapped_ep);
}

static grpc_resource_user* endpoint_get_resource_user(
    grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  return grpc_endpoint_get_resource_user(ep->wrapped_ep);
}

static const grpc_endpoint_vtable vtable = {endpoint_read,
                                            endpoint_write,
                                            endpoint_add_to_pollset,
                                            endpoint_add_to_pollset_set,
                                            endpoint_delete_from_pollset_set,
                                            endpoint_shutdown,
                                            endpoint_destroy,
                                            endpoint_get_resource_user,
                                            endpoint_get_peer,
                                            endpoint_get_fd};

grpc_endpoint* grpc_secure_endpoint_create(
    struct tsi_frame_protector* protector,
    struct tsi_zero_copy_grpc_protector* zero_copy_protector,
    grpc_endpoint* transport, grpc_slice* leftover_slices,
    size_t leftover_nslices) {
  size_t i;
  secure_endpoint* ep =
      static_cast<secure_endpoint*>(gpr_malloc(sizeof(secure_endpoint)));
  ep->base.vtable = &vtable;
  ep->wrapped_ep = transport;
  ep->protector = protector;
  ep->zero_copy_protector = zero_copy_protector;
  grpc_slice_buffer_init(&ep->leftover_bytes);
  for (i = 0; i < leftover_nslices; i++) {
    grpc_slice_buffer_add(&ep->leftover_bytes,
                          grpc_slice_ref_internal(leftover_slices[i]));
  }
  ep->write_staging_buffer = GRPC_SLICE_MALLOC(STAGING_BUFFER_SIZE);
  ep->read_staging_buffer = GRPC_SLICE_MALLOC(STAGING_BUFFER_SIZE);
  grpc_slice_buffer_init(&ep->output_buffer);
  grpc_slice_buffer_init(&ep->source_buffer);
  ep->read_buffer = nullptr;
  GRPC_CLOSURE_INIT(&ep->on_read, on_read, ep, grpc_schedule_on_exec_ctx);
  gpr_mu_init(&ep->protector_mu);
  gpr_ref_init(&ep->ref, 1);
  return &ep->base;
}
