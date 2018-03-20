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

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/iomgr/tcp_server.h"

#include <errno.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", #x)

static gpr_mu* g_mu;
static grpc_pollset* g_pollset;
static int g_nconnects = 0;

typedef struct {
  /* Owns a ref to server. */
  grpc_tcp_server* server;
  unsigned port_index;
  unsigned fd_index;
  int server_fd;
} on_connect_result;

typedef struct {
  grpc_tcp_server* server;

  /* arg is this server_weak_ref. */
  grpc_closure server_shutdown;
} server_weak_ref;

#define MAX_URI 1024
typedef struct {
  grpc_resolved_address addr;
  char str[MAX_URI];
} test_addr;

#define MAX_ADDRS 100
typedef struct {
  size_t naddrs;
  test_addr addrs[MAX_ADDRS];
} test_addrs;

static on_connect_result g_result = {nullptr, 0, 0, -1};

static char family_name_buf[1024];
static const char* sock_family_name(int family) {
  if (family == AF_INET) {
    return "AF_INET";
  } else if (family == AF_INET6) {
    return "AF_INET6";
  } else if (family == AF_UNSPEC) {
    return "AF_UNSPEC";
  } else {
    sprintf(family_name_buf, "%d", family);
    return family_name_buf;
  }
}

static void on_connect_result_init(on_connect_result* result) {
  result->server = nullptr;
  result->port_index = 0;
  result->fd_index = 0;
  result->server_fd = -1;
}

static void on_connect_result_set(on_connect_result* result,
                                  const grpc_tcp_server_acceptor* acceptor) {
  result->server = grpc_tcp_server_ref(acceptor->from_server);
  result->port_index = acceptor->port_index;
  result->fd_index = acceptor->fd_index;
  result->server_fd = grpc_tcp_server_port_fd(
      result->server, acceptor->port_index, acceptor->fd_index);
}

static void server_weak_ref_shutdown(void* arg, grpc_error* error) {
  server_weak_ref* weak_ref = static_cast<server_weak_ref*>(arg);
  weak_ref->server = nullptr;
}

static void server_weak_ref_init(server_weak_ref* weak_ref) {
  weak_ref->server = nullptr;
  GRPC_CLOSURE_INIT(&weak_ref->server_shutdown, server_weak_ref_shutdown,
                    weak_ref, grpc_schedule_on_exec_ctx);
}

/* Make weak_ref->server_shutdown a shutdown_starting cb on server.
   grpc_tcp_server promises that the server object will live until
   weak_ref->server_shutdown has returned. A strong ref on grpc_tcp_server
   should be held until server_weak_ref_set() returns to avoid a race where the
   server is deleted before the shutdown_starting cb is added. */
static void server_weak_ref_set(server_weak_ref* weak_ref,
                                grpc_tcp_server* server) {
  grpc_tcp_server_shutdown_starting_add(server, &weak_ref->server_shutdown);
  weak_ref->server = server;
}

static void test_addr_init_str(test_addr* addr) {
  char* str = nullptr;
  if (grpc_sockaddr_to_string(&str, &addr->addr, 0) != -1) {
    size_t str_len;
    memcpy(addr->str, str, (str_len = strnlen(str, sizeof(addr->str) - 1)));
    addr->str[str_len] = '\0';
    gpr_free(str);
  } else {
    addr->str[0] = '\0';
  }
}

static void on_connect(void* arg, grpc_endpoint* tcp, grpc_pollset* pollset,
                       grpc_tcp_server_acceptor* acceptor) {
  grpc_endpoint_shutdown(tcp,
                         GRPC_ERROR_CREATE_FROM_STATIC_STRING("Connected"));
  grpc_endpoint_destroy(tcp);

  on_connect_result temp_result;
  on_connect_result_set(&temp_result, acceptor);
  gpr_free(acceptor);

  gpr_mu_lock(g_mu);
  g_result = temp_result;
  g_nconnects++;
  GPR_ASSERT(
      GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(g_pollset, nullptr)));
  gpr_mu_unlock(g_mu);
}

static void test_no_op(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_tcp_server* s;
  GPR_ASSERT(GRPC_ERROR_NONE == grpc_tcp_server_create(nullptr, nullptr, &s));
  grpc_tcp_server_unref(s);
}

static void test_no_op_with_start(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_tcp_server* s;
  GPR_ASSERT(GRPC_ERROR_NONE == grpc_tcp_server_create(nullptr, nullptr, &s));
  LOG_TEST("test_no_op_with_start");
  grpc_tcp_server_start(s, nullptr, 0, on_connect, nullptr);
  grpc_tcp_server_unref(s);
}

static void test_no_op_with_port(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_resolved_address resolved_addr;
  struct sockaddr_in* addr =
      reinterpret_cast<struct sockaddr_in*>(resolved_addr.addr);
  grpc_tcp_server* s;
  GPR_ASSERT(GRPC_ERROR_NONE == grpc_tcp_server_create(nullptr, nullptr, &s));
  LOG_TEST("test_no_op_with_port");

  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  int port = -1;
  GPR_ASSERT(grpc_tcp_server_add_port(s, &resolved_addr, &port) ==
                 GRPC_ERROR_NONE &&
             port > 0);

  grpc_tcp_server_unref(s);
}

static void test_no_op_with_port_and_start(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_resolved_address resolved_addr;
  struct sockaddr_in* addr =
      reinterpret_cast<struct sockaddr_in*>(resolved_addr.addr);
  grpc_tcp_server* s;
  GPR_ASSERT(GRPC_ERROR_NONE == grpc_tcp_server_create(nullptr, nullptr, &s));
  LOG_TEST("test_no_op_with_port_and_start");
  int port = -1;

  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  GPR_ASSERT(grpc_tcp_server_add_port(s, &resolved_addr, &port) ==
                 GRPC_ERROR_NONE &&
             port > 0);

  grpc_tcp_server_start(s, nullptr, 0, on_connect, nullptr);

  grpc_tcp_server_unref(s);
}

static grpc_error* tcp_connect(const test_addr* remote,
                               on_connect_result* result) {
  grpc_millis deadline =
      grpc_timespec_to_millis_round_up(grpc_timeout_seconds_to_deadline(10));
  int clifd;
  int nconnects_before;
  const struct sockaddr* remote_addr =
      reinterpret_cast<const struct sockaddr*>(remote->addr.addr);

  gpr_log(GPR_INFO, "Connecting to %s", remote->str);
  gpr_mu_lock(g_mu);
  nconnects_before = g_nconnects;
  on_connect_result_init(&g_result);
  clifd = socket(remote_addr->sa_family, SOCK_STREAM, 0);
  if (clifd < 0) {
    gpr_mu_unlock(g_mu);
    return GRPC_OS_ERROR(errno, "Failed to create socket");
  }
  gpr_log(GPR_DEBUG, "start connect to %s", remote->str);
  if (connect(clifd, remote_addr, static_cast<socklen_t>(remote->addr.len)) !=
      0) {
    gpr_mu_unlock(g_mu);
    close(clifd);
    return GRPC_OS_ERROR(errno, "connect");
  }
  gpr_log(GPR_DEBUG, "wait");
  while (g_nconnects == nconnects_before &&
         deadline > grpc_core::ExecCtx::Get()->Now()) {
    grpc_pollset_worker* worker = nullptr;
    grpc_error* err;
    if ((err = grpc_pollset_work(g_pollset, &worker, deadline)) !=
        GRPC_ERROR_NONE) {
      gpr_mu_unlock(g_mu);
      close(clifd);
      return err;
    }
    gpr_mu_unlock(g_mu);

    gpr_mu_lock(g_mu);
  }
  gpr_log(GPR_DEBUG, "wait done");
  if (g_nconnects != nconnects_before + 1) {
    gpr_mu_unlock(g_mu);
    close(clifd);
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Didn't connect");
  }
  close(clifd);
  *result = g_result;

  gpr_mu_unlock(g_mu);
  gpr_log(GPR_INFO, "Result (%d, %d) fd %d", result->port_index,
          result->fd_index, result->server_fd);
  grpc_tcp_server_unref(result->server);
  return GRPC_ERROR_NONE;
}

/* Tests a tcp server on "::" listeners with multiple ports. If channel_args is
   non-NULL, pass them to the server. If dst_addrs is non-NULL, use valid addrs
   as destination addrs (port is not set). If dst_addrs is NULL, use listener
   addrs as destination addrs. If test_dst_addrs is true, test connectivity with
   each destination address, set grpc_resolved_address::len=0 for failures, but
   don't fail the overall unitest. */
static void test_connect(size_t num_connects,
                         const grpc_channel_args* channel_args,
                         test_addrs* dst_addrs, bool test_dst_addrs) {
  grpc_core::ExecCtx exec_ctx;
  grpc_resolved_address resolved_addr;
  grpc_resolved_address resolved_addr1;
  struct sockaddr_storage* const addr =
      reinterpret_cast<struct sockaddr_storage*>(resolved_addr.addr);
  struct sockaddr_storage* const addr1 =
      reinterpret_cast<struct sockaddr_storage*>(resolved_addr1.addr);
  unsigned svr_fd_count;
  int port;
  int svr_port;
  unsigned svr1_fd_count;
  int svr1_port;
  grpc_tcp_server* s;
  const unsigned num_ports = 2;
  GPR_ASSERT(GRPC_ERROR_NONE ==
             grpc_tcp_server_create(nullptr, channel_args, &s));
  unsigned port_num;
  server_weak_ref weak_ref;
  server_weak_ref_init(&weak_ref);
  server_weak_ref_set(&weak_ref, s);
  LOG_TEST("test_connect");
  gpr_log(GPR_INFO,
          "clients=%lu, num chan args=%lu, remote IP=%s, test_dst_addrs=%d",
          static_cast<unsigned long>(num_connects),
          static_cast<unsigned long>(
              channel_args != nullptr ? channel_args->num_args : 0),
          dst_addrs != nullptr ? "<specific>" : "::", test_dst_addrs);
  memset(&resolved_addr, 0, sizeof(resolved_addr));
  memset(&resolved_addr1, 0, sizeof(resolved_addr1));
  resolved_addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
  resolved_addr1.len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
  addr->ss_family = addr1->ss_family = AF_INET;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "grpc_tcp_server_add_port",
      grpc_tcp_server_add_port(s, &resolved_addr, &svr_port)));
  gpr_log(GPR_INFO, "Allocated port %d", svr_port);
  GPR_ASSERT(svr_port > 0);
  /* Cannot use wildcard (port==0), because add_port() will try to reuse the
     same port as a previous add_port(). */
  svr1_port = grpc_pick_unused_port_or_die();
  GPR_ASSERT(svr1_port > 0);
  gpr_log(GPR_INFO, "Picked unused port %d", svr1_port);
  grpc_sockaddr_set_port(&resolved_addr1, svr1_port);
  GPR_ASSERT(grpc_tcp_server_add_port(s, &resolved_addr1, &port) ==
                 GRPC_ERROR_NONE &&
             port == svr1_port);

  /* Bad port_index. */
  GPR_ASSERT(grpc_tcp_server_port_fd_count(s, 2) == 0);
  GPR_ASSERT(grpc_tcp_server_port_fd(s, 2, 0) < 0);

  /* Bad fd_index. */
  GPR_ASSERT(grpc_tcp_server_port_fd(s, 0, 100) < 0);
  GPR_ASSERT(grpc_tcp_server_port_fd(s, 1, 100) < 0);

  /* Got at least one fd per port. */
  svr_fd_count = grpc_tcp_server_port_fd_count(s, 0);
  GPR_ASSERT(svr_fd_count >= 1);
  svr1_fd_count = grpc_tcp_server_port_fd_count(s, 1);
  GPR_ASSERT(svr1_fd_count >= 1);

  grpc_tcp_server_start(s, &g_pollset, 1, on_connect, nullptr);

  if (dst_addrs != nullptr) {
    int ports[] = {svr_port, svr1_port};
    for (port_num = 0; port_num < num_ports; ++port_num) {
      size_t dst_idx;
      size_t num_tested = 0;
      for (dst_idx = 0; dst_idx < dst_addrs->naddrs; ++dst_idx) {
        test_addr dst = dst_addrs->addrs[dst_idx];
        on_connect_result result;
        grpc_error* err;
        if (dst.addr.len == 0) {
          gpr_log(GPR_DEBUG, "Skipping test of non-functional local IP %s",
                  dst.str);
          continue;
        }
        GPR_ASSERT(grpc_sockaddr_set_port(&dst.addr, ports[port_num]));
        test_addr_init_str(&dst);
        ++num_tested;
        on_connect_result_init(&result);
        if ((err = tcp_connect(&dst, &result)) == GRPC_ERROR_NONE &&
            result.server_fd >= 0 && result.server == s) {
          continue;
        }
        gpr_log(GPR_ERROR, "Failed to connect to %s: %s", dst.str,
                grpc_error_string(err));
        GPR_ASSERT(test_dst_addrs);
        dst_addrs->addrs[dst_idx].addr.len = 0;
        GRPC_ERROR_UNREF(err);
      }
      GPR_ASSERT(num_tested > 0);
    }
  } else {
    for (port_num = 0; port_num < num_ports; ++port_num) {
      const unsigned num_fds = grpc_tcp_server_port_fd_count(s, port_num);
      unsigned fd_num;
      for (fd_num = 0; fd_num < num_fds; ++fd_num) {
        int fd = grpc_tcp_server_port_fd(s, port_num, fd_num);
        size_t connect_num;
        test_addr dst;
        GPR_ASSERT(fd >= 0);
        dst.addr.len = static_cast<socklen_t>(sizeof(dst.addr.addr));
        GPR_ASSERT(getsockname(fd, (struct sockaddr*)dst.addr.addr,
                               (socklen_t*)&dst.addr.len) == 0);
        GPR_ASSERT(dst.addr.len <= sizeof(dst.addr.addr));
        test_addr_init_str(&dst);
        gpr_log(GPR_INFO, "(%d, %d) fd %d family %s listening on %s", port_num,
                fd_num, fd, sock_family_name(addr->ss_family), dst.str);
        for (connect_num = 0; connect_num < num_connects; ++connect_num) {
          on_connect_result result;
          on_connect_result_init(&result);
          GPR_ASSERT(
              GRPC_LOG_IF_ERROR("tcp_connect", tcp_connect(&dst, &result)));
          GPR_ASSERT(result.server_fd == fd);
          GPR_ASSERT(result.port_index == port_num);
          GPR_ASSERT(result.fd_index == fd_num);
          GPR_ASSERT(result.server == s);
          GPR_ASSERT(
              grpc_tcp_server_port_fd(s, result.port_index, result.fd_index) ==
              result.server_fd);
        }
      }
    }
  }
  /* Weak ref to server valid until final unref. */
  GPR_ASSERT(weak_ref.server != nullptr);
  GPR_ASSERT(grpc_tcp_server_port_fd(s, 0, 0) >= 0);

  grpc_tcp_server_unref(s);
  grpc_core::ExecCtx::Get()->Flush();

  /* Weak ref lost. */
  GPR_ASSERT(weak_ref.server == nullptr);
}

static void destroy_pollset(void* p, grpc_error* error) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(p));
}

int main(int argc, char** argv) {
  grpc_closure destroyed;
  grpc_arg chan_args[1];
  chan_args[0].type = GRPC_ARG_INTEGER;
  chan_args[0].key = const_cast<char*>(GRPC_ARG_EXPAND_WILDCARD_ADDRS);
  chan_args[0].value.integer = 1;
  const grpc_channel_args channel_args = {1, chan_args};
  struct ifaddrs* ifa = nullptr;
  struct ifaddrs* ifa_it;
  // Zalloc dst_addrs to avoid oversized frames.
  test_addrs* dst_addrs =
      static_cast<test_addrs*>(gpr_zalloc(sizeof(*dst_addrs)));
  grpc_test_init(argc, argv);
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;
    g_pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(g_pollset, &g_mu);

    test_no_op();
    test_no_op_with_start();
    test_no_op_with_port();
    test_no_op_with_port_and_start();

    if (getifaddrs(&ifa) != 0 || ifa == nullptr) {
      gpr_log(GPR_ERROR, "getifaddrs: %s", strerror(errno));
      return EXIT_FAILURE;
    }
    dst_addrs->naddrs = 0;
    for (ifa_it = ifa; ifa_it != nullptr && dst_addrs->naddrs < MAX_ADDRS;
         ifa_it = ifa_it->ifa_next) {
      if (ifa_it->ifa_addr == nullptr) {
        continue;
      } else if (ifa_it->ifa_addr->sa_family == AF_INET) {
        dst_addrs->addrs[dst_addrs->naddrs].addr.len =
            static_cast<socklen_t>(sizeof(struct sockaddr_in));
      } else if (ifa_it->ifa_addr->sa_family == AF_INET6) {
        dst_addrs->addrs[dst_addrs->naddrs].addr.len =
            static_cast<socklen_t>(sizeof(struct sockaddr_in6));
      } else {
        continue;
      }
      memcpy(dst_addrs->addrs[dst_addrs->naddrs].addr.addr, ifa_it->ifa_addr,
             dst_addrs->addrs[dst_addrs->naddrs].addr.len);
      GPR_ASSERT(
          grpc_sockaddr_set_port(&dst_addrs->addrs[dst_addrs->naddrs].addr, 0));
      test_addr_init_str(&dst_addrs->addrs[dst_addrs->naddrs]);
      ++dst_addrs->naddrs;
    }
    freeifaddrs(ifa);
    ifa = nullptr;

    /* Connect to same addresses as listeners. */
    test_connect(1, nullptr, nullptr, false);
    test_connect(10, nullptr, nullptr, false);

    /* Set dst_addrs->addrs[i].len=0 for dst_addrs that are unreachable with a
       "::" listener. */
    test_connect(1, nullptr, dst_addrs, true);

    /* Test connect(2) with dst_addrs. */
    test_connect(1, &channel_args, dst_addrs, false);
    /* Test connect(2) with dst_addrs. */
    test_connect(10, &channel_args, dst_addrs, false);

    GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, g_pollset,
                      grpc_schedule_on_exec_ctx);
    grpc_pollset_shutdown(g_pollset, &destroyed);
  }
  grpc_shutdown();
  gpr_free(dst_addrs);
  gpr_free(g_pollset);
  return EXIT_SUCCESS;
}

#else /* GRPC_POSIX_SOCKET */

int main(int argc, char** argv) { return 1; }

#endif /* GRPC_POSIX_SOCKET */
