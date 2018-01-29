/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_UV

#include <limits.h>
#include <string.h>

#include <grpc/slice_buffer.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_uv.h"
#include "src/core/lib/iomgr/network_status_tracker.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "src/core/lib/iomgr/tcp_uv.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

grpc_core::TraceFlag grpc_tcp_trace(false, "tcp");

typedef struct {
  grpc_endpoint base;
  gpr_refcount refcount;

  uv_write_t write_req;
  uv_shutdown_t shutdown_req;

  uv_tcp_t* handle;

  grpc_closure* read_cb;
  grpc_closure* write_cb;

  grpc_slice_buffer* read_slices;
  grpc_slice_buffer* write_slices;
  uv_buf_t* write_buffers;

  grpc_resource_user* resource_user;
  grpc_resource_user_slice_allocator slice_allocator;

  bool shutting_down;

  char* peer_string;
  grpc_pollset* pollset;
} grpc_tcp;

static grpc_error* tcp_annotate_error(grpc_error* src_error, grpc_tcp* tcp) {
  return grpc_error_set_str(
      grpc_error_set_int(
          src_error,
          /* All tcp errors are marked with UNAVAILABLE so that application may
           * choose to retry. */
          GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE),
      GRPC_ERROR_STR_TARGET_ADDRESS,
      grpc_slice_from_copied_string(tcp->peer_string));
}

static void tcp_free(grpc_tcp* tcp) {
  grpc_resource_user_unref(tcp->resource_user);
  gpr_free(tcp->handle);
  gpr_free(tcp->peer_string);
  gpr_free(tcp);
}

#ifndef NDEBUG
#define TCP_UNREF(tcp, reason) tcp_unref((tcp), (reason), __FILE__, __LINE__)
#define TCP_REF(tcp, reason) tcp_ref((tcp), (reason), __FILE__, __LINE__)
static void tcp_unref(grpc_tcp* tcp, const char* reason, const char* file,
                      int line) {
  if (grpc_tcp_trace.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&tcp->refcount.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "TCP unref %p : %s %" PRIdPTR " -> %" PRIdPTR, tcp, reason, val,
            val - 1);
  }
  if (gpr_unref(&tcp->refcount)) {
    tcp_free(tcp);
  }
}

static void tcp_ref(grpc_tcp* tcp, const char* reason, const char* file,
                    int line) {
  if (grpc_tcp_trace.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&tcp->refcount.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "TCP   ref %p : %s %" PRIdPTR " -> %" PRIdPTR, tcp, reason, val,
            val + 1);
  }
  gpr_ref(&tcp->refcount);
}
#else
#define TCP_UNREF(tcp, reason) tcp_unref((tcp))
#define TCP_REF(tcp, reason) tcp_ref((tcp))
static void tcp_unref(grpc_tcp* tcp) {
  if (gpr_unref(&tcp->refcount)) {
    tcp_free(tcp);
  }
}

static void tcp_ref(grpc_tcp* tcp) { gpr_ref(&tcp->refcount); }
#endif

static void uv_close_callback(uv_handle_t* handle) {
  grpc_core::ExecCtx exec_ctx;
  grpc_tcp* tcp = (grpc_tcp*)handle->data;
  TCP_UNREF(tcp, "destroy");
}

static void alloc_uv_buf(uv_handle_t* handle, size_t suggested_size,
                         uv_buf_t* buf) {
  grpc_core::ExecCtx exec_ctx;
  grpc_tcp* tcp = (grpc_tcp*)handle->data;
  (void)suggested_size;
  /* Before calling uv_read_start, we allocate a buffer with exactly one slice
   * to tcp->read_slices and wait for the callback indicating that the
   * allocation was successful. So slices[0] should always exist here */
  buf->base = (char*)GRPC_SLICE_START_PTR(tcp->read_slices->slices[0]);
  buf->len = GRPC_SLICE_LENGTH(tcp->read_slices->slices[0]);
}

static void call_read_cb(grpc_tcp* tcp, grpc_error* error) {
  grpc_closure* cb = tcp->read_cb;
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "TCP:%p call_cb %p %p:%p", tcp, cb, cb->cb, cb->cb_arg);
    size_t i;
    const char* str = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "read: error=%s", str);

    for (i = 0; i < tcp->read_slices->count; i++) {
      char* dump = grpc_dump_slice(tcp->read_slices->slices[i],
                                   GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "READ %p (peer=%s): %s", tcp, tcp->peer_string, dump);
      gpr_free(dump);
    }
  }
  tcp->read_slices = NULL;
  tcp->read_cb = NULL;
  GRPC_CLOSURE_RUN(cb, error);
}

static void read_callback(uv_stream_t* stream, ssize_t nread,
                          const uv_buf_t* buf) {
  grpc_error* error;
  grpc_core::ExecCtx exec_ctx;
  grpc_tcp* tcp = (grpc_tcp*)stream->data;
  grpc_slice_buffer garbage;
  if (nread == 0) {
    // Nothing happened. Wait for the next callback
    return;
  }
  TCP_UNREF(tcp, "read");
  // TODO(murgatroid99): figure out what the return value here means
  uv_read_stop(stream);
  if (nread == UV_EOF) {
    error =
        tcp_annotate_error(GRPC_ERROR_CREATE_FROM_STATIC_STRING("EOF"), tcp);
    grpc_slice_buffer_reset_and_unref_internal(tcp->read_slices);
  } else if (nread > 0) {
    // Successful read
    error = GRPC_ERROR_NONE;
    if ((size_t)nread < tcp->read_slices->length) {
      /* TODO(murgatroid99): Instead of discarding the unused part of the read
       * buffer, reuse it as the next read buffer. */
      grpc_slice_buffer_init(&garbage);
      grpc_slice_buffer_trim_end(
          tcp->read_slices, tcp->read_slices->length - (size_t)nread, &garbage);
      grpc_slice_buffer_reset_and_unref_internal(&garbage);
    }
  } else {
    // nread < 0: Error
    error = tcp_annotate_error(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("TCP Read failed"), tcp);
    grpc_slice_buffer_reset_and_unref_internal(tcp->read_slices);
  }
  call_read_cb(tcp, error);
}

static void tcp_read_allocation_done(void* tcpp, grpc_error* error) {
  int status;
  grpc_tcp* tcp = (grpc_tcp*)tcpp;
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "TCP:%p read_allocation_done: %s", tcp,
            grpc_error_string(error));
  }
  if (error == GRPC_ERROR_NONE) {
    status =
        uv_read_start((uv_stream_t*)tcp->handle, alloc_uv_buf, read_callback);
    if (status != 0) {
      error = tcp_annotate_error(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("TCP Read failed at start"),
          tcp);
      error = grpc_error_set_str(
          error, GRPC_ERROR_STR_OS_ERROR,
          grpc_slice_from_static_string(uv_strerror(status)));
    }
  }
  if (error != GRPC_ERROR_NONE) {
    grpc_slice_buffer_reset_and_unref_internal(tcp->read_slices);
    call_read_cb(tcp, GRPC_ERROR_REF(error));
    TCP_UNREF(tcp, "read");
  }
  if (grpc_tcp_trace.enabled()) {
    const char* str = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "Initiating read on %p: error=%s", tcp, str);
  }
}

static void uv_endpoint_read(grpc_endpoint* ep, grpc_slice_buffer* read_slices,
                             grpc_closure* cb) {
  grpc_tcp* tcp = (grpc_tcp*)ep;
  GRPC_UV_ASSERT_SAME_THREAD();
  GPR_ASSERT(tcp->read_cb == NULL);
  tcp->read_cb = cb;
  tcp->read_slices = read_slices;
  grpc_slice_buffer_reset_and_unref_internal(read_slices);
  TCP_REF(tcp, "read");
  grpc_resource_user_alloc_slices(&tcp->slice_allocator,
                                  GRPC_TCP_DEFAULT_READ_SLICE_SIZE, 1,
                                  tcp->read_slices);
}

static void write_callback(uv_write_t* req, int status) {
  grpc_tcp* tcp = (grpc_tcp*)req->data;
  grpc_error* error;
  grpc_core::ExecCtx exec_ctx;
  grpc_closure* cb = tcp->write_cb;
  tcp->write_cb = NULL;
  TCP_UNREF(tcp, "write");
  if (status == 0) {
    error = GRPC_ERROR_NONE;
  } else {
    error = tcp_annotate_error(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("TCP Write failed"), tcp);
  }
  if (grpc_tcp_trace.enabled()) {
    const char* str = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "write complete on %p: error=%s", tcp, str);
  }
  gpr_free(tcp->write_buffers);
  GRPC_CLOSURE_SCHED(cb, error);
}

static void uv_endpoint_write(grpc_endpoint* ep,
                              grpc_slice_buffer* write_slices,
                              grpc_closure* cb) {
  grpc_tcp* tcp = (grpc_tcp*)ep;
  uv_buf_t* buffers;
  unsigned int buffer_count;
  unsigned int i;
  grpc_slice* slice;
  uv_write_t* write_req;
  GRPC_UV_ASSERT_SAME_THREAD();

  if (grpc_tcp_trace.enabled()) {
    size_t j;

    for (j = 0; j < write_slices->count; j++) {
      char* data = grpc_dump_slice(write_slices->slices[j],
                                   GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "WRITE %p (peer=%s): %s", tcp, tcp->peer_string, data);
      gpr_free(data);
    }
  }

  if (tcp->shutting_down) {
    GRPC_CLOSURE_SCHED(cb,
                       tcp_annotate_error(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                              "TCP socket is shutting down"),
                                          tcp));
    return;
  }

  GPR_ASSERT(tcp->write_cb == NULL);
  tcp->write_slices = write_slices;
  GPR_ASSERT(tcp->write_slices->count <= UINT_MAX);
  if (tcp->write_slices->count == 0) {
    // No slices means we don't have to do anything,
    // and libuv doesn't like empty writes
    GRPC_CLOSURE_SCHED(cb, GRPC_ERROR_NONE);
    return;
  }

  tcp->write_cb = cb;
  buffer_count = (unsigned int)tcp->write_slices->count;
  buffers = (uv_buf_t*)gpr_malloc(sizeof(uv_buf_t) * buffer_count);
  for (i = 0; i < buffer_count; i++) {
    slice = &tcp->write_slices->slices[i];
    buffers[i].base = (char*)GRPC_SLICE_START_PTR(*slice);
    buffers[i].len = GRPC_SLICE_LENGTH(*slice);
  }
  tcp->write_buffers = buffers;
  write_req = &tcp->write_req;
  write_req->data = tcp;
  TCP_REF(tcp, "write");
  // TODO(murgatroid99): figure out what the return value here means
  uv_write(write_req, (uv_stream_t*)tcp->handle, buffers, buffer_count,
           write_callback);
}

static void uv_add_to_pollset(grpc_endpoint* ep, grpc_pollset* pollset) {
  // No-op. We're ignoring pollsets currently
  (void)ep;
  (void)pollset;
  grpc_tcp* tcp = (grpc_tcp*)ep;
  tcp->pollset = pollset;
}

static void uv_add_to_pollset_set(grpc_endpoint* ep,
                                  grpc_pollset_set* pollset) {
  // No-op. We're ignoring pollsets currently
  (void)ep;
  (void)pollset;
}

static void uv_delete_from_pollset_set(grpc_endpoint* ep,
                                       grpc_pollset_set* pollset) {
  // No-op. We're ignoring pollsets currently
  (void)ep;
  (void)pollset;
}

static void shutdown_callback(uv_shutdown_t* req, int status) {}

static void uv_endpoint_shutdown(grpc_endpoint* ep, grpc_error* why) {
  grpc_tcp* tcp = (grpc_tcp*)ep;
  if (!tcp->shutting_down) {
    if (grpc_tcp_trace.enabled()) {
      const char* str = grpc_error_string(why);
      gpr_log(GPR_DEBUG, "TCP %p shutdown why=%s", tcp->handle, str);
    }
    tcp->shutting_down = true;
    uv_shutdown_t* req = &tcp->shutdown_req;
    uv_shutdown(req, (uv_stream_t*)tcp->handle, shutdown_callback);
    grpc_resource_user_shutdown(tcp->resource_user);
  }
  GRPC_ERROR_UNREF(why);
}

static void uv_destroy(grpc_endpoint* ep) {
  grpc_network_status_unregister_endpoint(ep);
  grpc_tcp* tcp = (grpc_tcp*)ep;
  uv_close((uv_handle_t*)tcp->handle, uv_close_callback);
}

static char* uv_get_peer(grpc_endpoint* ep) {
  grpc_tcp* tcp = (grpc_tcp*)ep;
  return gpr_strdup(tcp->peer_string);
}

static grpc_resource_user* uv_get_resource_user(grpc_endpoint* ep) {
  grpc_tcp* tcp = (grpc_tcp*)ep;
  return tcp->resource_user;
}

static int uv_get_fd(grpc_endpoint* ep) { return -1; }

static grpc_endpoint_vtable vtable = {uv_endpoint_read,
                                      uv_endpoint_write,
                                      uv_add_to_pollset,
                                      uv_add_to_pollset_set,
                                      uv_delete_from_pollset_set,
                                      uv_endpoint_shutdown,
                                      uv_destroy,
                                      uv_get_resource_user,
                                      uv_get_peer,
                                      uv_get_fd};

grpc_endpoint* grpc_tcp_create(uv_tcp_t* handle,
                               grpc_resource_quota* resource_quota,
                               char* peer_string) {
  grpc_tcp* tcp = (grpc_tcp*)gpr_malloc(sizeof(grpc_tcp));
  grpc_core::ExecCtx exec_ctx;

  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "Creating TCP endpoint %p", tcp);
  }

  /* Disable Nagle's Algorithm */
  uv_tcp_nodelay(handle, 1);

  memset(tcp, 0, sizeof(grpc_tcp));
  tcp->base.vtable = &vtable;
  tcp->handle = handle;
  handle->data = tcp;
  gpr_ref_init(&tcp->refcount, 1);
  tcp->peer_string = gpr_strdup(peer_string);
  tcp->shutting_down = false;
  tcp->read_slices = NULL;
  tcp->resource_user = grpc_resource_user_create(resource_quota, peer_string);
  grpc_resource_user_slice_allocator_init(
      &tcp->slice_allocator, tcp->resource_user, tcp_read_allocation_done, tcp);
  /* Tell network status tracking code about the new endpoint */
  grpc_network_status_register_endpoint(&tcp->base);

#ifndef GRPC_UV_TCP_HOLD_LOOP
  uv_unref((uv_handle_t*)handle);
#endif

  return &tcp->base;
}

#endif /* GRPC_UV */
