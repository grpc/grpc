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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/iomgr/tcp_client_posix.h"

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_posix.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_mutator.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

typedef struct {
  gpr_mu mu;
  grpc_fd* fd;
  grpc_timer alarm;
  grpc_closure on_alarm;
  int refs;
  grpc_closure write_closure;
  grpc_pollset_set* interested_parties;
  char* addr_str;
  grpc_endpoint** ep;
  grpc_closure* closure;
  grpc_channel_args* channel_args;
} async_connect;

static grpc_error* prepare_socket(const grpc_resolved_address* addr, int fd,
                                  const grpc_channel_args* channel_args) {
  grpc_error* err = GRPC_ERROR_NONE;

  GPR_ASSERT(fd >= 0);

  err = grpc_set_socket_nonblocking(fd, 1);
  if (err != GRPC_ERROR_NONE) goto error;
  err = grpc_set_socket_cloexec(fd, 1);
  if (err != GRPC_ERROR_NONE) goto error;
  if (!grpc_is_unix_socket(addr)) {
    err = grpc_set_socket_low_latency(fd, 1);
    if (err != GRPC_ERROR_NONE) goto error;
  }
  err = grpc_set_socket_no_sigpipe_if_possible(fd);
  if (err != GRPC_ERROR_NONE) goto error;
  if (channel_args) {
    for (size_t i = 0; i < channel_args->num_args; i++) {
      if (0 == strcmp(channel_args->args[i].key, GRPC_ARG_SOCKET_MUTATOR)) {
        GPR_ASSERT(channel_args->args[i].type == GRPC_ARG_POINTER);
        grpc_socket_mutator* mutator =
            (grpc_socket_mutator*)channel_args->args[i].value.pointer.p;
        err = grpc_set_socket_with_mutator(fd, mutator);
        if (err != GRPC_ERROR_NONE) goto error;
      }
    }
  }
  goto done;

error:
  if (fd >= 0) {
    close(fd);
  }
done:
  return err;
}

static void tc_on_alarm(void* acp, grpc_error* error) {
  int done;
  async_connect* ac = (async_connect*)acp;
  if (grpc_tcp_trace.enabled()) {
    const char* str = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "CLIENT_CONNECT: %s: on_alarm: error=%s", ac->addr_str,
            str);
  }
  gpr_mu_lock(&ac->mu);
  if (ac->fd != nullptr) {
    grpc_fd_shutdown(
        ac->fd, GRPC_ERROR_CREATE_FROM_STATIC_STRING("connect() timed out"));
  }
  done = (--ac->refs == 0);
  gpr_mu_unlock(&ac->mu);
  if (done) {
    gpr_mu_destroy(&ac->mu);
    gpr_free(ac->addr_str);
    grpc_channel_args_destroy(ac->channel_args);
    gpr_free(ac);
  }
}

grpc_endpoint* grpc_tcp_client_create_from_fd(
    grpc_fd* fd, const grpc_channel_args* channel_args, const char* addr_str) {
  return grpc_tcp_create(fd, channel_args, addr_str);
}

static void on_writable(void* acp, grpc_error* error) {
  async_connect* ac = (async_connect*)acp;
  int so_error = 0;
  socklen_t so_error_size;
  int err;
  int done;
  grpc_endpoint** ep = ac->ep;
  grpc_closure* closure = ac->closure;
  grpc_fd* fd;

  GRPC_ERROR_REF(error);

  if (grpc_tcp_trace.enabled()) {
    const char* str = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "CLIENT_CONNECT: %s: on_writable: error=%s",
            ac->addr_str, str);
  }

  gpr_mu_lock(&ac->mu);
  GPR_ASSERT(ac->fd);
  fd = ac->fd;
  ac->fd = nullptr;
  gpr_mu_unlock(&ac->mu);

  grpc_timer_cancel(&ac->alarm);

  gpr_mu_lock(&ac->mu);
  if (error != GRPC_ERROR_NONE) {
    error =
        grpc_error_set_str(error, GRPC_ERROR_STR_OS_ERROR,
                           grpc_slice_from_static_string("Timeout occurred"));
    goto finish;
  }

  do {
    so_error_size = sizeof(so_error);
    err = getsockopt(grpc_fd_wrapped_fd(fd), SOL_SOCKET, SO_ERROR, &so_error,
                     &so_error_size);
  } while (err < 0 && errno == EINTR);
  if (err < 0) {
    error = GRPC_OS_ERROR(errno, "getsockopt");
    goto finish;
  }

  switch (so_error) {
    case 0:
      grpc_pollset_set_del_fd(ac->interested_parties, fd);
      *ep = grpc_tcp_client_create_from_fd(fd, ac->channel_args, ac->addr_str);
      fd = nullptr;
      break;
    case ENOBUFS:
      /* We will get one of these errors if we have run out of
         memory in the kernel for the data structures allocated
         when you connect a socket.  If this happens it is very
         likely that if we wait a little bit then try again the
         connection will work (since other programs or this
         program will close their network connections and free up
         memory).  This does _not_ indicate that there is anything
         wrong with the server we are connecting to, this is a
         local problem.

         If you are looking at this code, then chances are that
         your program or another program on the same computer
         opened too many network connections.  The "easy" fix:
         don't do that! */
      gpr_log(GPR_ERROR, "kernel out of buffers");
      gpr_mu_unlock(&ac->mu);
      grpc_fd_notify_on_write(fd, &ac->write_closure);
      return;
    case ECONNREFUSED:
      /* This error shouldn't happen for anything other than connect(). */
      error = GRPC_OS_ERROR(so_error, "connect");
      break;
    default:
      /* We don't really know which syscall triggered the problem here,
         so punt by reporting getsockopt(). */
      error = GRPC_OS_ERROR(so_error, "getsockopt(SO_ERROR)");
      break;
  }

finish:
  if (fd != nullptr) {
    grpc_pollset_set_del_fd(ac->interested_parties, fd);
    grpc_fd_orphan(fd, nullptr, nullptr, false /* already_closed */,
                   "tcp_client_orphan");
    fd = nullptr;
  }
  done = (--ac->refs == 0);
  // Create a copy of the data from "ac" to be accessed after the unlock, as
  // "ac" and its contents may be deallocated by the time they are read.
  const grpc_slice addr_str_slice = grpc_slice_from_copied_string(ac->addr_str);
  gpr_mu_unlock(&ac->mu);
  if (error != GRPC_ERROR_NONE) {
    char* error_descr;
    grpc_slice str;
    bool ret = grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &str);
    GPR_ASSERT(ret);
    char* desc = grpc_slice_to_c_string(str);
    gpr_asprintf(&error_descr, "Failed to connect to remote host: %s", desc);
    error = grpc_error_set_str(error, GRPC_ERROR_STR_DESCRIPTION,
                               grpc_slice_from_copied_string(error_descr));
    gpr_free(error_descr);
    gpr_free(desc);
    error = grpc_error_set_str(error, GRPC_ERROR_STR_TARGET_ADDRESS,
                               addr_str_slice /* takes ownership */);
  } else {
    grpc_slice_unref(addr_str_slice);
  }
  if (done) {
    // This is safe even outside the lock, because "done", the sentinel, is
    // populated *inside* the lock.
    gpr_mu_destroy(&ac->mu);
    gpr_free(ac->addr_str);
    grpc_channel_args_destroy(ac->channel_args);
    gpr_free(ac);
  }
  GRPC_CLOSURE_SCHED(closure, error);
}

grpc_error* grpc_tcp_client_prepare_fd(const grpc_channel_args* channel_args,
                                       const grpc_resolved_address* addr,
                                       grpc_resolved_address* mapped_addr,
                                       grpc_fd** fdobj) {
  grpc_dualstack_mode dsmode;
  int fd;
  grpc_error* error;
  char* name;
  char* addr_str;
  *fdobj = nullptr;
  /* Use dualstack sockets where available. Set mapped to v6 or v4 mapped to
     v6. */
  if (!grpc_sockaddr_to_v4mapped(addr, mapped_addr)) {
    /* addr is v4 mapped to v6 or v6. */
    memcpy(mapped_addr, addr, sizeof(*mapped_addr));
  }
  error =
      grpc_create_dualstack_socket(mapped_addr, SOCK_STREAM, 0, &dsmode, &fd);
  if (error != GRPC_ERROR_NONE) {
    return error;
  }
  if (dsmode == GRPC_DSMODE_IPV4) {
    /* Original addr is either v4 or v4 mapped to v6. Set mapped_addr to v4. */
    if (!grpc_sockaddr_is_v4mapped(addr, mapped_addr)) {
      memcpy(mapped_addr, addr, sizeof(*mapped_addr));
    }
  }
  if ((error = prepare_socket(mapped_addr, fd, channel_args)) !=
      GRPC_ERROR_NONE) {
    return error;
  }
  addr_str = grpc_sockaddr_to_uri(mapped_addr);
  gpr_asprintf(&name, "tcp-client:%s", addr_str);
  *fdobj = grpc_fd_create(fd, name);
  gpr_free(name);
  gpr_free(addr_str);
  return GRPC_ERROR_NONE;
}

void grpc_tcp_client_create_from_prepared_fd(
    grpc_pollset_set* interested_parties, grpc_closure* closure, grpc_fd* fdobj,
    const grpc_channel_args* channel_args, const grpc_resolved_address* addr,
    grpc_millis deadline, grpc_endpoint** ep) {
  const int fd = grpc_fd_wrapped_fd(fdobj);
  int err;
  async_connect* ac;
  do {
    GPR_ASSERT(addr->len < ~(socklen_t)0);
    err = connect(fd, (const struct sockaddr*)addr->addr, (socklen_t)addr->len);
  } while (err < 0 && errno == EINTR);
  if (err >= 0) {
    char* addr_str = grpc_sockaddr_to_uri(addr);
    *ep = grpc_tcp_client_create_from_fd(fdobj, channel_args, addr_str);
    gpr_free(addr_str);
    GRPC_CLOSURE_SCHED(closure, GRPC_ERROR_NONE);
    return;
  }
  if (errno != EWOULDBLOCK && errno != EINPROGRESS) {
    grpc_fd_orphan(fdobj, nullptr, nullptr, false /* already_closed */,
                   "tcp_client_connect_error");
    GRPC_CLOSURE_SCHED(closure, GRPC_OS_ERROR(errno, "connect"));
    return;
  }

  grpc_pollset_set_add_fd(interested_parties, fdobj);

  ac = (async_connect*)gpr_malloc(sizeof(async_connect));
  ac->closure = closure;
  ac->ep = ep;
  ac->fd = fdobj;
  ac->interested_parties = interested_parties;
  ac->addr_str = grpc_sockaddr_to_uri(addr);
  gpr_mu_init(&ac->mu);
  ac->refs = 2;
  GRPC_CLOSURE_INIT(&ac->write_closure, on_writable, ac,
                    grpc_schedule_on_exec_ctx);
  ac->channel_args = grpc_channel_args_copy(channel_args);

  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "CLIENT_CONNECT: %s: asynchronously connecting fd %p",
            ac->addr_str, fdobj);
  }

  gpr_mu_lock(&ac->mu);
  GRPC_CLOSURE_INIT(&ac->on_alarm, tc_on_alarm, ac, grpc_schedule_on_exec_ctx);
  grpc_timer_init(&ac->alarm, deadline, &ac->on_alarm);
  grpc_fd_notify_on_write(ac->fd, &ac->write_closure);
  gpr_mu_unlock(&ac->mu);
}

static void tcp_client_connect_impl(grpc_closure* closure, grpc_endpoint** ep,
                                    grpc_pollset_set* interested_parties,
                                    const grpc_channel_args* channel_args,
                                    const grpc_resolved_address* addr,
                                    grpc_millis deadline) {
  grpc_resolved_address mapped_addr;
  grpc_fd* fdobj = nullptr;
  grpc_error* error;
  *ep = nullptr;
  if ((error = grpc_tcp_client_prepare_fd(channel_args, addr, &mapped_addr,
                                          &fdobj)) != GRPC_ERROR_NONE) {
    GRPC_CLOSURE_SCHED(closure, error);
    return;
  }
  grpc_tcp_client_create_from_prepared_fd(interested_parties, closure, fdobj,
                                          channel_args, &mapped_addr, deadline,
                                          ep);
}

// overridden by api_fuzzer.c
void (*grpc_tcp_client_connect_impl)(
    grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties, const grpc_channel_args* channel_args,
    const grpc_resolved_address* addr,
    grpc_millis deadline) = tcp_client_connect_impl;

void grpc_tcp_client_connect(grpc_closure* closure, grpc_endpoint** ep,
                             grpc_pollset_set* interested_parties,
                             const grpc_channel_args* channel_args,
                             const grpc_resolved_address* addr,
                             grpc_millis deadline) {
  grpc_tcp_client_connect_impl(closure, ep, interested_parties, channel_args,
                               addr, deadline);
}

#endif
