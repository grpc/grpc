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

#ifdef GRPC_POSIX_SOCKET_TCP_CLIENT

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

#include "absl/strings/str_cat.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_mutator.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/tcp_client_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/slice/slice_internal.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

struct async_connect {
  gpr_mu mu;
  grpc_fd* fd;
  grpc_timer alarm;
  grpc_closure on_alarm;
  int refs;
  grpc_closure write_closure;
  grpc_pollset_set* interested_parties;
  std::string addr_str;
  grpc_endpoint** ep;
  grpc_closure* closure;
  grpc_channel_args* channel_args;
};

static grpc_error_handle prepare_socket(const grpc_resolved_address* addr,
                                        int fd,
                                        const grpc_channel_args* channel_args) {
  grpc_error_handle err = GRPC_ERROR_NONE;

  GPR_ASSERT(fd >= 0);

  err = grpc_set_socket_nonblocking(fd, 1);
  if (err != GRPC_ERROR_NONE) goto error;
  err = grpc_set_socket_cloexec(fd, 1);
  if (err != GRPC_ERROR_NONE) goto error;
  if (!grpc_is_unix_socket(addr)) {
    err = grpc_set_socket_low_latency(fd, 1);
    if (err != GRPC_ERROR_NONE) goto error;
    err = grpc_set_socket_reuse_addr(fd, 1);
    if (err != GRPC_ERROR_NONE) goto error;
    err = grpc_set_socket_tcp_user_timeout(fd, channel_args,
                                           true /* is_client */);
    if (err != GRPC_ERROR_NONE) goto error;
  }
  err = grpc_set_socket_no_sigpipe_if_possible(fd);
  if (err != GRPC_ERROR_NONE) goto error;

  err = grpc_apply_socket_mutator_in_args(fd, GRPC_FD_CLIENT_CONNECTION_USAGE,
                                          channel_args);
  if (err != GRPC_ERROR_NONE) goto error;

  goto done;

error:
  if (fd >= 0) {
    close(fd);
  }
done:
  return err;
}

static void tc_on_alarm(void* acp, grpc_error_handle error) {
  int done;
  async_connect* ac = static_cast<async_connect*>(acp);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "CLIENT_CONNECT: %s: on_alarm: error=%s",
            ac->addr_str.c_str(), grpc_error_std_string(error).c_str());
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
    grpc_channel_args_destroy(ac->channel_args);
    delete ac;
  }
}

grpc_endpoint* grpc_tcp_client_create_from_fd(
    grpc_fd* fd, const grpc_channel_args* channel_args,
    absl::string_view addr_str) {
  return grpc_tcp_create(fd, channel_args, addr_str);
}

static void on_writable(void* acp, grpc_error_handle error) {
  async_connect* ac = static_cast<async_connect*>(acp);
  int so_error = 0;
  socklen_t so_error_size;
  int err;
  int done;
  grpc_endpoint** ep = ac->ep;
  grpc_closure* closure = ac->closure;
  grpc_fd* fd;

  (void)GRPC_ERROR_REF(error);

  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "CLIENT_CONNECT: %s: on_writable: error=%s",
            ac->addr_str.c_str(), grpc_error_std_string(error).c_str());
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
    grpc_fd_orphan(fd, nullptr, nullptr, "tcp_client_orphan");
    fd = nullptr;
  }
  done = (--ac->refs == 0);
  gpr_mu_unlock(&ac->mu);
  if (error != GRPC_ERROR_NONE) {
    std::string str;
    bool ret = grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &str);
    GPR_ASSERT(ret);
    std::string description =
        absl::StrCat("Failed to connect to remote host: ", str);
    error = grpc_error_set_str(error, GRPC_ERROR_STR_DESCRIPTION, description);
    error =
        grpc_error_set_str(error, GRPC_ERROR_STR_TARGET_ADDRESS, ac->addr_str);
  }
  if (done) {
    // This is safe even outside the lock, because "done", the sentinel, is
    // populated *inside* the lock.
    gpr_mu_destroy(&ac->mu);
    grpc_channel_args_destroy(ac->channel_args);
    delete ac;
  }
  // Push async connect closure to the executor since this may actually be
  // called during the shutdown process, in which case a deadlock could form
  // between the core shutdown mu and the connector mu (b/188239051)
  grpc_core::Executor::Run(closure, error);
}

grpc_error_handle grpc_tcp_client_prepare_fd(
    const grpc_channel_args* channel_args, const grpc_resolved_address* addr,
    grpc_resolved_address* mapped_addr, int* fd) {
  grpc_dualstack_mode dsmode;
  grpc_error_handle error;
  *fd = -1;
  /* Use dualstack sockets where available. Set mapped to v6 or v4 mapped to
     v6. */
  if (!grpc_sockaddr_to_v4mapped(addr, mapped_addr)) {
    /* addr is v4 mapped to v6 or v6. */
    memcpy(mapped_addr, addr, sizeof(*mapped_addr));
  }
  error =
      grpc_create_dualstack_socket(mapped_addr, SOCK_STREAM, 0, &dsmode, fd);
  if (error != GRPC_ERROR_NONE) {
    return error;
  }
  if (dsmode == GRPC_DSMODE_IPV4) {
    /* Original addr is either v4 or v4 mapped to v6. Set mapped_addr to v4. */
    if (!grpc_sockaddr_is_v4mapped(addr, mapped_addr)) {
      memcpy(mapped_addr, addr, sizeof(*mapped_addr));
    }
  }
  if ((error = prepare_socket(mapped_addr, *fd, channel_args)) !=
      GRPC_ERROR_NONE) {
    return error;
  }
  return GRPC_ERROR_NONE;
}

void grpc_tcp_client_create_from_prepared_fd(
    grpc_pollset_set* interested_parties, grpc_closure* closure, const int fd,
    const grpc_channel_args* channel_args, const grpc_resolved_address* addr,
    grpc_millis deadline, grpc_endpoint** ep) {
  int err;
  do {
    err = connect(fd, reinterpret_cast<const grpc_sockaddr*>(addr->addr),
                  addr->len);
  } while (err < 0 && errno == EINTR);

  std::string name = absl::StrCat("tcp-client:", grpc_sockaddr_to_uri(addr));
  grpc_fd* fdobj = grpc_fd_create(fd, name.c_str(), true);

  if (err >= 0) {
    *ep = grpc_tcp_client_create_from_fd(fdobj, channel_args,
                                         grpc_sockaddr_to_uri(addr));
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, GRPC_ERROR_NONE);
    return;
  }
  if (errno != EWOULDBLOCK && errno != EINPROGRESS) {
    grpc_error_handle error = GRPC_OS_ERROR(errno, "connect");
    error = grpc_error_set_str(error, GRPC_ERROR_STR_TARGET_ADDRESS,
                               grpc_sockaddr_to_uri(addr));
    grpc_fd_orphan(fdobj, nullptr, nullptr, "tcp_client_connect_error");
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, error);
    return;
  }

  grpc_pollset_set_add_fd(interested_parties, fdobj);

  async_connect* ac = new async_connect();
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

  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "CLIENT_CONNECT: %s: asynchronously connecting fd %p",
            ac->addr_str.c_str(), fdobj);
  }

  gpr_mu_lock(&ac->mu);
  GRPC_CLOSURE_INIT(&ac->on_alarm, tc_on_alarm, ac, grpc_schedule_on_exec_ctx);
  grpc_timer_init(&ac->alarm, deadline, &ac->on_alarm);
  grpc_fd_notify_on_write(ac->fd, &ac->write_closure);
  gpr_mu_unlock(&ac->mu);
}

static void tcp_connect(grpc_closure* closure, grpc_endpoint** ep,
                        grpc_pollset_set* interested_parties,
                        const grpc_channel_args* channel_args,
                        const grpc_resolved_address* addr,
                        grpc_millis deadline) {
  grpc_resolved_address mapped_addr;
  int fd = -1;
  grpc_error_handle error;
  *ep = nullptr;
  if ((error = grpc_tcp_client_prepare_fd(channel_args, addr, &mapped_addr,
                                          &fd)) != GRPC_ERROR_NONE) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, error);
    return;
  }
  grpc_tcp_client_create_from_prepared_fd(interested_parties, closure, fd,
                                          channel_args, &mapped_addr, deadline,
                                          ep);
}

grpc_tcp_client_vtable grpc_posix_tcp_client_vtable = {tcp_connect};
#endif
