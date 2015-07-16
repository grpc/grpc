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

#include "src/core/support/string.h"
#include "src/core/debug/trace.h"
#include "src/core/profiling/timers.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#ifdef GPR_HAVE_MSG_NOSIGNAL
#define SENDMSG_FLAGS MSG_NOSIGNAL
#else
#define SENDMSG_FLAGS 0
#endif

/* Holds a slice array and associated state. */
typedef struct grpc_tcp_slice_state {
  gpr_slice *slices;       /* Array of slices */
  size_t nslices;          /* Size of slices array. */
  ssize_t first_slice;     /* First valid slice in array */
  ssize_t last_slice;      /* Last valid slice in array */
  gpr_slice working_slice; /* pointer to original final slice */
  int working_slice_valid; /* True if there is a working slice */
  int memory_owned;        /* True if slices array is owned */
} grpc_tcp_slice_state;

int grpc_tcp_trace = 0;

static void slice_state_init(grpc_tcp_slice_state *state, gpr_slice *slices,
                             size_t nslices, size_t valid_slices) {
  state->slices = slices;
  state->nslices = nslices;
  if (valid_slices == 0) {
    state->first_slice = -1;
  } else {
    state->first_slice = 0;
  }
  state->last_slice = valid_slices - 1;
  state->working_slice_valid = 0;
  state->memory_owned = 0;
}

/* Returns true if there is still available data */
static int slice_state_has_available(grpc_tcp_slice_state *state) {
  return state->first_slice != -1 && state->last_slice >= state->first_slice;
}

static ssize_t slice_state_slices_allocated(grpc_tcp_slice_state *state) {
  if (state->first_slice == -1) {
    return 0;
  } else {
    return state->last_slice - state->first_slice + 1;
  }
}

static void slice_state_realloc(grpc_tcp_slice_state *state, size_t new_size) {
  /* TODO(klempner): use realloc instead when first_slice is 0 */
  /* TODO(klempner): Avoid a realloc in cases where it is unnecessary */
  gpr_slice *slices = state->slices;
  size_t original_size = slice_state_slices_allocated(state);
  size_t i;
  gpr_slice *new_slices = gpr_malloc(sizeof(gpr_slice) * new_size);

  for (i = 0; i < original_size; ++i) {
    new_slices[i] = slices[i + state->first_slice];
  }

  state->slices = new_slices;
  state->last_slice = original_size - 1;
  if (original_size > 0) {
    state->first_slice = 0;
  } else {
    state->first_slice = -1;
  }
  state->nslices = new_size;

  if (state->memory_owned) {
    gpr_free(slices);
  }
  state->memory_owned = 1;
}

static void slice_state_remove_prefix(grpc_tcp_slice_state *state,
                                      size_t prefix_bytes) {
  gpr_slice *current_slice = &state->slices[state->first_slice];
  size_t current_slice_size;

  while (slice_state_has_available(state)) {
    current_slice_size = GPR_SLICE_LENGTH(*current_slice);
    if (current_slice_size > prefix_bytes) {
      /* TODO(klempner): Get rid of the extra refcount created here by adding a
         native "trim the first N bytes" operation to splice */
      /* TODO(klempner): This really shouldn't be modifying the current slice
         unless we own the slices array. */
      gpr_slice tail;
      tail = gpr_slice_split_tail(current_slice, prefix_bytes);
      gpr_slice_unref(*current_slice);
      *current_slice = tail;
      return;
    } else {
      gpr_slice_unref(*current_slice);
      ++state->first_slice;
      ++current_slice;
      prefix_bytes -= current_slice_size;
    }
  }
}

static void slice_state_destroy(grpc_tcp_slice_state *state) {
  while (slice_state_has_available(state)) {
    gpr_slice_unref(state->slices[state->first_slice]);
    ++state->first_slice;
  }

  if (state->memory_owned) {
    gpr_free(state->slices);
    state->memory_owned = 0;
  }
}

void slice_state_transfer_ownership(grpc_tcp_slice_state *state,
                                    gpr_slice **slices, size_t *nslices) {
  *slices = state->slices + state->first_slice;
  *nslices = state->last_slice - state->first_slice + 1;

  state->first_slice = -1;
  state->last_slice = -1;
}

/* Fills iov with the first min(iov_size, available) slices, returns number
   filled */
static size_t slice_state_to_iovec(grpc_tcp_slice_state *state,
                                   struct iovec *iov, size_t iov_size) {
  size_t nslices = state->last_slice - state->first_slice + 1;
  gpr_slice *slices = state->slices + state->first_slice;
  size_t i;
  if (nslices < iov_size) {
    iov_size = nslices;
  }

  for (i = 0; i < iov_size; ++i) {
    iov[i].iov_base = GPR_SLICE_START_PTR(slices[i]);
    iov[i].iov_len = GPR_SLICE_LENGTH(slices[i]);
  }
  return iov_size;
}

/* Makes n blocks available at the end of state, writes them into iov, and
   returns the number of bytes allocated */
static size_t slice_state_append_blocks_into_iovec(grpc_tcp_slice_state *state,
                                                   struct iovec *iov, size_t n,
                                                   size_t slice_size) {
  size_t target_size;
  size_t i;
  size_t allocated_bytes;
  ssize_t allocated_slices = slice_state_slices_allocated(state);

  if (n - state->working_slice_valid >= state->nslices - state->last_slice) {
    /* Need to grow the slice array */
    target_size = state->nslices;
    do {
      target_size = target_size * 2;
    } while (target_size < allocated_slices + n - state->working_slice_valid);
    /* TODO(klempner): If this ever needs to support both prefix removal and
       append, we should be smarter about the growth logic here */
    slice_state_realloc(state, target_size);
  }

  i = 0;
  allocated_bytes = 0;

  if (state->working_slice_valid) {
    iov[0].iov_base = GPR_SLICE_END_PTR(state->slices[state->last_slice]);
    iov[0].iov_len = GPR_SLICE_LENGTH(state->working_slice) -
                     GPR_SLICE_LENGTH(state->slices[state->last_slice]);
    allocated_bytes += iov[0].iov_len;
    ++i;
    state->slices[state->last_slice] = state->working_slice;
    state->working_slice_valid = 0;
  }

  for (; i < n; ++i) {
    ++state->last_slice;
    state->slices[state->last_slice] = gpr_slice_malloc(slice_size);
    iov[i].iov_base = GPR_SLICE_START_PTR(state->slices[state->last_slice]);
    iov[i].iov_len = slice_size;
    allocated_bytes += slice_size;
  }
  if (state->first_slice == -1) {
    state->first_slice = 0;
  }
  return allocated_bytes;
}

/* Remove the last n bytes from state */
/* TODO(klempner): Consider having this defer actual deletion until later */
static void slice_state_remove_last(grpc_tcp_slice_state *state, size_t bytes) {
  while (bytes > 0 && slice_state_has_available(state)) {
    if (GPR_SLICE_LENGTH(state->slices[state->last_slice]) > bytes) {
      state->working_slice = state->slices[state->last_slice];
      state->working_slice_valid = 1;
      /* TODO(klempner): Combine these into a single operation that doesn't need
         to refcount */
      gpr_slice_unref(gpr_slice_split_tail(
          &state->slices[state->last_slice],
          GPR_SLICE_LENGTH(state->slices[state->last_slice]) - bytes));
      bytes = 0;
    } else {
      bytes -= GPR_SLICE_LENGTH(state->slices[state->last_slice]);
      gpr_slice_unref(state->slices[state->last_slice]);
      --state->last_slice;
      if (state->last_slice == -1) {
        state->first_slice = -1;
      }
    }
  }
}

typedef struct {
  grpc_endpoint base;
  grpc_fd *em_fd;
  int fd;
  int iov_size; /* Number of slices to allocate per read attempt */
  int finished_edge;
  size_t slice_size;
  gpr_refcount refcount;

  grpc_endpoint_read_cb read_cb;
  void *read_user_data;
  grpc_endpoint_write_cb write_cb;
  void *write_user_data;

  grpc_tcp_slice_state write_state;

  grpc_iomgr_closure read_closure;
  grpc_iomgr_closure write_closure;

  grpc_iomgr_closure handle_read_closure;
} grpc_tcp;

static void grpc_tcp_handle_read(void *arg /* grpc_tcp */, int success);
static void grpc_tcp_handle_write(void *arg /* grpc_tcp */, int success);

static void grpc_tcp_shutdown(grpc_endpoint *ep) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_fd_shutdown(tcp->em_fd);
}

static void grpc_tcp_unref(grpc_tcp *tcp) {
  int refcount_zero = gpr_unref(&tcp->refcount);
  if (refcount_zero) {
    grpc_fd_orphan(tcp->em_fd, NULL, "tcp_unref_orphan");
    gpr_free(tcp);
  }
}

static void grpc_tcp_destroy(grpc_endpoint *ep) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_tcp_unref(tcp);
}

static void call_read_cb(grpc_tcp *tcp, gpr_slice *slices, size_t nslices,
                         grpc_endpoint_cb_status status) {
  grpc_endpoint_read_cb cb = tcp->read_cb;

  if (grpc_tcp_trace) {
    size_t i;
    gpr_log(GPR_DEBUG, "read: status=%d", status);
    for (i = 0; i < nslices; i++) {
      char *dump = gpr_dump_slice(slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "READ: %s", dump);
      gpr_free(dump);
    }
  }

  tcp->read_cb = NULL;
  cb(tcp->read_user_data, slices, nslices, status);
}

#define INLINE_SLICE_BUFFER_SIZE 8
#define MAX_READ_IOVEC 4
static void grpc_tcp_continue_read(grpc_tcp *tcp) {
  gpr_slice static_read_slices[INLINE_SLICE_BUFFER_SIZE];
  struct msghdr msg;
  struct iovec iov[MAX_READ_IOVEC];
  ssize_t read_bytes;
  ssize_t allocated_bytes;
  struct grpc_tcp_slice_state read_state;
  gpr_slice *final_slices;
  size_t final_nslices;

  GPR_ASSERT(!tcp->finished_edge);
  GRPC_TIMER_BEGIN(GRPC_PTAG_HANDLE_READ, 0);
  slice_state_init(&read_state, static_read_slices, INLINE_SLICE_BUFFER_SIZE,
                   0);

  allocated_bytes = slice_state_append_blocks_into_iovec(
      &read_state, iov, tcp->iov_size, tcp->slice_size);

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

  if (read_bytes < allocated_bytes) {
    /* TODO(klempner): Consider a second read first, in hopes of getting a
     * quick EAGAIN and saving a bunch of allocations. */
    slice_state_remove_last(&read_state, read_bytes < 0
                                             ? allocated_bytes
                                             : allocated_bytes - read_bytes);
  }

  if (read_bytes < 0) {
    /* NB: After calling the user_cb a parallel call of the read handler may
     * be running. */
    if (errno == EAGAIN) {
      if (tcp->iov_size > 1) {
        tcp->iov_size /= 2;
      }
      if (slice_state_has_available(&read_state)) {
        /* TODO(klempner): We should probably do the call into the application
           without all this junk on the stack */
        /* FIXME(klempner): Refcount properly */
        slice_state_transfer_ownership(&read_state, &final_slices,
                                       &final_nslices);
        tcp->finished_edge = 1;
        call_read_cb(tcp, final_slices, final_nslices, GRPC_ENDPOINT_CB_OK);
        slice_state_destroy(&read_state);
        grpc_tcp_unref(tcp);
      } else {
        /* We've consumed the edge, request a new one */
        slice_state_destroy(&read_state);
        grpc_fd_notify_on_read(tcp->em_fd, &tcp->read_closure);
      }
    } else {
      /* TODO(klempner): Log interesting errors */
      call_read_cb(tcp, NULL, 0, GRPC_ENDPOINT_CB_ERROR);
      slice_state_destroy(&read_state);
      grpc_tcp_unref(tcp);
    }
  } else if (read_bytes == 0) {
    /* 0 read size ==> end of stream */
    if (slice_state_has_available(&read_state)) {
      /* there were bytes already read: pass them up to the application */
      slice_state_transfer_ownership(&read_state, &final_slices,
                                     &final_nslices);
      call_read_cb(tcp, final_slices, final_nslices, GRPC_ENDPOINT_CB_EOF);
    } else {
      call_read_cb(tcp, NULL, 0, GRPC_ENDPOINT_CB_EOF);
    }
    slice_state_destroy(&read_state);
    grpc_tcp_unref(tcp);
  } else {
    if (tcp->iov_size < MAX_READ_IOVEC) {
      ++tcp->iov_size;
    }
    GPR_ASSERT(slice_state_has_available(&read_state));
    slice_state_transfer_ownership(&read_state, &final_slices, &final_nslices);
    call_read_cb(tcp, final_slices, final_nslices, GRPC_ENDPOINT_CB_OK);
    slice_state_destroy(&read_state);
    grpc_tcp_unref(tcp);
  }

  GRPC_TIMER_END(GRPC_PTAG_HANDLE_READ, 0);
}

static void grpc_tcp_handle_read(void *arg /* grpc_tcp */, int success) {
  grpc_tcp *tcp = (grpc_tcp *)arg;
  GPR_ASSERT(!tcp->finished_edge);

  if (!success) {
    call_read_cb(tcp, NULL, 0, GRPC_ENDPOINT_CB_SHUTDOWN);
    grpc_tcp_unref(tcp);
  } else {
    grpc_tcp_continue_read(tcp);
  }
}

static void grpc_tcp_notify_on_read(grpc_endpoint *ep, grpc_endpoint_read_cb cb,
                                    void *user_data) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  GPR_ASSERT(tcp->read_cb == NULL);
  tcp->read_cb = cb;
  tcp->read_user_data = user_data;
  gpr_ref(&tcp->refcount);
  if (tcp->finished_edge) {
    tcp->finished_edge = 0;
    grpc_fd_notify_on_read(tcp->em_fd, &tcp->read_closure);
  } else {
    tcp->handle_read_closure.cb_arg = tcp;
    grpc_iomgr_add_callback(&tcp->handle_read_closure);
  }
}

#define MAX_WRITE_IOVEC 16
static grpc_endpoint_write_status grpc_tcp_flush(grpc_tcp *tcp) {
  struct msghdr msg;
  struct iovec iov[MAX_WRITE_IOVEC];
  int iov_size;
  ssize_t sent_length;
  grpc_tcp_slice_state *state = &tcp->write_state;

  for (;;) {
    iov_size = slice_state_to_iovec(state, iov, MAX_WRITE_IOVEC);

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
        return GRPC_ENDPOINT_WRITE_PENDING;
      } else {
        /* TODO(klempner): Log some of these */
        slice_state_destroy(state);
        return GRPC_ENDPOINT_WRITE_ERROR;
      }
    }

    /* TODO(klempner): Probably better to batch this after we finish flushing */
    slice_state_remove_prefix(state, sent_length);

    if (!slice_state_has_available(state)) {
      return GRPC_ENDPOINT_WRITE_DONE;
    }
  };
}

static void grpc_tcp_handle_write(void *arg /* grpc_tcp */, int success) {
  grpc_tcp *tcp = (grpc_tcp *)arg;
  grpc_endpoint_write_status write_status;
  grpc_endpoint_cb_status cb_status;
  grpc_endpoint_write_cb cb;

  if (!success) {
    slice_state_destroy(&tcp->write_state);
    cb = tcp->write_cb;
    tcp->write_cb = NULL;
    cb(tcp->write_user_data, GRPC_ENDPOINT_CB_SHUTDOWN);
    grpc_tcp_unref(tcp);
    return;
  }

  GRPC_TIMER_BEGIN(GRPC_PTAG_TCP_CB_WRITE, 0);
  write_status = grpc_tcp_flush(tcp);
  if (write_status == GRPC_ENDPOINT_WRITE_PENDING) {
    grpc_fd_notify_on_write(tcp->em_fd, &tcp->write_closure);
  } else {
    slice_state_destroy(&tcp->write_state);
    if (write_status == GRPC_ENDPOINT_WRITE_DONE) {
      cb_status = GRPC_ENDPOINT_CB_OK;
    } else {
      cb_status = GRPC_ENDPOINT_CB_ERROR;
    }
    cb = tcp->write_cb;
    tcp->write_cb = NULL;
    cb(tcp->write_user_data, cb_status);
    grpc_tcp_unref(tcp);
  }
  GRPC_TIMER_END(GRPC_PTAG_TCP_CB_WRITE, 0);
}

static grpc_endpoint_write_status grpc_tcp_write(grpc_endpoint *ep,
                                                 gpr_slice *slices,
                                                 size_t nslices,
                                                 grpc_endpoint_write_cb cb,
                                                 void *user_data) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_endpoint_write_status status;

  if (grpc_tcp_trace) {
    size_t i;

    for (i = 0; i < nslices; i++) {
      char *data = gpr_dump_slice(slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "WRITE %p: %s", tcp, data);
      gpr_free(data);
    }
  }

  GRPC_TIMER_BEGIN(GRPC_PTAG_TCP_WRITE, 0);
  GPR_ASSERT(tcp->write_cb == NULL);
  slice_state_init(&tcp->write_state, slices, nslices, nslices);

  status = grpc_tcp_flush(tcp);
  if (status == GRPC_ENDPOINT_WRITE_PENDING) {
    /* TODO(klempner): Consider inlining rather than malloc for small nslices */
    slice_state_realloc(&tcp->write_state, nslices);
    gpr_ref(&tcp->refcount);
    tcp->write_cb = cb;
    tcp->write_user_data = user_data;
    grpc_fd_notify_on_write(tcp->em_fd, &tcp->write_closure);
  }

  GRPC_TIMER_END(GRPC_PTAG_TCP_WRITE, 0);
  return status;
}

static void grpc_tcp_add_to_pollset(grpc_endpoint *ep, grpc_pollset *pollset) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_pollset_add_fd(pollset, tcp->em_fd);
}

static void grpc_tcp_add_to_pollset_set(grpc_endpoint *ep, grpc_pollset_set *pollset_set) {
  grpc_tcp *tcp = (grpc_tcp *)ep;
  grpc_pollset_set_add_fd(pollset_set, tcp->em_fd);
}

static const grpc_endpoint_vtable vtable = {
    grpc_tcp_notify_on_read, grpc_tcp_write, grpc_tcp_add_to_pollset,
    grpc_tcp_add_to_pollset_set, grpc_tcp_shutdown, grpc_tcp_destroy};

grpc_endpoint *grpc_tcp_create(grpc_fd *em_fd, size_t slice_size) {
  grpc_tcp *tcp = (grpc_tcp *)gpr_malloc(sizeof(grpc_tcp));
  tcp->base.vtable = &vtable;
  tcp->fd = em_fd->fd;
  tcp->read_cb = NULL;
  tcp->write_cb = NULL;
  tcp->read_user_data = NULL;
  tcp->write_user_data = NULL;
  tcp->slice_size = slice_size;
  tcp->iov_size = 1;
  tcp->finished_edge = 1;
  slice_state_init(&tcp->write_state, NULL, 0, 0);
  /* paired with unref in grpc_tcp_destroy */
  gpr_ref_init(&tcp->refcount, 1);
  tcp->em_fd = em_fd;
  tcp->read_closure.cb = grpc_tcp_handle_read;
  tcp->read_closure.cb_arg = tcp;
  tcp->write_closure.cb = grpc_tcp_handle_write;
  tcp->write_closure.cb_arg = tcp;

  tcp->handle_read_closure.cb = grpc_tcp_handle_read;
  return &tcp->base;
}

#endif
