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
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_posix.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_mutator.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/support/string.h"

extern int grpc_tcp_trace;

typedef struct {
  gpr_mu mu;
  grpc_fd *fd;
  gpr_timespec deadline;
  grpc_timer alarm;
  grpc_closure on_alarm;
  int refs;
  grpc_closure write_closure;
  grpc_pollset_set *interested_parties;
  char *addr_str;
  grpc_endpoint **ep;
  grpc_closure *closure;
  grpc_channel_args *channel_args;
} async_connect;

static grpc_error *prepare_socket(const grpc_resolved_address *addr, int fd,
                                  const grpc_channel_args *channel_args) {
  grpc_error *err = GRPC_ERROR_NONE;

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
        grpc_socket_mutator *mutator = channel_args->args[i].value.pointer.p;
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

static void tc_on_alarm(grpc_exec_ctx *exec_ctx, void *acp, grpc_error *error) {
  int done;
  async_connect *ac = acp;
  if (grpc_tcp_trace) {
    const char *str = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "CLIENT_CONNECT: %s: on_alarm: error=%s", ac->addr_str,
            str);
  }
  gpr_mu_lock(&ac->mu);
  if (ac->fd != NULL) {
    grpc_fd_shutdown(exec_ctx, ac->fd,
                     GRPC_ERROR_CREATE("connect() timed out"));
  }
  done = (--ac->refs == 0);
  gpr_mu_unlock(&ac->mu);
  if (done) {
    gpr_mu_destroy(&ac->mu);
    gpr_free(ac->addr_str);
    grpc_channel_args_destroy(exec_ctx, ac->channel_args);
    gpr_free(ac);
  }
}

grpc_endpoint *grpc_tcp_client_create_from_fd(
    grpc_exec_ctx *exec_ctx, grpc_fd *fd, const grpc_channel_args *channel_args,
    const char *addr_str) {
  size_t tcp_read_chunk_size = GRPC_TCP_DEFAULT_READ_SLICE_SIZE;
  grpc_resource_quota *resource_quota = grpc_resource_quota_create(NULL);
  if (channel_args != NULL) {
    for (size_t i = 0; i < channel_args->num_args; i++) {
      if (0 ==
          strcmp(channel_args->args[i].key, GRPC_ARG_TCP_READ_CHUNK_SIZE)) {
        grpc_integer_options options = {(int)tcp_read_chunk_size, 1,
                                        8 * 1024 * 1024};
        tcp_read_chunk_size = (size_t)grpc_channel_arg_get_integer(
            &channel_args->args[i], options);
      } else if (0 ==
                 strcmp(channel_args->args[i].key, GRPC_ARG_RESOURCE_QUOTA)) {
        grpc_resource_quota_unref_internal(exec_ctx, resource_quota);
        resource_quota = grpc_resource_quota_ref_internal(
            channel_args->args[i].value.pointer.p);
      }
    }
  }

  grpc_endpoint *ep =
      grpc_tcp_create(fd, resource_quota, tcp_read_chunk_size, addr_str);
  grpc_resource_quota_unref_internal(exec_ctx, resource_quota);
  return ep;
}

static void on_writable(grpc_exec_ctx *exec_ctx, void *acp, grpc_error *error) {
  async_connect *ac = acp;
  int so_error = 0;
  socklen_t so_error_size;
  int err;
  int done;
  grpc_endpoint **ep = ac->ep;
  grpc_closure *closure = ac->closure;
  grpc_fd *fd;

  GRPC_ERROR_REF(error);

  if (grpc_tcp_trace) {
    const char *str = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "CLIENT_CONNECT: %s: on_writable: error=%s",
            ac->addr_str, str);
  }

  gpr_mu_lock(&ac->mu);
  GPR_ASSERT(ac->fd);
  fd = ac->fd;
  ac->fd = NULL;
  gpr_mu_unlock(&ac->mu);

  grpc_timer_cancel(exec_ctx, &ac->alarm);

  gpr_mu_lock(&ac->mu);
  if (error != GRPC_ERROR_NONE) {
    error =
        grpc_error_set_str(error, GRPC_ERROR_STR_OS_ERROR, "Timeout occurred");
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
      grpc_pollset_set_del_fd(exec_ctx, ac->interested_parties, fd);
      *ep = grpc_tcp_client_create_from_fd(exec_ctx, fd, ac->channel_args,
                                           ac->addr_str);
      fd = NULL;
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
      grpc_fd_notify_on_write(exec_ctx, fd, &ac->write_closure);
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
  if (fd != NULL) {
    grpc_pollset_set_del_fd(exec_ctx, ac->interested_parties, fd);
    grpc_fd_orphan(exec_ctx, fd, NULL, NULL, "tcp_client_orphan");
    fd = NULL;
  }
  done = (--ac->refs == 0);
  gpr_mu_unlock(&ac->mu);
  if (error != GRPC_ERROR_NONE) {
    char *error_descr;
    gpr_asprintf(&error_descr, "Failed to connect to remote host: %s",
                 grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION));
    error = grpc_error_set_str(error, GRPC_ERROR_STR_DESCRIPTION, error_descr);
    gpr_free(error_descr);
    error =
        grpc_error_set_str(error, GRPC_ERROR_STR_TARGET_ADDRESS, ac->addr_str);
  }
  if (done) {
    gpr_mu_destroy(&ac->mu);
    gpr_free(ac->addr_str);
    grpc_channel_args_destroy(exec_ctx, ac->channel_args);
    gpr_free(ac);
  }
  grpc_closure_sched(exec_ctx, closure, error);
}

static void tcp_client_connect_impl(grpc_exec_ctx *exec_ctx,
                                    grpc_closure *closure, grpc_endpoint **ep,
                                    grpc_pollset_set *interested_parties,
                                    const grpc_channel_args *channel_args,
                                    const grpc_resolved_address *addr,
                                    gpr_timespec deadline) {
  int fd;
  grpc_dualstack_mode dsmode;
  int err;
  async_connect *ac;
  grpc_resolved_address addr6_v4mapped;
  grpc_resolved_address addr4_copy;
  grpc_fd *fdobj;
  char *name;
  char *addr_str;
  grpc_error *error;

  *ep = NULL;

  /* Use dualstack sockets where available. */
  if (grpc_sockaddr_to_v4mapped(addr, &addr6_v4mapped)) {
    addr = &addr6_v4mapped;
  }

  error = grpc_create_dualstack_socket(addr, SOCK_STREAM, 0, &dsmode, &fd);
  if (error != GRPC_ERROR_NONE) {
    grpc_closure_sched(exec_ctx, closure, error);
    return;
  }
  if (dsmode == GRPC_DSMODE_IPV4) {
    /* If we got an AF_INET socket, map the address back to IPv4. */
    GPR_ASSERT(grpc_sockaddr_is_v4mapped(addr, &addr4_copy));
    addr = &addr4_copy;
  }
  if ((error = prepare_socket(addr, fd, channel_args)) != GRPC_ERROR_NONE) {
    grpc_closure_sched(exec_ctx, closure, error);
    return;
  }

  do {
    GPR_ASSERT(addr->len < ~(socklen_t)0);
    err =
        connect(fd, (const struct sockaddr *)addr->addr, (socklen_t)addr->len);
  } while (err < 0 && errno == EINTR);

  addr_str = grpc_sockaddr_to_uri(addr);
  gpr_asprintf(&name, "tcp-client:%s", addr_str);

  fdobj = grpc_fd_create(fd, name);

  if (err >= 0) {
    *ep =
        grpc_tcp_client_create_from_fd(exec_ctx, fdobj, channel_args, addr_str);
    grpc_closure_sched(exec_ctx, closure, GRPC_ERROR_NONE);
    goto done;
  }

  if (errno != EWOULDBLOCK && errno != EINPROGRESS) {
    grpc_fd_orphan(exec_ctx, fdobj, NULL, NULL, "tcp_client_connect_error");
    grpc_closure_sched(exec_ctx, closure, GRPC_OS_ERROR(errno, "connect"));
    goto done;
  }

  grpc_pollset_set_add_fd(exec_ctx, interested_parties, fdobj);

  ac = gpr_malloc(sizeof(async_connect));
  ac->closure = closure;
  ac->ep = ep;
  ac->fd = fdobj;
  ac->interested_parties = interested_parties;
  ac->addr_str = addr_str;
  addr_str = NULL;
  gpr_mu_init(&ac->mu);
  ac->refs = 2;
  grpc_closure_init(&ac->write_closure, on_writable, ac,
                    grpc_schedule_on_exec_ctx);
  ac->channel_args = grpc_channel_args_copy(channel_args);

  if (grpc_tcp_trace) {
    gpr_log(GPR_DEBUG, "CLIENT_CONNECT: %s: asynchronously connecting",
            ac->addr_str);
  }

  gpr_mu_lock(&ac->mu);
  grpc_closure_init(&ac->on_alarm, tc_on_alarm, ac, grpc_schedule_on_exec_ctx);
  grpc_timer_init(exec_ctx, &ac->alarm,
                  gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC),
                  &ac->on_alarm, gpr_now(GPR_CLOCK_MONOTONIC));
  grpc_fd_notify_on_write(exec_ctx, ac->fd, &ac->write_closure);
  gpr_mu_unlock(&ac->mu);

done:
  gpr_free(name);
  gpr_free(addr_str);
}

// overridden by api_fuzzer.c
void (*grpc_tcp_client_connect_impl)(
    grpc_exec_ctx *exec_ctx, grpc_closure *closure, grpc_endpoint **ep,
    grpc_pollset_set *interested_parties, const grpc_channel_args *channel_args,
    const grpc_resolved_address *addr,
    gpr_timespec deadline) = tcp_client_connect_impl;

void grpc_tcp_client_connect(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                             grpc_endpoint **ep,
                             grpc_pollset_set *interested_parties,
                             const grpc_channel_args *channel_args,
                             const grpc_resolved_address *addr,
                             gpr_timespec deadline) {
  grpc_tcp_client_connect_impl(exec_ctx, closure, ep, interested_parties,
                               channel_args, addr, deadline);
}

#endif
