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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP

#include "src/core/lib/iomgr/network_status_tracker.h"
#include "src/core/lib/iomgr/tcp_posix.h"

#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
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

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/buffer_list.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

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

extern grpc_core::TraceFlag grpc_tcp_trace;

namespace {
struct grpc_tcp {
  grpc_endpoint base;
  grpc_fd* em_fd;
  int fd;
  /* Used by the endpoint read function to distinguish the very first read call
   * from the rest */
  bool is_first_read;
  double target_length;
  double bytes_read_this_round;
  gpr_refcount refcount;
  gpr_atm shutdown_count;

  int min_read_chunk_size;
  int max_read_chunk_size;

  /* garbage after the last read */
  grpc_slice_buffer last_read_buffer;

  grpc_slice_buffer* incoming_buffer;
  grpc_slice_buffer* outgoing_buffer;
  /** byte within outgoing_buffer->slices[0] to write next */
  size_t outgoing_byte_idx;

  grpc_closure* read_cb;
  grpc_closure* write_cb;
  grpc_closure* release_fd_cb;
  int* release_fd;

  grpc_closure read_done_closure;
  grpc_closure write_done_closure;
  grpc_closure error_closure;

  char* peer_string;

  grpc_resource_user* resource_user;
  grpc_resource_user_slice_allocator slice_allocator;

  grpc_core::TracedBuffer* tb_head; /* List of traced buffers */
  gpr_mu tb_mu; /* Lock for access to list of traced buffers */

  /* grpc_endpoint_write takes an argument which if non-null means that the
   * transport layer wants the TCP layer to collect timestamps for this write.
   * This arg is forwarded to the timestamps callback function when the ACK
   * timestamp is received from the kernel. This arg is a (void *) which allows
   * users of this API to pass in a pointer to any kind of structure. This
   * structure could actually be a tag or any book-keeping object that the user
   * can use to distinguish between different traced writes. The only
   * requirement from the TCP endpoint layer is that this arg should be non-null
   * if the user wants timestamps for the write. */
  void* outgoing_buffer_arg;
  /* A counter which starts at 0. It is initialized the first time the socket
   * options for collecting timestamps are set, and is incremented with each
   * byte sent. */
  int bytes_counter;
  bool socket_ts_enabled; /* True if timestamping options are set on the socket
                           */
  gpr_atm
      stop_error_notification; /* Set to 1 if we do not want to be notified on
                                  errors anymore */
};

struct backup_poller {
  gpr_mu* pollset_mu;
  grpc_closure run_poller;
};

}  // namespace

#define BACKUP_POLLER_POLLSET(b) ((grpc_pollset*)((b) + 1))

static gpr_atm g_uncovered_notifications_pending;
static gpr_atm g_backup_poller; /* backup_poller* */

static void tcp_handle_read(void* arg /* grpc_tcp */, grpc_error* error);
static void tcp_handle_write(void* arg /* grpc_tcp */, grpc_error* error);
static void tcp_drop_uncovered_then_handle_write(void* arg /* grpc_tcp */,
                                                 grpc_error* error);

static void done_poller(void* bp, grpc_error* error_ignored) {
  backup_poller* p = static_cast<backup_poller*>(bp);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_INFO, "BACKUP_POLLER:%p destroy", p);
  }
  grpc_pollset_destroy(BACKUP_POLLER_POLLSET(p));
  gpr_free(p);
}

static void run_poller(void* bp, grpc_error* error_ignored) {
  backup_poller* p = static_cast<backup_poller*>(bp);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_INFO, "BACKUP_POLLER:%p run", p);
  }
  gpr_mu_lock(p->pollset_mu);
  grpc_millis deadline = grpc_core::ExecCtx::Get()->Now() + 10 * GPR_MS_PER_SEC;
  GRPC_STATS_INC_TCP_BACKUP_POLLER_POLLS();
  GRPC_LOG_IF_ERROR(
      "backup_poller:pollset_work",
      grpc_pollset_work(BACKUP_POLLER_POLLSET(p), nullptr, deadline));
  gpr_mu_unlock(p->pollset_mu);
  /* last "uncovered" notification is the ref that keeps us polling, if we get
   * there try a cas to release it */
  if (gpr_atm_no_barrier_load(&g_uncovered_notifications_pending) == 1 &&
      gpr_atm_full_cas(&g_uncovered_notifications_pending, 1, 0)) {
    gpr_mu_lock(p->pollset_mu);
    bool cas_ok = gpr_atm_full_cas(&g_backup_poller, (gpr_atm)p, 0);
    if (grpc_tcp_trace.enabled()) {
      gpr_log(GPR_INFO, "BACKUP_POLLER:%p done cas_ok=%d", p, cas_ok);
    }
    gpr_mu_unlock(p->pollset_mu);
    if (grpc_tcp_trace.enabled()) {
      gpr_log(GPR_INFO, "BACKUP_POLLER:%p shutdown", p);
    }
    grpc_pollset_shutdown(BACKUP_POLLER_POLLSET(p),
                          GRPC_CLOSURE_INIT(&p->run_poller, done_poller, p,
                                            grpc_schedule_on_exec_ctx));
  } else {
    if (grpc_tcp_trace.enabled()) {
      gpr_log(GPR_INFO, "BACKUP_POLLER:%p reschedule", p);
    }
    GRPC_CLOSURE_SCHED(&p->run_poller, GRPC_ERROR_NONE);
  }
}

static void drop_uncovered(grpc_tcp* tcp) {
  backup_poller* p = (backup_poller*)gpr_atm_acq_load(&g_backup_poller);
  gpr_atm old_count =
      gpr_atm_no_barrier_fetch_add(&g_uncovered_notifications_pending, -1);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_INFO, "BACKUP_POLLER:%p uncover cnt %d->%d", p,
            static_cast<int>(old_count), static_cast<int>(old_count) - 1);
  }
  GPR_ASSERT(old_count != 1);
}

// gRPC API considers a Write operation to be done the moment it clears ‘flow
// control’ i.e., not necessarily sent on the wire. This means that the
// application MIGHT not call `grpc_completion_queue_next/pluck` in a timely
// manner when its `Write()` API is acked.
//
// We need to ensure that the fd is 'covered' (i.e being monitored by some
// polling thread and progress is made) and hence add it to a backup poller here
static void cover_self(grpc_tcp* tcp) {
  backup_poller* p;
  gpr_atm old_count =
      gpr_atm_no_barrier_fetch_add(&g_uncovered_notifications_pending, 2);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_INFO, "BACKUP_POLLER: cover cnt %d->%d",
            static_cast<int>(old_count), 2 + static_cast<int>(old_count));
  }
  if (old_count == 0) {
    GRPC_STATS_INC_TCP_BACKUP_POLLERS_CREATED();
    p = static_cast<backup_poller*>(
        gpr_zalloc(sizeof(*p) + grpc_pollset_size()));
    if (grpc_tcp_trace.enabled()) {
      gpr_log(GPR_INFO, "BACKUP_POLLER:%p create", p);
    }
    grpc_pollset_init(BACKUP_POLLER_POLLSET(p), &p->pollset_mu);
    gpr_atm_rel_store(&g_backup_poller, (gpr_atm)p);
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_INIT(&p->run_poller, run_poller, p,
                          grpc_executor_scheduler(GRPC_EXECUTOR_LONG)),
        GRPC_ERROR_NONE);
  } else {
    while ((p = (backup_poller*)gpr_atm_acq_load(&g_backup_poller)) ==
           nullptr) {
      // spin waiting for backup poller
    }
  }
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_INFO, "BACKUP_POLLER:%p add %p", p, tcp);
  }
  grpc_pollset_add_fd(BACKUP_POLLER_POLLSET(p), tcp->em_fd);
  if (old_count != 0) {
    drop_uncovered(tcp);
  }
}

static void notify_on_read(grpc_tcp* tcp) {
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_INFO, "TCP:%p notify_on_read", tcp);
  }
  GRPC_CLOSURE_INIT(&tcp->read_done_closure, tcp_handle_read, tcp,
                    grpc_schedule_on_exec_ctx);
  grpc_fd_notify_on_read(tcp->em_fd, &tcp->read_done_closure);
}

static void notify_on_write(grpc_tcp* tcp) {
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_INFO, "TCP:%p notify_on_write", tcp);
  }
  cover_self(tcp);
  GRPC_CLOSURE_INIT(&tcp->write_done_closure,
                    tcp_drop_uncovered_then_handle_write, tcp,
                    grpc_schedule_on_exec_ctx);
  grpc_fd_notify_on_write(tcp->em_fd, &tcp->write_done_closure);
}

static void tcp_drop_uncovered_then_handle_write(void* arg, grpc_error* error) {
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_INFO, "TCP:%p got_write: %s", arg, grpc_error_string(error));
  }
  drop_uncovered(static_cast<grpc_tcp*>(arg));
  tcp_handle_write(arg, error);
}

static void add_to_estimate(grpc_tcp* tcp, size_t bytes) {
  tcp->bytes_read_this_round += static_cast<double>(bytes);
}

static void finish_estimate(grpc_tcp* tcp) {
  /* If we read >80% of the target buffer in one read loop, increase the size
     of the target buffer to either the amount read, or twice its previous
     value */
  if (tcp->bytes_read_this_round > tcp->target_length * 0.8) {
    tcp->target_length =
        GPR_MAX(2 * tcp->target_length, tcp->bytes_read_this_round);
  } else {
    tcp->target_length =
        0.99 * tcp->target_length + 0.01 * tcp->bytes_read_this_round;
  }
  tcp->bytes_read_this_round = 0;
}

static size_t get_target_read_size(grpc_tcp* tcp) {
  grpc_resource_quota* rq = grpc_resource_user_quota(tcp->resource_user);
  double pressure = grpc_resource_quota_get_memory_pressure(rq);
  double target =
      tcp->target_length * (pressure > 0.8 ? (1.0 - pressure) / 0.2 : 1.0);
  size_t sz = ((static_cast<size_t> GPR_CLAMP(target, tcp->min_read_chunk_size,
                                              tcp->max_read_chunk_size)) +
               255) &
              ~static_cast<size_t>(255);
  /* don't use more than 1/16th of the overall resource quota for a single read
   * alloc */
  size_t rqmax = grpc_resource_quota_peek_size(rq);
  if (sz > rqmax / 16 && rqmax > 1024) {
    sz = rqmax / 16;
  }
  return sz;
}

static grpc_error* tcp_annotate_error(grpc_error* src_error, grpc_tcp* tcp) {
  return grpc_error_set_str(
      grpc_error_set_int(
          grpc_error_set_int(src_error, GRPC_ERROR_INT_FD, tcp->fd),
          /* All tcp errors are marked with UNAVAILABLE so that application may
           * choose to retry. */
          GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE),
      GRPC_ERROR_STR_TARGET_ADDRESS,
      grpc_slice_from_copied_string(tcp->peer_string));
}

static void tcp_handle_read(void* arg /* grpc_tcp */, grpc_error* error);
static void tcp_handle_write(void* arg /* grpc_tcp */, grpc_error* error);

static void tcp_shutdown(grpc_endpoint* ep, grpc_error* why) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  grpc_fd_shutdown(tcp->em_fd, why);
  grpc_resource_user_shutdown(tcp->resource_user);
}

static void tcp_free(grpc_tcp* tcp) {
  grpc_fd_orphan(tcp->em_fd, tcp->release_fd_cb, tcp->release_fd,
                 "tcp_unref_orphan");
  grpc_slice_buffer_destroy_internal(&tcp->last_read_buffer);
  grpc_resource_user_unref(tcp->resource_user);
  gpr_free(tcp->peer_string);
  gpr_mu_destroy(&tcp->tb_mu);
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

static void tcp_destroy(grpc_endpoint* ep) {
  grpc_network_status_unregister_endpoint(ep);
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  grpc_slice_buffer_reset_and_unref_internal(&tcp->last_read_buffer);
  if (grpc_event_engine_can_track_errors()) {
    gpr_atm_no_barrier_store(&tcp->stop_error_notification, true);
    grpc_fd_set_error(tcp->em_fd);
  }
  TCP_UNREF(tcp, "destroy");
}

static void call_read_cb(grpc_tcp* tcp, grpc_error* error) {
  grpc_closure* cb = tcp->read_cb;

  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_INFO, "TCP:%p call_cb %p %p:%p", tcp, cb, cb->cb, cb->cb_arg);
    size_t i;
    const char* str = grpc_error_string(error);
    gpr_log(GPR_INFO, "read: error=%s", str);

    for (i = 0; i < tcp->incoming_buffer->count; i++) {
      char* dump = grpc_dump_slice(tcp->incoming_buffer->slices[i],
                                   GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_INFO, "READ %p (peer=%s): %s", tcp, tcp->peer_string, dump);
      gpr_free(dump);
    }
  }

  tcp->read_cb = nullptr;
  tcp->incoming_buffer = nullptr;
  GRPC_CLOSURE_SCHED(cb, error);
}

#define MAX_READ_IOVEC 4
static void tcp_do_read(grpc_tcp* tcp) {
  GPR_TIMER_SCOPE("tcp_do_read", 0);
  struct msghdr msg;
  struct iovec iov[MAX_READ_IOVEC];
  ssize_t read_bytes;
  size_t i;

  GPR_ASSERT(tcp->incoming_buffer->count <= MAX_READ_IOVEC);

  for (i = 0; i < tcp->incoming_buffer->count; i++) {
    iov[i].iov_base = GRPC_SLICE_START_PTR(tcp->incoming_buffer->slices[i]);
    iov[i].iov_len = GRPC_SLICE_LENGTH(tcp->incoming_buffer->slices[i]);
  }

  msg.msg_name = nullptr;
  msg.msg_namelen = 0;
  msg.msg_iov = iov;
  msg.msg_iovlen = static_cast<msg_iovlen_type>(tcp->incoming_buffer->count);
  msg.msg_control = nullptr;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;

  GRPC_STATS_INC_TCP_READ_OFFER(tcp->incoming_buffer->length);
  GRPC_STATS_INC_TCP_READ_OFFER_IOV_SIZE(tcp->incoming_buffer->count);

  do {
    GPR_TIMER_SCOPE("recvmsg", 0);
    GRPC_STATS_INC_SYSCALL_READ();
    read_bytes = recvmsg(tcp->fd, &msg, 0);
  } while (read_bytes < 0 && errno == EINTR);

  if (read_bytes < 0) {
    /* NB: After calling call_read_cb a parallel call of the read handler may
     * be running. */
    if (errno == EAGAIN) {
      finish_estimate(tcp);
      /* We've consumed the edge, request a new one */
      notify_on_read(tcp);
    } else {
      grpc_slice_buffer_reset_and_unref_internal(tcp->incoming_buffer);
      call_read_cb(tcp,
                   tcp_annotate_error(GRPC_OS_ERROR(errno, "recvmsg"), tcp));
      TCP_UNREF(tcp, "read");
    }
  } else if (read_bytes == 0) {
    /* 0 read size ==> end of stream */
    grpc_slice_buffer_reset_and_unref_internal(tcp->incoming_buffer);
    call_read_cb(
        tcp, tcp_annotate_error(
                 GRPC_ERROR_CREATE_FROM_STATIC_STRING("Socket closed"), tcp));
    TCP_UNREF(tcp, "read");
  } else {
    GRPC_STATS_INC_TCP_READ_SIZE(read_bytes);
    add_to_estimate(tcp, static_cast<size_t>(read_bytes));
    GPR_ASSERT((size_t)read_bytes <= tcp->incoming_buffer->length);
    if (static_cast<size_t>(read_bytes) < tcp->incoming_buffer->length) {
      grpc_slice_buffer_trim_end(
          tcp->incoming_buffer,
          tcp->incoming_buffer->length - static_cast<size_t>(read_bytes),
          &tcp->last_read_buffer);
    }
    GPR_ASSERT((size_t)read_bytes == tcp->incoming_buffer->length);
    call_read_cb(tcp, GRPC_ERROR_NONE);
    TCP_UNREF(tcp, "read");
  }
}

static void tcp_read_allocation_done(void* tcpp, grpc_error* error) {
  grpc_tcp* tcp = static_cast<grpc_tcp*>(tcpp);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_INFO, "TCP:%p read_allocation_done: %s", tcp,
            grpc_error_string(error));
  }
  if (error != GRPC_ERROR_NONE) {
    grpc_slice_buffer_reset_and_unref_internal(tcp->incoming_buffer);
    grpc_slice_buffer_reset_and_unref_internal(&tcp->last_read_buffer);
    call_read_cb(tcp, GRPC_ERROR_REF(error));
    TCP_UNREF(tcp, "read");
  } else {
    tcp_do_read(tcp);
  }
}

static void tcp_continue_read(grpc_tcp* tcp) {
  size_t target_read_size = get_target_read_size(tcp);
  if (tcp->incoming_buffer->length < target_read_size &&
      tcp->incoming_buffer->count < MAX_READ_IOVEC) {
    if (grpc_tcp_trace.enabled()) {
      gpr_log(GPR_INFO, "TCP:%p alloc_slices", tcp);
    }
    grpc_resource_user_alloc_slices(&tcp->slice_allocator, target_read_size, 1,
                                    tcp->incoming_buffer);
  } else {
    if (grpc_tcp_trace.enabled()) {
      gpr_log(GPR_INFO, "TCP:%p do_read", tcp);
    }
    tcp_do_read(tcp);
  }
}

static void tcp_handle_read(void* arg /* grpc_tcp */, grpc_error* error) {
  grpc_tcp* tcp = static_cast<grpc_tcp*>(arg);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_INFO, "TCP:%p got_read: %s", tcp, grpc_error_string(error));
  }

  if (error != GRPC_ERROR_NONE) {
    grpc_slice_buffer_reset_and_unref_internal(tcp->incoming_buffer);
    grpc_slice_buffer_reset_and_unref_internal(&tcp->last_read_buffer);
    call_read_cb(tcp, GRPC_ERROR_REF(error));
    TCP_UNREF(tcp, "read");
  } else {
    tcp_continue_read(tcp);
  }
}

static void tcp_read(grpc_endpoint* ep, grpc_slice_buffer* incoming_buffer,
                     grpc_closure* cb) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  GPR_ASSERT(tcp->read_cb == nullptr);
  tcp->read_cb = cb;
  tcp->incoming_buffer = incoming_buffer;
  grpc_slice_buffer_reset_and_unref_internal(incoming_buffer);
  grpc_slice_buffer_swap(incoming_buffer, &tcp->last_read_buffer);
  TCP_REF(tcp, "read");
  if (tcp->is_first_read) {
    /* Endpoint read called for the very first time. Register read callback with
     * the polling engine */
    tcp->is_first_read = false;
    notify_on_read(tcp);
  } else {
    /* Not the first time. We may or may not have more bytes available. In any
     * case call tcp->read_done_closure (i.e tcp_handle_read()) which does the
     * right thing (i.e calls tcp_do_read() which either reads the available
     * bytes or calls notify_on_read() to be notified when new bytes become
     * available */
    GRPC_CLOSURE_SCHED(&tcp->read_done_closure, GRPC_ERROR_NONE);
  }
}

/* A wrapper around sendmsg. It sends \a msg over \a fd and returns the number
 * of bytes sent. */
ssize_t tcp_send(int fd, const struct msghdr* msg) {
  GPR_TIMER_SCOPE("sendmsg", 1);
  ssize_t sent_length;
  do {
    /* TODO(klempner): Cork if this is a partial write */
    GRPC_STATS_INC_SYSCALL_WRITE();
    sent_length = sendmsg(fd, msg, SENDMSG_FLAGS);
  } while (sent_length < 0 && errno == EINTR);
  return sent_length;
}

/** This is to be called if outgoing_buffer_arg is not null. On linux platforms,
 * this will call sendmsg with socket options set to collect timestamps inside
 * the kernel. On return, sent_length is set to the return value of the sendmsg
 * call. Returns false if setting the socket options failed. This is not
 * implemented for non-linux platforms currently, and crashes out.
 */
static bool tcp_write_with_timestamps(grpc_tcp* tcp, struct msghdr* msg,
                                      size_t sending_length,
                                      ssize_t* sent_length, grpc_error** error);

/** The callback function to be invoked when we get an error on the socket. */
static void tcp_handle_error(void* arg /* grpc_tcp */, grpc_error* error);

#ifdef GRPC_LINUX_ERRQUEUE
static bool tcp_write_with_timestamps(grpc_tcp* tcp, struct msghdr* msg,
                                      size_t sending_length,
                                      ssize_t* sent_length,
                                      grpc_error** error) {
  if (!tcp->socket_ts_enabled) {
    uint32_t opt = grpc_core::kTimestampingSocketOptions;
    if (setsockopt(tcp->fd, SOL_SOCKET, SO_TIMESTAMPING,
                   static_cast<void*>(&opt), sizeof(opt)) != 0) {
      *error = tcp_annotate_error(GRPC_OS_ERROR(errno, "setsockopt"), tcp);
      grpc_slice_buffer_reset_and_unref_internal(tcp->outgoing_buffer);
      if (grpc_tcp_trace.enabled()) {
        gpr_log(GPR_ERROR, "Failed to set timestamping options on the socket.");
      }
      return false;
    }
    tcp->bytes_counter = -1;
    tcp->socket_ts_enabled = true;
  }
  /* Set control message to indicate that you want timestamps. */
  union {
    char cmsg_buf[CMSG_SPACE(sizeof(uint32_t))];
    struct cmsghdr align;
  } u;
  cmsghdr* cmsg = reinterpret_cast<cmsghdr*>(u.cmsg_buf);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SO_TIMESTAMPING;
  cmsg->cmsg_len = CMSG_LEN(sizeof(uint32_t));
  *reinterpret_cast<int*>(CMSG_DATA(cmsg)) =
      grpc_core::kTimestampingRecordingOptions;
  msg->msg_control = u.cmsg_buf;
  msg->msg_controllen = CMSG_SPACE(sizeof(uint32_t));

  /* If there was an error on sendmsg the logic in tcp_flush will handle it. */
  ssize_t length = tcp_send(tcp->fd, msg);
  *sent_length = length;
  /* Only save timestamps if all the bytes were taken by sendmsg. */
  if (sending_length == static_cast<size_t>(length)) {
    gpr_mu_lock(&tcp->tb_mu);
    grpc_core::TracedBuffer::AddNewEntry(
        &tcp->tb_head, static_cast<int>(tcp->bytes_counter + length),
        tcp->outgoing_buffer_arg);
    gpr_mu_unlock(&tcp->tb_mu);
    tcp->outgoing_buffer_arg = nullptr;
  }
  return true;
}

/** Reads \a cmsg to derive timestamps from the control messages. If a valid
 * timestamp is found, the traced buffer list is updated with this timestamp.
 * The caller of this function should be looping on the control messages found
 * in \a msg. \a cmsg should point to the control message that the caller wants
 * processed.
 * On return, a pointer to a control message is returned. On the next iteration,
 * CMSG_NXTHDR(msg, ret_val) should be passed as \a cmsg. */
struct cmsghdr* process_timestamp(grpc_tcp* tcp, msghdr* msg,
                                  struct cmsghdr* cmsg) {
  auto next_cmsg = CMSG_NXTHDR(msg, cmsg);
  if (next_cmsg == nullptr) {
    if (grpc_tcp_trace.enabled()) {
      gpr_log(GPR_ERROR, "Received timestamp without extended error");
    }
    return cmsg;
  }

  if (!(next_cmsg->cmsg_level == SOL_IP || next_cmsg->cmsg_level == SOL_IPV6) ||
      !(next_cmsg->cmsg_type == IP_RECVERR ||
        next_cmsg->cmsg_type == IPV6_RECVERR)) {
    if (grpc_tcp_trace.enabled()) {
      gpr_log(GPR_ERROR, "Unexpected control message");
    }
    return cmsg;
  }

  auto tss =
      reinterpret_cast<struct grpc_core::scm_timestamping*>(CMSG_DATA(cmsg));
  auto serr = reinterpret_cast<struct sock_extended_err*>(CMSG_DATA(next_cmsg));
  if (serr->ee_errno != ENOMSG ||
      serr->ee_origin != SO_EE_ORIGIN_TIMESTAMPING) {
    gpr_log(GPR_ERROR, "Unexpected control message");
    return cmsg;
  }
  /* The error handling can potentially be done on another thread so we need
   * to protect the traced buffer list. A lock free list might be better. Using
   * a simple mutex for now. */
  gpr_mu_lock(&tcp->tb_mu);
  grpc_core::TracedBuffer::ProcessTimestamp(&tcp->tb_head, serr, tss);
  gpr_mu_unlock(&tcp->tb_mu);
  return next_cmsg;
}

/** For linux platforms, reads the socket's error queue and processes error
 * messages from the queue. Returns true if all the errors processed were
 * timestamps. Returns false if any of the errors were not timestamps. For
 * non-linux platforms, error processing is not used/enabled currently.
 */
static bool process_errors(grpc_tcp* tcp) {
  while (true) {
    struct iovec iov;
    iov.iov_base = nullptr;
    iov.iov_len = 0;
    struct msghdr msg;
    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 0;
    msg.msg_flags = 0;

    union {
      char rbuf[1024 /*CMSG_SPACE(sizeof(scm_timestamping)) +
                CMSG_SPACE(sizeof(sock_extended_err) + sizeof(sockaddr_in))*/];
      struct cmsghdr align;
    } aligned_buf;
    memset(&aligned_buf, 0, sizeof(aligned_buf));

    msg.msg_control = aligned_buf.rbuf;
    msg.msg_controllen = sizeof(aligned_buf.rbuf);

    int r, saved_errno;
    do {
      r = recvmsg(tcp->fd, &msg, MSG_ERRQUEUE);
      saved_errno = errno;
    } while (r < 0 && saved_errno == EINTR);

    if (r == -1 && saved_errno == EAGAIN) {
      return true; /* No more errors to process */
    }
    if (r == -1) {
      return false;
    }
    if (grpc_tcp_trace.enabled()) {
      if ((msg.msg_flags & MSG_CTRUNC) == 1) {
        gpr_log(GPR_INFO, "Error message was truncated.");
      }
    }

    if (msg.msg_controllen == 0) {
      /* There was no control message found. It was probably spurious. */
      return true;
    }
    for (auto cmsg = CMSG_FIRSTHDR(&msg); cmsg && cmsg->cmsg_len;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
      if (cmsg->cmsg_level != SOL_SOCKET ||
          cmsg->cmsg_type != SCM_TIMESTAMPING) {
        /* Got a control message that is not a timestamp. Don't know how to
         * handle this. */
        if (grpc_tcp_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "unknown control message cmsg_level:%d cmsg_type:%d",
                  cmsg->cmsg_level, cmsg->cmsg_type);
        }
        return false;
      }
      process_timestamp(tcp, &msg, cmsg);
    }
  }
}

static void tcp_handle_error(void* arg /* grpc_tcp */, grpc_error* error) {
  grpc_tcp* tcp = static_cast<grpc_tcp*>(arg);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_INFO, "TCP:%p got_error: %s", tcp, grpc_error_string(error));
  }

  if (error != GRPC_ERROR_NONE ||
      static_cast<bool>(gpr_atm_acq_load(&tcp->stop_error_notification))) {
    /* We aren't going to register to hear on error anymore, so it is safe to
     * unref. */
    grpc_core::TracedBuffer::Shutdown(&tcp->tb_head, GRPC_ERROR_REF(error));
    TCP_UNREF(tcp, "error-tracking");
    return;
  }

  /* We are still interested in collecting timestamps, so let's try reading
   * them. */
  if (!process_errors(tcp)) {
    /* This was not a timestamps error. This was an actual error. Set the
     * read and write closures to be ready.
     */
    grpc_fd_set_readable(tcp->em_fd);
    grpc_fd_set_writable(tcp->em_fd);
  }
  GRPC_CLOSURE_INIT(&tcp->error_closure, tcp_handle_error, tcp,
                    grpc_schedule_on_exec_ctx);
  grpc_fd_notify_on_error(tcp->em_fd, &tcp->error_closure);
}

#else  /* GRPC_LINUX_ERRQUEUE */
static bool tcp_write_with_timestamps(grpc_tcp* tcp, struct msghdr* msg,
                                      size_t sending_length,
                                      ssize_t* sent_length,
                                      grpc_error** error) {
  gpr_log(GPR_ERROR, "Write with timestamps not supported for this platform");
  GPR_ASSERT(0);
  return false;
}

static void tcp_handle_error(void* arg /* grpc_tcp */, grpc_error* error) {
  gpr_log(GPR_ERROR, "Error handling is not supported for this platform");
  GPR_ASSERT(0);
}
#endif /* GRPC_LINUX_ERRQUEUE */

/* returns true if done, false if pending; if returning true, *error is set */
#if defined(IOV_MAX) && IOV_MAX < 1000
#define MAX_WRITE_IOVEC IOV_MAX
#else
#define MAX_WRITE_IOVEC 1000
#endif
static bool tcp_flush(grpc_tcp* tcp, grpc_error** error) {
  struct msghdr msg;
  struct iovec iov[MAX_WRITE_IOVEC];
  msg_iovlen_type iov_size;
  ssize_t sent_length;
  size_t sending_length;
  size_t trailing;
  size_t unwind_slice_idx;
  size_t unwind_byte_idx;

  // We always start at zero, because we eagerly unref and trim the slice
  // buffer as we write
  size_t outgoing_slice_idx = 0;

  for (;;) {
    sending_length = 0;
    unwind_slice_idx = outgoing_slice_idx;
    unwind_byte_idx = tcp->outgoing_byte_idx;
    for (iov_size = 0; outgoing_slice_idx != tcp->outgoing_buffer->count &&
                       iov_size != MAX_WRITE_IOVEC;
         iov_size++) {
      iov[iov_size].iov_base =
          GRPC_SLICE_START_PTR(
              tcp->outgoing_buffer->slices[outgoing_slice_idx]) +
          tcp->outgoing_byte_idx;
      iov[iov_size].iov_len =
          GRPC_SLICE_LENGTH(tcp->outgoing_buffer->slices[outgoing_slice_idx]) -
          tcp->outgoing_byte_idx;
      sending_length += iov[iov_size].iov_len;
      outgoing_slice_idx++;
      tcp->outgoing_byte_idx = 0;
    }
    GPR_ASSERT(iov_size > 0);

    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = iov_size;
    msg.msg_flags = 0;
    if (tcp->outgoing_buffer_arg != nullptr) {
      if (!tcp_write_with_timestamps(tcp, &msg, sending_length, &sent_length,
                                     error))
        return true; /* something went wrong with timestamps */
    } else {
      msg.msg_control = nullptr;
      msg.msg_controllen = 0;

      GRPC_STATS_INC_TCP_WRITE_SIZE(sending_length);
      GRPC_STATS_INC_TCP_WRITE_IOV_SIZE(iov_size);

      sent_length = tcp_send(tcp->fd, &msg);
    }

    if (sent_length < 0) {
      if (errno == EAGAIN) {
        tcp->outgoing_byte_idx = unwind_byte_idx;
        // unref all and forget about all slices that have been written to this
        // point
        for (size_t idx = 0; idx < unwind_slice_idx; ++idx) {
          grpc_slice_unref_internal(
              grpc_slice_buffer_take_first(tcp->outgoing_buffer));
        }
        return false;
      } else if (errno == EPIPE) {
        *error = tcp_annotate_error(GRPC_OS_ERROR(errno, "sendmsg"), tcp);
        grpc_slice_buffer_reset_and_unref_internal(tcp->outgoing_buffer);
        return true;
      } else {
        *error = tcp_annotate_error(GRPC_OS_ERROR(errno, "sendmsg"), tcp);
        grpc_slice_buffer_reset_and_unref_internal(tcp->outgoing_buffer);
        return true;
      }
    }

    GPR_ASSERT(tcp->outgoing_byte_idx == 0);
    tcp->bytes_counter += sent_length;
    trailing = sending_length - static_cast<size_t>(sent_length);
    while (trailing > 0) {
      size_t slice_length;

      outgoing_slice_idx--;
      slice_length =
          GRPC_SLICE_LENGTH(tcp->outgoing_buffer->slices[outgoing_slice_idx]);
      if (slice_length > trailing) {
        tcp->outgoing_byte_idx = slice_length - trailing;
        break;
      } else {
        trailing -= slice_length;
      }
    }
    if (outgoing_slice_idx == tcp->outgoing_buffer->count) {
      *error = GRPC_ERROR_NONE;
      grpc_slice_buffer_reset_and_unref_internal(tcp->outgoing_buffer);
      return true;
    }
  }
}

static void tcp_handle_write(void* arg /* grpc_tcp */, grpc_error* error) {
  grpc_tcp* tcp = static_cast<grpc_tcp*>(arg);
  grpc_closure* cb;

  if (error != GRPC_ERROR_NONE) {
    cb = tcp->write_cb;
    tcp->write_cb = nullptr;
    cb->cb(cb->cb_arg, error);
    TCP_UNREF(tcp, "write");
    return;
  }

  if (!tcp_flush(tcp, &error)) {
    if (grpc_tcp_trace.enabled()) {
      gpr_log(GPR_INFO, "write: delayed");
    }
    notify_on_write(tcp);
  } else {
    cb = tcp->write_cb;
    tcp->write_cb = nullptr;
    if (grpc_tcp_trace.enabled()) {
      const char* str = grpc_error_string(error);
      gpr_log(GPR_INFO, "write: %s", str);
    }
    GRPC_CLOSURE_SCHED(cb, error);
    TCP_UNREF(tcp, "write");
  }
}

static void tcp_write(grpc_endpoint* ep, grpc_slice_buffer* buf,
                      grpc_closure* cb, void* arg) {
  GPR_TIMER_SCOPE("tcp_write", 0);
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  grpc_error* error = GRPC_ERROR_NONE;

  if (grpc_tcp_trace.enabled()) {
    size_t i;

    for (i = 0; i < buf->count; i++) {
      char* data =
          grpc_dump_slice(buf->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_INFO, "WRITE %p (peer=%s): %s", tcp, tcp->peer_string, data);
      gpr_free(data);
    }
  }

  GPR_ASSERT(tcp->write_cb == nullptr);

  if (buf->length == 0) {
    GRPC_CLOSURE_SCHED(
        cb, grpc_fd_is_shutdown(tcp->em_fd)
                ? tcp_annotate_error(
                      GRPC_ERROR_CREATE_FROM_STATIC_STRING("EOF"), tcp)
                : GRPC_ERROR_NONE);
    return;
  }
  tcp->outgoing_buffer = buf;
  tcp->outgoing_byte_idx = 0;
  tcp->outgoing_buffer_arg = arg;
  if (arg) {
    GPR_ASSERT(grpc_event_engine_can_track_errors());
  }

  if (!tcp_flush(tcp, &error)) {
    TCP_REF(tcp, "write");
    tcp->write_cb = cb;
    if (grpc_tcp_trace.enabled()) {
      gpr_log(GPR_INFO, "write: delayed");
    }
    notify_on_write(tcp);
  } else {
    if (grpc_tcp_trace.enabled()) {
      const char* str = grpc_error_string(error);
      gpr_log(GPR_INFO, "write: %s", str);
    }
    GRPC_CLOSURE_SCHED(cb, error);
  }
}

static void tcp_add_to_pollset(grpc_endpoint* ep, grpc_pollset* pollset) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  grpc_pollset_add_fd(pollset, tcp->em_fd);
}

static void tcp_add_to_pollset_set(grpc_endpoint* ep,
                                   grpc_pollset_set* pollset_set) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  grpc_pollset_set_add_fd(pollset_set, tcp->em_fd);
}

static void tcp_delete_from_pollset_set(grpc_endpoint* ep,
                                        grpc_pollset_set* pollset_set) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  grpc_pollset_set_del_fd(pollset_set, tcp->em_fd);
}

static char* tcp_get_peer(grpc_endpoint* ep) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  return gpr_strdup(tcp->peer_string);
}

static int tcp_get_fd(grpc_endpoint* ep) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  return tcp->fd;
}

static grpc_resource_user* tcp_get_resource_user(grpc_endpoint* ep) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  return tcp->resource_user;
}

static const grpc_endpoint_vtable vtable = {tcp_read,
                                            tcp_write,
                                            tcp_add_to_pollset,
                                            tcp_add_to_pollset_set,
                                            tcp_delete_from_pollset_set,
                                            tcp_shutdown,
                                            tcp_destroy,
                                            tcp_get_resource_user,
                                            tcp_get_peer,
                                            tcp_get_fd};

#define MAX_CHUNK_SIZE 32 * 1024 * 1024

grpc_endpoint* grpc_tcp_create(grpc_fd* em_fd,
                               const grpc_channel_args* channel_args,
                               const char* peer_string) {
  int tcp_read_chunk_size = GRPC_TCP_DEFAULT_READ_SLICE_SIZE;
  int tcp_max_read_chunk_size = 4 * 1024 * 1024;
  int tcp_min_read_chunk_size = 256;
  grpc_resource_quota* resource_quota = grpc_resource_quota_create(nullptr);
  if (channel_args != nullptr) {
    for (size_t i = 0; i < channel_args->num_args; i++) {
      if (0 ==
          strcmp(channel_args->args[i].key, GRPC_ARG_TCP_READ_CHUNK_SIZE)) {
        grpc_integer_options options = {tcp_read_chunk_size, 1, MAX_CHUNK_SIZE};
        tcp_read_chunk_size =
            grpc_channel_arg_get_integer(&channel_args->args[i], options);
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_TCP_MIN_READ_CHUNK_SIZE)) {
        grpc_integer_options options = {tcp_read_chunk_size, 1, MAX_CHUNK_SIZE};
        tcp_min_read_chunk_size =
            grpc_channel_arg_get_integer(&channel_args->args[i], options);
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_TCP_MAX_READ_CHUNK_SIZE)) {
        grpc_integer_options options = {tcp_read_chunk_size, 1, MAX_CHUNK_SIZE};
        tcp_max_read_chunk_size =
            grpc_channel_arg_get_integer(&channel_args->args[i], options);
      } else if (0 ==
                 strcmp(channel_args->args[i].key, GRPC_ARG_RESOURCE_QUOTA)) {
        grpc_resource_quota_unref_internal(resource_quota);
        resource_quota =
            grpc_resource_quota_ref_internal(static_cast<grpc_resource_quota*>(
                channel_args->args[i].value.pointer.p));
      }
    }
  }

  if (tcp_min_read_chunk_size > tcp_max_read_chunk_size) {
    tcp_min_read_chunk_size = tcp_max_read_chunk_size;
  }
  tcp_read_chunk_size = GPR_CLAMP(tcp_read_chunk_size, tcp_min_read_chunk_size,
                                  tcp_max_read_chunk_size);

  grpc_tcp* tcp = static_cast<grpc_tcp*>(gpr_malloc(sizeof(grpc_tcp)));
  tcp->base.vtable = &vtable;
  tcp->peer_string = gpr_strdup(peer_string);
  tcp->fd = grpc_fd_wrapped_fd(em_fd);
  tcp->read_cb = nullptr;
  tcp->write_cb = nullptr;
  tcp->release_fd_cb = nullptr;
  tcp->release_fd = nullptr;
  tcp->incoming_buffer = nullptr;
  tcp->target_length = static_cast<double>(tcp_read_chunk_size);
  tcp->min_read_chunk_size = tcp_min_read_chunk_size;
  tcp->max_read_chunk_size = tcp_max_read_chunk_size;
  tcp->bytes_read_this_round = 0;
  /* Will be set to false by the very first endpoint read function */
  tcp->is_first_read = true;
  tcp->bytes_counter = -1;
  tcp->socket_ts_enabled = false;
  /* paired with unref in grpc_tcp_destroy */
  gpr_ref_init(&tcp->refcount, 1);
  gpr_atm_no_barrier_store(&tcp->shutdown_count, 0);
  tcp->em_fd = em_fd;
  grpc_slice_buffer_init(&tcp->last_read_buffer);
  tcp->resource_user = grpc_resource_user_create(resource_quota, peer_string);
  grpc_resource_user_slice_allocator_init(
      &tcp->slice_allocator, tcp->resource_user, tcp_read_allocation_done, tcp);
  /* Tell network status tracker about new endpoint */
  grpc_network_status_register_endpoint(&tcp->base);
  grpc_resource_quota_unref_internal(resource_quota);
  gpr_mu_init(&tcp->tb_mu);
  tcp->tb_head = nullptr;
  /* Start being notified on errors if event engine can track errors. */
  if (grpc_event_engine_can_track_errors()) {
    /* Grab a ref to tcp so that we can safely access the tcp struct when
     * processing errors. We unref when we no longer want to track errors
     * separately. */
    TCP_REF(tcp, "error-tracking");
    gpr_atm_rel_store(&tcp->stop_error_notification, 0);
    GRPC_CLOSURE_INIT(&tcp->error_closure, tcp_handle_error, tcp,
                      grpc_schedule_on_exec_ctx);
    grpc_fd_notify_on_error(tcp->em_fd, &tcp->error_closure);
  }

  return &tcp->base;
}

int grpc_tcp_fd(grpc_endpoint* ep) {
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  GPR_ASSERT(ep->vtable == &vtable);
  return grpc_fd_wrapped_fd(tcp->em_fd);
}

void grpc_tcp_destroy_and_release_fd(grpc_endpoint* ep, int* fd,
                                     grpc_closure* done) {
  grpc_network_status_unregister_endpoint(ep);
  grpc_tcp* tcp = reinterpret_cast<grpc_tcp*>(ep);
  GPR_ASSERT(ep->vtable == &vtable);
  tcp->release_fd = fd;
  tcp->release_fd_cb = done;
  grpc_slice_buffer_reset_and_unref_internal(&tcp->last_read_buffer);
  if (grpc_event_engine_can_track_errors()) {
    /* Stop errors notification. */
    gpr_atm_no_barrier_store(&tcp->stop_error_notification, true);
    grpc_fd_set_error(tcp->em_fd);
  }
  TCP_UNREF(tcp, "destroy");
}

#endif /* GRPC_POSIX_SOCKET_TCP */
