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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/iomgr/network_status_tracker.h"
#include "src/core/lib/iomgr/tcp_posix.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"

#ifdef GRPC_HAVE_MSG_NOSIGNAL
#define SENDMSG_FLAGS MSG_NOSIGNAL
#else
#define SENDMSG_FLAGS 0
#endif

#ifdef GRPC_MSG_IOVLEN_TYPE
typedef GRPC_MSG_IOVLEN_TYPE msg_iovlen_type;
#else
typedef size_t msg_iovlen_type;
#endif

int grpc_tcp_trace = 0;

typedef struct {
  grpc_endpoint base;
  grpc_fd *em_fd;
  int fd;
  bool finished_edge;
  msg_iovlen_type iov_size; /* Number of slices to allocate per read attempt */
  size_t slice_size;
  gpr_refcount refcount;
  gpr_atm shutdown_count;

  /* garbage after the last read */
  grpc_slice_buffer last_read_buffer;

  grpc_slice_buffer *incoming_buffer;
  grpc_slice_buffer *outgoing_buffer;
  /** slice within outgoing_buffer to write next */
  size_t outgoing_slice_idx;
  /** byte within outgoing_buffer->slices[outgoing_slice_idx] to write next */
  size_t outgoing_byte_idx;

  grpc_closure *read_cb;
  grpc_closure *write_cb;
  grpc_closure *release_fd_cb;
  int *release_fd;

  grpc_closure read_closure;
  grpc_closure write_closure;

  char *peer_string;

  grpc_resource_user *resource_user;
  grpc_resource_user_slice_allocator slice_allocator;
} grpc_tcp;

static grpc_error *tcp_annotate_error(grpc_error *src_error, grpc_tcp *tcp) {
  return grpc_error_set_str(
      grpc_error_set_int(src_error, GRPC_ERROR_INT_FD, tcp->fd),
      GRPC_ERROR_STR_TARGET_ADDRESS, tcp->peer_string);
}

static void tcp_handle_read(grpc_exec_ctx *exec_ctx, void *arg /* grpc_tcp */,
                            grpc_error *error);
static void tcp_handle_write(grpc_exec_ctx *exec_ctx, void *arg /* grpc_tcp */,
                             grpc_error *error);

static void tcp_shutdown(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_fd_shutdown(exec_ctx, tcp->em_fd);
  grpc_resource_user_shutdown(exec_ctx, tcp->resource_user);
}

static void tcp_free(grpc_exec_ctx *exec_ctx, grpc_tcp *tcp) {
  grpc_fd_orphan(exec_ctx, tcp->em_fd, tcp->release_fd_cb, tcp->release_fd,
                 "tcp_unref_orphan");
  grpc_slice_buffer_destroy_internal(exec_ctx, &tcp->last_read_buffer);
  grpc_resource_user_unref(exec_ctx, tcp->resource_user);
  gpr_free(tcp->peer_string);
  gpr_free(tcp);
}

/*#define GRPC_TCP_REFCOUNT_DEBUG*/
#ifdef GRPC_TCP_REFCOUNT_DEBUG
#define TCP_UNREF(cl, tcp, reason) \
  tcp_unref((cl), (tcp), (reason), __FILE__, __LINE__)
#define TCP_REF(tcp, reason) tcp_ref((tcp), (reason), __FILE__, __LINE__)
static void tcp_unref(grpc_exec_ctx *exec_ctx, grpc_tcp *tcp,
                      const char *reason, const char *file, int line) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "TCP unref %p : %s %d -> %d", tcp,
          reason, tcp->refcount.count, tcp->refcount.count - 1);
  if (gpr_unref(&tcp->refcount)) {
    tcp_free(exec_ctx, tcp);
  }
}

static void tcp_ref(grpc_tcp *tcp, const char *reason, const char *file,
                    int line) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "TCP   ref %p : %s %d -> %d", tcp,
          reason, tcp->refcount.count, tcp->refcount.count + 1);
  gpr_ref(&tcp->refcount);
}
#else
#define TCP_UNREF(cl, tcp, reason) tcp_unref((cl), (tcp))
#define TCP_REF(tcp, reason) tcp_ref((tcp))
static void tcp_unref(grpc_exec_ctx *exec_ctx, grpc_tcp *tcp) {
  if (gpr_unref(&tcp->refcount)) {
    tcp_free(exec_ctx, tcp);
  }
}

static void tcp_ref(grpc_tcp *tcp) { gpr_ref(&tcp->refcount); }
#endif

static void tcp_destroy(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep) {
  grpc_network_status_unregister_endpoint(ep);
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &tcp->last_read_buffer);
  TCP_UNREF(exec_ctx, tcp, "destroy");
}

static void call_read_cb(grpc_exec_ctx *exec_ctx, grpc_tcp *tcp,
                         grpc_error *error) {
  grpc_closure *cb = tcp->read_cb;

  if (grpc_tcp_trace) {
    size_t i;
    const char *str = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "read: error=%s", str);
    grpc_error_free_string(str);
    for (i = 0; i < tcp->incoming_buffer->count; i++) {
      char *dump = grpc_dump_slice(tcp->incoming_buffer->slices[i],
                                   GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "READ %p (peer=%s): %s", tcp, tcp->peer_string, dump);
      gpr_free(dump);
    }
  }

  tcp->read_cb = NULL;
  tcp->incoming_buffer = NULL;
  grpc_closure_run(exec_ctx, cb, error);
}

#define MAX_READ_IOVEC 4
static void tcp_do_read(grpc_exec_ctx *exec_ctx, grpc_tcp *tcp) {
  struct msghdr msg;
  struct iovec iov[MAX_READ_IOVEC];
  ssize_t read_bytes;
  size_t i;

  GPR_ASSERT(!tcp->finished_edge);
  GPR_ASSERT(tcp->iov_size <= MAX_READ_IOVEC);
  GPR_ASSERT(tcp->incoming_buffer->count <= MAX_READ_IOVEC);
  GPR_TIMER_BEGIN("tcp_continue_read", 0);

  for (i = 0; i < tcp->incoming_buffer->count; i++) {
    iov[i].iov_base = GRPC_SLICE_START_PTR(tcp->incoming_buffer->slices[i]);
    iov[i].iov_len = GRPC_SLICE_LENGTH(tcp->incoming_buffer->slices[i]);
  }

  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = iov;
  msg.msg_iovlen = tcp->iov_size;
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;

  GPR_TIMER_BEGIN("recvmsg", 0);
  do {
    read_bytes = recvmsg(tcp->fd, &msg, 0);
  } while (read_bytes < 0 && errno == EINTR);
  GPR_TIMER_END("recvmsg", read_bytes >= 0);

  if (read_bytes < 0) {
    /* NB: After calling call_read_cb a parallel call of the read handler may
     * be running. */
    if (errno == EAGAIN) {
      if (tcp->iov_size > 1) {
        tcp->iov_size /= 2;
      }
      /* We've consumed the edge, request a new one */
      grpc_fd_notify_on_read(exec_ctx, tcp->em_fd, &tcp->read_closure);
    } else {
      grpc_slice_buffer_reset_and_unref_internal(exec_ctx,
                                                 tcp->incoming_buffer);
      call_read_cb(exec_ctx, tcp,
                   tcp_annotate_error(GRPC_OS_ERROR(errno, "recvmsg"), tcp));
      TCP_UNREF(exec_ctx, tcp, "read");
    }
  } else if (read_bytes == 0) {
    /* 0 read size ==> end of stream */
    grpc_slice_buffer_reset_and_unref_internal(exec_ctx, tcp->incoming_buffer);
    call_read_cb(exec_ctx, tcp,
                 tcp_annotate_error(GRPC_ERROR_CREATE("Socket closed"), tcp));
    TCP_UNREF(exec_ctx, tcp, "read");
  } else {
    GPR_ASSERT((size_t)read_bytes <= tcp->incoming_buffer->length);
    if ((size_t)read_bytes < tcp->incoming_buffer->length) {
      grpc_slice_buffer_trim_end(
          tcp->incoming_buffer,
          tcp->incoming_buffer->length - (size_t)read_bytes,
          &tcp->last_read_buffer);
    } else if (tcp->iov_size < MAX_READ_IOVEC) {
      ++tcp->iov_size;
    }
    GPR_ASSERT((size_t)read_bytes == tcp->incoming_buffer->length);
    call_read_cb(exec_ctx, tcp, GRPC_ERROR_NONE);
    TCP_UNREF(exec_ctx, tcp, "read");
  }

  GPR_TIMER_END("tcp_continue_read", 0);
}

static void tcp_read_allocation_done(grpc_exec_ctx *exec_ctx, void *tcpp,
                                     grpc_error *error) {
  grpc_tcp *tcp = tcpp;
  if (error != GRPC_ERROR_NONE) {
    grpc_slice_buffer_reset_and_unref_internal(exec_ctx, tcp->incoming_buffer);
    grpc_slice_buffer_reset_and_unref_internal(exec_ctx,
                                               &tcp->last_read_buffer);
    call_read_cb(exec_ctx, tcp, GRPC_ERROR_REF(error));
    TCP_UNREF(exec_ctx, tcp, "read");
  } else {
    tcp_do_read(exec_ctx, tcp);
  }
}

static void tcp_continue_read(grpc_exec_ctx *exec_ctx, grpc_tcp *tcp) {
  if (tcp->incoming_buffer->count < (size_t)tcp->iov_size) {
    grpc_resource_user_alloc_slices(
        exec_ctx, &tcp->slice_allocator, tcp->slice_size,
        (size_t)tcp->iov_size - tcp->incoming_buffer->count,
        tcp->incoming_buffer);
  } else {
    tcp_do_read(exec_ctx, tcp);
  }
}

static void tcp_handle_read(grpc_exec_ctx *exec_ctx, void *arg /* grpc_tcp */,
                            grpc_error *error) {
  grpc_tcp *tcp = (grpc_tcp *)arg;
  GPR_ASSERT(!tcp->finished_edge);

  if (error != GRPC_ERROR_NONE) {
    grpc_slice_buffer_reset_and_unref_internal(exec_ctx, tcp->incoming_buffer);
    grpc_slice_buffer_reset_and_unref_internal(exec_ctx,
                                               &tcp->last_read_buffer);
    call_read_cb(exec_ctx, tcp, GRPC_ERROR_REF(error));
    TCP_UNREF(exec_ctx, tcp, "read");
  } else {
    tcp_continue_read(exec_ctx, tcp);
  }
}

static void tcp_read(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                     grpc_slice_buffer *incoming_buffer, grpc_closure *cb) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  GPR_ASSERT(tcp->read_cb == NULL);
  tcp->read_cb = cb;
  tcp->incoming_buffer = incoming_buffer;
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, incoming_buffer);
  grpc_slice_buffer_swap(incoming_buffer, &tcp->last_read_buffer);
  TCP_REF(tcp, "read");
  if (tcp->finished_edge) {
    tcp->finished_edge = false;
    grpc_fd_notify_on_read(exec_ctx, tcp->em_fd, &tcp->read_closure);
  } else {
    grpc_closure_sched(exec_ctx, &tcp->read_closure, GRPC_ERROR_NONE);
  }
}

/* returns true if done, false if pending; if returning true, *error is set */
#define MAX_WRITE_IOVEC 1000
static bool tcp_flush(grpc_tcp *tcp, grpc_error **error) {
  struct msghdr msg;
  struct iovec iov[MAX_WRITE_IOVEC];
  msg_iovlen_type iov_size;
  ssize_t sent_length;
  size_t sending_length;
  size_t trailing;
  size_t unwind_slice_idx;
  size_t unwind_byte_idx;

  for (;;) {
    sending_length = 0;
    unwind_slice_idx = tcp->outgoing_slice_idx;
    unwind_byte_idx = tcp->outgoing_byte_idx;
    for (iov_size = 0; tcp->outgoing_slice_idx != tcp->outgoing_buffer->count &&
                       iov_size != MAX_WRITE_IOVEC;
         iov_size++) {
      iov[iov_size].iov_base =
          GRPC_SLICE_START_PTR(
              tcp->outgoing_buffer->slices[tcp->outgoing_slice_idx]) +
          tcp->outgoing_byte_idx;
      iov[iov_size].iov_len =
          GRPC_SLICE_LENGTH(
              tcp->outgoing_buffer->slices[tcp->outgoing_slice_idx]) -
          tcp->outgoing_byte_idx;
      sending_length += iov[iov_size].iov_len;
      tcp->outgoing_slice_idx++;
      tcp->outgoing_byte_idx = 0;
    }
    GPR_ASSERT(iov_size > 0);

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = iov_size;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    GPR_TIMER_BEGIN("sendmsg", 1);
    do {
      /* TODO(klempner): Cork if this is a partial write */
      sent_length = sendmsg(tcp->fd, &msg, SENDMSG_FLAGS);
    } while (sent_length < 0 && errno == EINTR);
    GPR_TIMER_END("sendmsg", 0);

    if (sent_length < 0) {
      if (errno == EAGAIN) {
        tcp->outgoing_slice_idx = unwind_slice_idx;
        tcp->outgoing_byte_idx = unwind_byte_idx;
        return false;
      } else if (errno == EPIPE) {
        *error = grpc_error_set_int(GRPC_OS_ERROR(errno, "sendmsg"),
                                    GRPC_ERROR_INT_GRPC_STATUS,
                                    GRPC_STATUS_UNAVAILABLE);
        return true;
      } else {
        *error = tcp_annotate_error(GRPC_OS_ERROR(errno, "sendmsg"), tcp);
        return true;
      }
    }

    GPR_ASSERT(tcp->outgoing_byte_idx == 0);
    trailing = sending_length - (size_t)sent_length;
    while (trailing > 0) {
      size_t slice_length;

      tcp->outgoing_slice_idx--;
      slice_length = GRPC_SLICE_LENGTH(
          tcp->outgoing_buffer->slices[tcp->outgoing_slice_idx]);
      if (slice_length > trailing) {
        tcp->outgoing_byte_idx = slice_length - trailing;
        break;
      } else {
        trailing -= slice_length;
      }
    }

    if (tcp->outgoing_slice_idx == tcp->outgoing_buffer->count) {
      *error = GRPC_ERROR_NONE;
      return true;
    }
  };
}

static void tcp_handle_write(grpc_exec_ctx *exec_ctx, void *arg /* grpc_tcp */,
                             grpc_error *error) {
  grpc_tcp *tcp = (grpc_tcp *)arg;
  grpc_closure *cb;

  if (error != GRPC_ERROR_NONE) {
    cb = tcp->write_cb;
    tcp->write_cb = NULL;
    cb->cb(exec_ctx, cb->cb_arg, error);
    TCP_UNREF(exec_ctx, tcp, "write");
    return;
  }

  if (!tcp_flush(tcp, &error)) {
    if (grpc_tcp_trace) {
      gpr_log(GPR_DEBUG, "write: delayed");
    }
    grpc_fd_notify_on_write(exec_ctx, tcp->em_fd, &tcp->write_closure);
  } else {
    cb = tcp->write_cb;
    tcp->write_cb = NULL;
    if (grpc_tcp_trace) {
      const char *str = grpc_error_string(error);
      gpr_log(GPR_DEBUG, "write: %s", str);
      grpc_error_free_string(str);
    }

    grpc_closure_run(exec_ctx, cb, error);
    TCP_UNREF(exec_ctx, tcp, "write");
  }
}

static void tcp_write(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                      grpc_slice_buffer *buf, grpc_closure *cb) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_error *error = GRPC_ERROR_NONE;

  if (grpc_tcp_trace) {
    size_t i;

    for (i = 0; i < buf->count; i++) {
      char *data =
          grpc_dump_slice(buf->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "WRITE %p (peer=%s): %s", tcp, tcp->peer_string, data);
      gpr_free(data);
    }
  }

  GPR_TIMER_BEGIN("tcp_write", 0);
  GPR_ASSERT(tcp->write_cb == NULL);

  if (buf->length == 0) {
    GPR_TIMER_END("tcp_write", 0);
    grpc_closure_sched(exec_ctx, cb,
                       grpc_fd_is_shutdown(tcp->em_fd)
                           ? tcp_annotate_error(GRPC_ERROR_CREATE("EOF"), tcp)
                           : GRPC_ERROR_NONE);
    return;
  }
  tcp->outgoing_buffer = buf;
  tcp->outgoing_slice_idx = 0;
  tcp->outgoing_byte_idx = 0;

  if (!tcp_flush(tcp, &error)) {
    TCP_REF(tcp, "write");
    tcp->write_cb = cb;
    if (grpc_tcp_trace) {
      gpr_log(GPR_DEBUG, "write: delayed");
    }
    grpc_fd_notify_on_write(exec_ctx, tcp->em_fd, &tcp->write_closure);
  } else {
    if (grpc_tcp_trace) {
      const char *str = grpc_error_string(error);
      gpr_log(GPR_DEBUG, "write: %s", str);
      grpc_error_free_string(str);
    }
    grpc_closure_sched(exec_ctx, cb, error);
  }

  GPR_TIMER_END("tcp_write", 0);
}

static void tcp_add_to_pollset(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                               grpc_pollset *pollset) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_pollset_add_fd(exec_ctx, pollset, tcp->em_fd);
}

static void tcp_add_to_pollset_set(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                                   grpc_pollset_set *pollset_set) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_pollset_set_add_fd(exec_ctx, pollset_set, tcp->em_fd);
}

static char *tcp_get_peer(grpc_endpoint *ep) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  return gpr_strdup(tcp->peer_string);
}

static int tcp_get_fd(grpc_endpoint *ep) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  return tcp->fd;
}

static grpc_workqueue *tcp_get_workqueue(grpc_endpoint *ep) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  return grpc_fd_get_workqueue(tcp->em_fd);
}

static grpc_resource_user *tcp_get_resource_user(grpc_endpoint *ep) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  return tcp->resource_user;
}

static const grpc_endpoint_vtable vtable = {tcp_read,
                                            tcp_write,
                                            tcp_get_workqueue,
                                            tcp_add_to_pollset,
                                            tcp_add_to_pollset_set,
                                            tcp_shutdown,
                                            tcp_destroy,
                                            tcp_get_resource_user,
                                            tcp_get_peer,
                                            tcp_get_fd};

grpc_endpoint *grpc_tcp_create(grpc_fd *em_fd,
                               grpc_resource_quota *resource_quota,
                               size_t slice_size, const char *peer_string) {
  grpc_tcp *tcp = (grpc_tcp *)gpr_malloc(sizeof(grpc_tcp));
  tcp->base.vtable = &vtable;
  tcp->peer_string = gpr_strdup(peer_string);
  tcp->fd = grpc_fd_wrapped_fd(em_fd);
  tcp->read_cb = NULL;
  tcp->write_cb = NULL;
  tcp->release_fd_cb = NULL;
  tcp->release_fd = NULL;
  tcp->incoming_buffer = NULL;
  tcp->slice_size = slice_size;
  tcp->iov_size = 1;
  tcp->finished_edge = true;
  /* paired with unref in grpc_tcp_destroy */
  gpr_ref_init(&tcp->refcount, 1);
  gpr_atm_no_barrier_store(&tcp->shutdown_count, 0);
  tcp->em_fd = em_fd;
  grpc_closure_init(&tcp->read_closure, tcp_handle_read, tcp,
                    grpc_schedule_on_exec_ctx);
  grpc_closure_init(&tcp->write_closure, tcp_handle_write, tcp,
                    grpc_schedule_on_exec_ctx);
  grpc_slice_buffer_init(&tcp->last_read_buffer);
  tcp->resource_user = grpc_resource_user_create(resource_quota, peer_string);
  grpc_resource_user_slice_allocator_init(
      &tcp->slice_allocator, tcp->resource_user, tcp_read_allocation_done, tcp);
  /* Tell network status tracker about new endpoint */
  grpc_network_status_register_endpoint(&tcp->base);

  return &tcp->base;
}

int grpc_tcp_fd(grpc_endpoint *ep) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  GPR_ASSERT(ep->vtable == &vtable);
  return grpc_fd_wrapped_fd(tcp->em_fd);
}

void grpc_tcp_destroy_and_release_fd(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                                     int *fd, grpc_closure *done) {
  grpc_network_status_unregister_endpoint(ep);
  grpc_tcp *tcp = (grpc_tcp *)ep;
  GPR_ASSERT(ep->vtable == &vtable);
  tcp->release_fd = fd;
  tcp->release_fd_cb = done;
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &tcp->last_read_buffer);
  TCP_UNREF(exec_ctx, tcp, "destroy");
}

#endif
