//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP_CLIENT

#include <errno.h>
#include <grpc/support/alloc.h>
#include <grpc/support/time.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/resolved_address_internal.h"
#include "src/core/lib/event_engine/shim.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/event_engine_shims/tcp_client.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_mutator.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/tcp_client_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/iomgr/vsock.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/crash.h"
#include "src/core/util/string.h"

using ::grpc_event_engine::experimental::EndpointConfig;

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
  int64_t connection_handle;
  bool connect_cancelled;
  grpc_core::PosixTcpOptions options;
};

struct ConnectionShard {
  grpc_core::Mutex mu;
  absl::flat_hash_map<int64_t, async_connect*> pending_connections
      ABSL_GUARDED_BY(&mu);
};

namespace {

gpr_once g_tcp_client_posix_init = GPR_ONCE_INIT;
std::vector<ConnectionShard>* g_connection_shards = nullptr;
std::atomic<int64_t> g_connection_id{1};

void do_tcp_client_global_init(void) {
  size_t num_shards = std::max(2 * gpr_cpu_num_cores(), 1u);
  g_connection_shards = new std::vector<struct ConnectionShard>(num_shards);
}

}  // namespace

void grpc_tcp_client_global_init() {
  gpr_once_init(&g_tcp_client_posix_init, do_tcp_client_global_init);
}

static grpc_error_handle prepare_socket(
    const grpc_resolved_address* addr, int fd,
    const grpc_core::PosixTcpOptions& options) {
  grpc_error_handle err;

  CHECK_GE(fd, 0);

  err = grpc_set_socket_nonblocking(fd, 1);
  if (!err.ok()) goto error;
  err = grpc_set_socket_cloexec(fd, 1);
  if (!err.ok()) goto error;
  if (options.tcp_receive_buffer_size != options.kReadBufferSizeUnset) {
    err = grpc_set_socket_rcvbuf(fd, options.tcp_receive_buffer_size);
    if (!err.ok()) goto error;
  }
  if (!grpc_is_unix_socket(addr) && !grpc_is_vsock(addr)) {
    err = grpc_set_socket_low_latency(fd, 1);
    if (!err.ok()) goto error;
    err = grpc_set_socket_reuse_addr(fd, 1);
    if (!err.ok()) goto error;
    err = grpc_set_socket_dscp(fd, options.dscp);
    if (!err.ok()) goto error;
    err = grpc_set_socket_tcp_user_timeout(fd, options, true /* is_client */);
    if (!err.ok()) goto error;
  }
  err = grpc_set_socket_no_sigpipe_if_possible(fd);
  if (!err.ok()) goto error;

  err = grpc_apply_socket_mutator_in_args(fd, GRPC_FD_CLIENT_CONNECTION_USAGE,
                                          options);
  if (!err.ok()) goto error;

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
  GRPC_TRACE_LOG(tcp, INFO)
      << "CLIENT_CONNECT: " << ac->addr_str
      << ": on_alarm: error=" << grpc_core::StatusToString(error);
  gpr_mu_lock(&ac->mu);
  if (ac->fd != nullptr) {
    grpc_fd_shutdown(ac->fd, GRPC_ERROR_CREATE("connect() timed out"));
  }
  done = (--ac->refs == 0);
  gpr_mu_unlock(&ac->mu);
  if (done) {
    gpr_mu_destroy(&ac->mu);
    delete ac;
  }
}

static grpc_endpoint* grpc_tcp_client_create_from_fd(
    grpc_fd* fd, const grpc_core::PosixTcpOptions& options,
    absl::string_view addr_str) {
  return grpc_tcp_create(fd, options, addr_str);
}

grpc_endpoint* grpc_tcp_create_from_fd(
    grpc_fd* fd, const grpc_event_engine::experimental::EndpointConfig& config,
    absl::string_view addr_str) {
  return grpc_tcp_create(fd, TcpOptionsFromEndpointConfig(config), addr_str);
}

static void on_writable(void* acp, grpc_error_handle error) {
  async_connect* ac = static_cast<async_connect*>(acp);
  int so_error = 0;
  socklen_t so_error_size;
  int err;
  int done;
  grpc_endpoint** ep = ac->ep;
  grpc_closure* closure = ac->closure;
  std::string addr_str = ac->addr_str;
  grpc_fd* fd;

  GRPC_TRACE_LOG(tcp, INFO)
      << "CLIENT_CONNECT: " << ac->addr_str
      << ": on_writable: error=" << grpc_core::StatusToString(error);

  gpr_mu_lock(&ac->mu);
  CHECK(ac->fd);
  fd = ac->fd;
  ac->fd = nullptr;
  bool connect_cancelled = ac->connect_cancelled;
  gpr_mu_unlock(&ac->mu);

  grpc_timer_cancel(&ac->alarm);

  gpr_mu_lock(&ac->mu);
  if (!error.ok()) {
    error = grpc_core::AddMessagePrefix("Timeout occurred", error);
    goto finish;
  }

  if (connect_cancelled) {
    // The callback should not get scheduled in this case.
    error = absl::OkStatus();
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
      *ep = grpc_tcp_client_create_from_fd(fd, ac->options, ac->addr_str);
      fd = nullptr;
      break;
    case ENOBUFS:
      // We will get one of these errors if we have run out of
      // memory in the kernel for the data structures allocated
      // when you connect a socket.  If this happens it is very
      // likely that if we wait a little bit then try again the
      // connection will work (since other programs or this
      // program will close their network connections and free up
      // memory).  This does _not_ indicate that there is anything
      // wrong with the server we are connecting to, this is a
      // local problem.

      // If you are looking at this code, then chances are that
      // your program or another program on the same computer
      // opened too many network connections.  The "easy" fix:
      // don't do that!
      LOG(ERROR) << "kernel out of buffers";
      gpr_mu_unlock(&ac->mu);
      grpc_fd_notify_on_write(fd, &ac->write_closure);
      return;
    case ECONNREFUSED:
      // This error shouldn't happen for anything other than connect().
      error = GRPC_OS_ERROR(so_error, "connect");
      break;
    default:
      // We don't really know which syscall triggered the problem here,
      // so punt by reporting getsockopt().
      error = GRPC_OS_ERROR(so_error, "getsockopt(SO_ERROR)");
      break;
  }

finish:
  if (!connect_cancelled) {
    int shard_number = ac->connection_handle % (*g_connection_shards).size();
    struct ConnectionShard* shard = &(*g_connection_shards)[shard_number];
    {
      grpc_core::MutexLock lock(&shard->mu);
      shard->pending_connections.erase(ac->connection_handle);
    }
  }
  if (fd != nullptr) {
    grpc_pollset_set_del_fd(ac->interested_parties, fd);
    grpc_fd_orphan(fd, nullptr, nullptr, "tcp_client_orphan");
    fd = nullptr;
  }
  done = (--ac->refs == 0);
  gpr_mu_unlock(&ac->mu);
  if (!error.ok()) {
    std::string str;
    bool ret = grpc_error_get_str(
        error, grpc_core::StatusStrProperty::kDescription, &str);
    CHECK(ret);
    std::string description =
        absl::StrCat("Failed to connect to remote host: ", str);
    error = grpc_error_set_str(
        error, grpc_core::StatusStrProperty::kDescription, description);
  }
  if (done) {
    // This is safe even outside the lock, because "done", the sentinel, is
    // populated *inside* the lock.
    gpr_mu_destroy(&ac->mu);
    delete ac;
  }
  // Push async connect closure to the executor since this may actually be
  // called during the shutdown process, in which case a deadlock could form
  // between the core shutdown mu and the connector mu (b/188239051)
  if (!connect_cancelled) {
    grpc_core::Executor::Run(closure, error);
  }
}

grpc_error_handle grpc_tcp_client_prepare_fd(
    const grpc_core::PosixTcpOptions& options,
    const grpc_resolved_address* addr, grpc_resolved_address* mapped_addr,
    int* fd) {
  grpc_dualstack_mode dsmode;
  grpc_error_handle error;
  *fd = -1;
  // Use dualstack sockets where available. Set mapped to v6 or v4 mapped to
  // v6.
  if (!grpc_sockaddr_to_v4mapped(addr, mapped_addr)) {
    // addr is v4 mapped to v6 or v6.
    memcpy(mapped_addr, addr, sizeof(*mapped_addr));
  }
  error =
      grpc_create_dualstack_socket(mapped_addr, SOCK_STREAM, 0, &dsmode, fd);
  if (!error.ok()) {
    return error;
  }
  if (dsmode == GRPC_DSMODE_IPV4) {
    // Original addr is either v4 or v4 mapped to v6. Set mapped_addr to v4.
    if (!grpc_sockaddr_is_v4mapped(addr, mapped_addr)) {
      memcpy(mapped_addr, addr, sizeof(*mapped_addr));
    }
  }
  if ((error = prepare_socket(mapped_addr, *fd, options)) != absl::OkStatus()) {
    return error;
  }
  return absl::OkStatus();
}

int64_t grpc_tcp_client_create_from_prepared_fd(
    grpc_pollset_set* interested_parties, grpc_closure* closure, const int fd,
    const grpc_core::PosixTcpOptions& options,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline,
    grpc_endpoint** ep) {
  int err;
  do {
    err = connect(fd, reinterpret_cast<const grpc_sockaddr*>(addr->addr),
                  addr->len);
  } while (err < 0 && errno == EINTR);
  int connect_errno = (err < 0) ? errno : 0;

  auto addr_uri = grpc_sockaddr_to_uri(addr);
  if (!addr_uri.ok()) {
    grpc_error_handle error = GRPC_ERROR_CREATE(addr_uri.status().ToString());
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, error);
    return 0;
  }

  std::string name = absl::StrCat("tcp-client:", *addr_uri);
  grpc_fd* fdobj = grpc_fd_create(fd, name.c_str(), true);
  int64_t connection_id = 0;
  if (connect_errno == EWOULDBLOCK || connect_errno == EINPROGRESS) {
    // Connection is still in progress.
    connection_id = g_connection_id.fetch_add(1, std::memory_order_acq_rel);
  }

  if (err >= 0) {
    // Connection already succeded. Return 0 to discourage any cancellation
    // attempts.
    *ep = grpc_tcp_client_create_from_fd(fdobj, options, *addr_uri);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, absl::OkStatus());
    return 0;
  }
  if (connect_errno != EWOULDBLOCK && connect_errno != EINPROGRESS) {
    // Connection already failed. Return 0 to discourage any cancellation
    // attempts.
    grpc_error_handle error = GRPC_OS_ERROR(connect_errno, "connect");
    grpc_fd_orphan(fdobj, nullptr, nullptr, "tcp_client_connect_error");
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, error);
    return 0;
  }

  grpc_pollset_set_add_fd(interested_parties, fdobj);

  async_connect* ac = new async_connect();
  ac->closure = closure;
  ac->ep = ep;
  ac->fd = fdobj;
  ac->interested_parties = interested_parties;
  ac->addr_str = addr_uri.value();
  ac->connection_handle = connection_id;
  ac->connect_cancelled = false;
  gpr_mu_init(&ac->mu);
  ac->refs = 2;
  GRPC_CLOSURE_INIT(&ac->write_closure, on_writable, ac,
                    grpc_schedule_on_exec_ctx);
  ac->options = options;

  GRPC_TRACE_LOG(tcp, INFO) << "CLIENT_CONNECT: " << ac->addr_str
                            << ": asynchronously connecting fd " << fdobj;

  int shard_number = connection_id % (*g_connection_shards).size();
  struct ConnectionShard* shard = &(*g_connection_shards)[shard_number];
  {
    grpc_core::MutexLock lock(&shard->mu);
    shard->pending_connections.insert_or_assign(connection_id, ac);
  }

  gpr_mu_lock(&ac->mu);
  GRPC_CLOSURE_INIT(&ac->on_alarm, tc_on_alarm, ac, grpc_schedule_on_exec_ctx);
  grpc_timer_init(&ac->alarm, deadline, &ac->on_alarm);
  grpc_fd_notify_on_write(ac->fd, &ac->write_closure);
  gpr_mu_unlock(&ac->mu);
  return connection_id;
}

static int64_t tcp_connect(grpc_closure* closure, grpc_endpoint** ep,
                           grpc_pollset_set* interested_parties,
                           const EndpointConfig& config,
                           const grpc_resolved_address* addr,
                           grpc_core::Timestamp deadline) {
  if (grpc_event_engine::experimental::UseEventEngineClient()) {
    return grpc_event_engine::experimental::event_engine_tcp_client_connect(
        closure, ep, config, addr, deadline);
  }
  grpc_resolved_address mapped_addr;
  grpc_core::PosixTcpOptions options(TcpOptionsFromEndpointConfig(config));
  int fd = -1;
  grpc_error_handle error;
  *ep = nullptr;
  if ((error = grpc_tcp_client_prepare_fd(options, addr, &mapped_addr, &fd)) !=
      absl::OkStatus()) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, error);
    return 0;
  }
  return grpc_tcp_client_create_from_prepared_fd(
      interested_parties, closure, fd, options, &mapped_addr, deadline, ep);
}

static bool tcp_cancel_connect(int64_t connection_handle) {
  if (grpc_event_engine::experimental::UseEventEngineClient()) {
    return grpc_event_engine::experimental::
        event_engine_tcp_client_cancel_connect(connection_handle);
  }
  if (connection_handle <= 0) {
    return false;
  }
  int shard_number = connection_handle % (*g_connection_shards).size();
  struct ConnectionShard* shard = &(*g_connection_shards)[shard_number];
  async_connect* ac = nullptr;
  {
    grpc_core::MutexLock lock(&shard->mu);
    auto it = shard->pending_connections.find(connection_handle);
    if (it != shard->pending_connections.end()) {
      ac = it->second;
      CHECK_NE(ac, nullptr);
      // Trying to acquire ac->mu here would could cause a deadlock because
      // the on_writable method tries to acquire the two mutexes used
      // here in the reverse order. But we dont need to acquire ac->mu before
      // incrementing ac->refs here. This is because the on_writable
      // method decrements ac->refs only after deleting the connection handle
      // from the corresponding hashmap. If the code enters here, it means that
      // deletion hasn't happened yet. The deletion can only happen after the
      // corresponding g_shard_mu is unlocked.
      ++ac->refs;
      // Remove connection from list of active connections.
      shard->pending_connections.erase(it);
    }
  }
  if (ac == nullptr) {
    return false;
  }
  gpr_mu_lock(&ac->mu);
  bool connection_cancel_success = (ac->fd != nullptr);
  if (connection_cancel_success) {
    // Connection is still pending. The on_writable callback hasn't executed
    // yet because ac->fd != nullptr.
    ac->connect_cancelled = true;
    // Shutdown the fd. This would cause on_writable to run as soon as possible.
    // We dont need to pass a custom error here because it wont be used since
    // the on_connect_closure is not run if connect cancellation is successfull.
    grpc_fd_shutdown(ac->fd, absl::OkStatus());
  }
  bool done = (--ac->refs == 0);
  gpr_mu_unlock(&ac->mu);
  if (done) {
    // This is safe even outside the lock, because "done", the sentinel, is
    // populated *inside* the lock.
    gpr_mu_destroy(&ac->mu);
    delete ac;
  }
  return connection_cancel_success;
}

grpc_tcp_client_vtable grpc_posix_tcp_client_vtable = {tcp_connect,
                                                       tcp_cancel_connect};
#endif
