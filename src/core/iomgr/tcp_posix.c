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

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_SOCKET

#include "src/core/iomgr/tcp_posix.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/support/string.h"
#include "src/core/debug/trace.h"
#include "src/core/profiling/timers.h"

#ifdef GPR_HAVE_MSG_NOSIGNAL
#define SENDMSG_FLAGS MSG_NOSIGNAL
#else
#define SENDMSG_FLAGS 0
#endif

int grpc_tcp_trace = 0;

typedef struct {
  grpc_endpoint base;
  grpc_fd *em_fd;
  int fd;
  int iov_size; /* Number of slices to allocate per read attempt */
  int finished_edge;
  size_t slice_size;
  gpr_refcount refcount;

  gpr_slice_buffer *incoming_buffer;
  gpr_slice_buffer *outgoing_buffer;
  /** slice within outgoing_buffer to write next */
  size_t outgoing_slice_idx;
  /** byte within outgoing_buffer->slices[outgoing_slice_idx] to write next */
  size_t outgoing_byte_idx;

  grpc_iomgr_closure *read_cb;
  grpc_iomgr_closure *write_cb;

  grpc_iomgr_closure read_closure;
  grpc_iomgr_closure write_closure;

  char *peer_string;
} grpc_tcp;

static void tcp_handle_read(void *arg /* grpc_tcp */, int success);
static void tcp_handle_write(void *arg /* grpc_tcp */, int success);

static void tcp_shutdown(grpc_endpoint *ep) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_fd_shutdown(tcp->em_fd);
}

static void tcp_free(grpc_tcp *tcp) {
  grpc_fd_orphan(tcp->em_fd, NULL, "tcp_unref_orphan");
  gpr_free(tcp->peer_string);
  gpr_free(tcp);
}

/*#define GRPC_TCP_REFCOUNT_DEBUG*/
#ifdef GRPC_TCP_REFCOUNT_DEBUG
#define TCP_UNREF(tcp, reason) tcp_unref((tcp), (reason), __FILE__, __LINE__)
#define TCP_REF(tcp, reason) tcp_ref((tcp), (reason), __FILE__, __LINE__)
static void tcp_unref(grpc_tcp *tcp, const char *reason, const char *file,
                      int line) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "TCP unref %p : %s %d -> %d", tcp,
          reason, tcp->refcount.count, tcp->refcount.count - 1);
  if (gpr_unref(&tcp->refcount)) {
    tcp_free(tcp);
  }
}

static void tcp_ref(grpc_tcp *tcp, const char *reason, const char *file,
                    int line) {
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "TCP   ref %p : %s %d -> %d", tcp,
          reason, tcp->refcount.count, tcp->refcount.count + 1);
  gpr_ref(&tcp->refcount);
}
#else
#define TCP_UNREF(tcp, reason) tcp_unref((tcp))
#define TCP_REF(tcp, reason) tcp_ref((tcp))
static void tcp_unref(grpc_tcp *tcp) {
  if (gpr_unref(&tcp->refcount)) {
    tcp_free(tcp);
  }
}

static void tcp_ref(grpc_tcp *tcp) { gpr_ref(&tcp->refcount); }
#endif

static void tcp_destroy(grpc_endpoint *ep) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  TCP_UNREF(tcp, "destroy");
}

static void call_read_cb(grpc_tcp *tcp, int success) {
  grpc_iomgr_closure *cb = tcp->read_cb;

  if (grpc_tcp_trace) {
    size_t i;
    gpr_log(GPR_DEBUG, "read: success=%d", success);
    for (i = 0; i < tcp->incoming_buffer->count; i++) {
      char *dump = gpr_dump_slice(tcp->incoming_buffer->slices[i],
                                  GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "READ %p: %s", tcp, dump);
      gpr_free(dump);
    }
  }

  tcp->read_cb = NULL;
  tcp->incoming_buffer = NULL;
  cb->cb(cb->cb_arg, success);
}

#define MAX_READ_IOVEC 4
static void tcp_continue_read(grpc_tcp *tcp) {
  struct msghdr msg;
  struct iovec iov[MAX_READ_IOVEC];
  ssize_t read_bytes;
  size_t i;

  GPR_ASSERT(!tcp->finished_edge);
  GPR_ASSERT(tcp->iov_size <= MAX_READ_IOVEC);
  GPR_ASSERT(tcp->incoming_buffer->count <= MAX_READ_IOVEC);
  GRPC_TIMER_BEGIN(GRPC_PTAG_HANDLE_READ, 0);

  while (tcp->incoming_buffer->count < (size_t)tcp->iov_size) {
    gpr_slice_buffer_add_indexed(tcp->incoming_buffer,
                                 gpr_slice_malloc(tcp->slice_size));
  }
  for (i = 0; i < tcp->incoming_buffer->count; i++) {
    iov[i].iov_base = GPR_SLICE_START_PTR(tcp->incoming_buffer->slices[i]);
    iov[i].iov_len = GPR_SLICE_LENGTH(tcp->incoming_buffer->slices[i]);
  }

  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = iov;
  msg.msg_iovlen = tcp->iov_size;
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;

  GRPC_TIMER_BEGIN(GRPC_PTAG_RECVMSG, 0);
  do {
    read_bytes = recvmsg(tcp->fd, &msg, 0);
  } while (read_bytes < 0 && errno == EINTR);
  GRPC_TIMER_END(GRPC_PTAG_RECVMSG, 0);

  if (read_bytes < 0) {
    /* NB: After calling call_read_cb a parallel call of the read handler may
     * be running. */
    if (errno == EAGAIN) {
      if (tcp->iov_size > 1) {
        tcp->iov_size /= 2;
      }
      /* We've consumed the edge, request a new one */
      grpc_fd_notify_on_read(tcp->em_fd, &tcp->read_closure);
    } else {
      /* TODO(klempner): Log interesting errors */
      gpr_slice_buffer_reset_and_unref(tcp->incoming_buffer);
      call_read_cb(tcp, 0);
      TCP_UNREF(tcp, "read");
    }
  } else if (read_bytes == 0) {
    /* 0 read size ==> end of stream */
    gpr_slice_buffer_reset_and_unref(tcp->incoming_buffer);
    call_read_cb(tcp, 0);
    TCP_UNREF(tcp, "read");
  } else {
    GPR_ASSERT((size_t)read_bytes <= tcp->incoming_buffer->length);
    if ((size_t)read_bytes < tcp->incoming_buffer->length) {
      gpr_slice_buffer_trim_end(tcp->incoming_buffer,
                                tcp->incoming_buffer->length - read_bytes);
    } else if (tcp->iov_size < MAX_READ_IOVEC) {
      ++tcp->iov_size;
    }
    GPR_ASSERT((size_t)read_bytes == tcp->incoming_buffer->length);
    call_read_cb(tcp, 1);
    TCP_UNREF(tcp, "read");
  }

  GRPC_TIMER_END(GRPC_PTAG_HANDLE_READ, 0);
}

static void tcp_handle_read(void *arg /* grpc_tcp */, int success) {
  grpc_tcp *tcp = (grpc_tcp *)arg;
  GPR_ASSERT(!tcp->finished_edge);

  if (!success) {
    gpr_slice_buffer_reset_and_unref(tcp->incoming_buffer);
    call_read_cb(tcp, 0);
    TCP_UNREF(tcp, "read");
  } else {
    tcp_continue_read(tcp);
  }
}

static grpc_endpoint_op_status tcp_read(grpc_endpoint *ep,
                                        gpr_slice_buffer *incoming_buffer,
                                        grpc_iomgr_closure *cb) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  GPR_ASSERT(tcp->read_cb == NULL);
  tcp->read_cb = cb;
  tcp->incoming_buffer = incoming_buffer;
  gpr_slice_buffer_reset_and_unref(incoming_buffer);
  TCP_REF(tcp, "read");
  if (tcp->finished_edge) {
    tcp->finished_edge = 0;
    grpc_fd_notify_on_read(tcp->em_fd, &tcp->read_closure);
  } else {
    grpc_iomgr_add_delayed_callback(&tcp->read_closure, 1);
  }
  /* TODO(ctiller): immediate return */
  return GRPC_ENDPOINT_PENDING;
}

#define MAX_WRITE_IOVEC 16
static grpc_endpoint_op_status tcp_flush(grpc_tcp *tcp) {
  struct msghdr msg;
  struct iovec iov[MAX_WRITE_IOVEC];
  int iov_size;
  ssize_t sent_length;
  ssize_t sending_length;
  ssize_t trailing;
  ssize_t unwind_slice_idx;
  ssize_t unwind_byte_idx;

  for (;;) {
    sending_length = 0;
    unwind_slice_idx = tcp->outgoing_slice_idx;
    unwind_byte_idx = tcp->outgoing_byte_idx;
    for (iov_size = 0; tcp->outgoing_slice_idx != tcp->outgoing_buffer->count &&
                       iov_size != MAX_WRITE_IOVEC;
         iov_size++) {
      iov[iov_size].iov_base =
          GPR_SLICE_START_PTR(
              tcp->outgoing_buffer->slices[tcp->outgoing_slice_idx]) +
          tcp->outgoing_byte_idx;
      iov[iov_size].iov_len =
          GPR_SLICE_LENGTH(
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

    GRPC_TIMER_BEGIN(GRPC_PTAG_SENDMSG, 0);
    do {
      /* TODO(klempner): Cork if this is a partial write */
      sent_length = sendmsg(tcp->fd, &msg, SENDMSG_FLAGS);
    } while (sent_length < 0 && errno == EINTR);
    GRPC_TIMER_END(GRPC_PTAG_SENDMSG, 0);

    if (sent_length < 0) {
      if (errno == EAGAIN) {
        tcp->outgoing_slice_idx = unwind_slice_idx;
        tcp->outgoing_byte_idx = unwind_byte_idx;
        return GRPC_ENDPOINT_PENDING;
      } else {
        /* TODO(klempner): Log some of these */
        return GRPC_ENDPOINT_ERROR;
      }
    }

    GPR_ASSERT(tcp->outgoing_byte_idx == 0);
    trailing = sending_length - sent_length;
    while (trailing > 0) {
      ssize_t slice_length;

      tcp->outgoing_slice_idx--;
      slice_length = GPR_SLICE_LENGTH(
          tcp->outgoing_buffer->slices[tcp->outgoing_slice_idx]);
      if (slice_length > trailing) {
        tcp->outgoing_byte_idx = slice_length - trailing;
        break;
      } else {
        trailing -= slice_length;
      }
    }

    if (tcp->outgoing_slice_idx == tcp->outgoing_buffer->count) {
      return GRPC_ENDPOINT_DONE;
    }
  };
}

static void tcp_handle_write(void *arg /* grpc_tcp */, int success) {
  grpc_tcp *tcp = (grpc_tcp *)arg;
  grpc_endpoint_op_status status;
  grpc_iomgr_closure *cb;

  if (!success) {
    cb = tcp->write_cb;
    tcp->write_cb = NULL;
    cb->cb(cb->cb_arg, 0);
    TCP_UNREF(tcp, "write");
    return;
  }

  GRPC_TIMER_BEGIN(GRPC_PTAG_TCP_CB_WRITE, 0);
  status = tcp_flush(tcp);
  if (status == GRPC_ENDPOINT_PENDING) {
    grpc_fd_notify_on_write(tcp->em_fd, &tcp->write_closure);
  } else {
    cb = tcp->write_cb;
    tcp->write_cb = NULL;
    cb->cb(cb->cb_arg, status == GRPC_ENDPOINT_DONE);
    TCP_UNREF(tcp, "write");
  }
  GRPC_TIMER_END(GRPC_PTAG_TCP_CB_WRITE, 0);
}

static grpc_endpoint_op_status tcp_write(grpc_endpoint *ep,
                                         gpr_slice_buffer *buf,
                                         grpc_iomgr_closure *cb) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_endpoint_op_status status;

  if (grpc_tcp_trace) {
    size_t i;

    for (i = 0; i < buf->count; i++) {
      char *data =
          gpr_dump_slice(buf->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "WRITE %p: %s", tcp, data);
      gpr_free(data);
    }
  }

  GRPC_TIMER_BEGIN(GRPC_PTAG_TCP_WRITE, 0);
  GPR_ASSERT(tcp->write_cb == NULL);

  if (buf->length == 0) {
    GRPC_TIMER_END(GRPC_PTAG_TCP_WRITE, 0);
    return GRPC_ENDPOINT_DONE;
  }
  tcp->outgoing_buffer = buf;
  tcp->outgoing_slice_idx = 0;
  tcp->outgoing_byte_idx = 0;

  status = tcp_flush(tcp);
  if (status == GRPC_ENDPOINT_PENDING) {
    TCP_REF(tcp, "write");
    tcp->write_cb = cb;
    grpc_fd_notify_on_write(tcp->em_fd, &tcp->write_closure);
  }

  GRPC_TIMER_END(GRPC_PTAG_TCP_WRITE, 0);
  return status;
}

static void tcp_add_to_pollset(grpc_endpoint *ep, grpc_pollset *pollset) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_pollset_add_fd(pollset, tcp->em_fd);
}

static void tcp_add_to_pollset_set(grpc_endpoint *ep,
                                   grpc_pollset_set *pollset_set) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_pollset_set_add_fd(pollset_set, tcp->em_fd);
}

static char *tcp_get_peer(grpc_endpoint *ep) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  return gpr_strdup(tcp->peer_string);
}

static const grpc_endpoint_vtable vtable = {
    tcp_read,     tcp_write,   tcp_add_to_pollset, tcp_add_to_pollset_set,
    tcp_shutdown, tcp_destroy, tcp_get_peer};

grpc_endpoint *grpc_tcp_create(grpc_fd *em_fd, size_t slice_size,
                               const char *peer_string) {
  grpc_tcp *tcp = (grpc_tcp *)gpr_malloc(sizeof(grpc_tcp));
  tcp->base.vtable = &vtable;
  tcp->peer_string = gpr_strdup(peer_string);
  tcp->fd = em_fd->fd;
  tcp->read_cb = NULL;
  tcp->write_cb = NULL;
  tcp->incoming_buffer = NULL;
  tcp->slice_size = slice_size;
  tcp->iov_size = 1;
  tcp->finished_edge = 1;
  /* paired with unref in grpc_tcp_destroy */
  gpr_ref_init(&tcp->refcount, 1);
  tcp->em_fd = em_fd;
  tcp->read_closure.cb = tcp_handle_read;
  tcp->read_closure.cb_arg = tcp;
  tcp->write_closure.cb = tcp_handle_write;
  tcp->write_closure.cb_arg = tcp;

  return &tcp->base;
}

#endif
