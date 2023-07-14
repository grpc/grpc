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

#include <gtest/gtest.h>

#include "src/core/lib/event_engine/shim.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/port.h"
#include "test/core/util/test_config.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET_TCP_SERVER

#include <errno.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef GRPC_HAVE_UNIX_SOCKET
#include <sys/un.h>
#endif

#include <string>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/strerror.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/resource_quota/api.h"
#include "test/core/util/port.h"

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", #x)

static gpr_mu* g_mu;
static grpc_pollset* g_pollset;
static int g_nconnects = 0;

typedef struct {
  // Owns a ref to server.
  grpc_tcp_server* server;
  unsigned port_index;
  unsigned fd_index;
  int server_fd;
} on_connect_result;

typedef struct {
  grpc_tcp_server* server;

  // arg is this server_weak_ref.
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

static void server_weak_ref_shutdown(void* arg, grpc_error_handle /*error*/) {
  server_weak_ref* weak_ref = static_cast<server_weak_ref*>(arg);
  weak_ref->server = nullptr;
}

static void server_weak_ref_init(server_weak_ref* weak_ref) {
  weak_ref->server = nullptr;
  GRPC_CLOSURE_INIT(&weak_ref->server_shutdown, server_weak_ref_shutdown,
                    weak_ref, grpc_schedule_on_exec_ctx);
}

// Make weak_ref->server_shutdown a shutdown_starting cb on server.
// grpc_tcp_server promises that the server object will live until
// weak_ref->server_shutdown has returned. A strong ref on grpc_tcp_server
// should be held until server_weak_ref_set() returns to avoid a race where the
// server is deleted before the shutdown_starting cb is added.
static void server_weak_ref_set(server_weak_ref* weak_ref,
                                grpc_tcp_server* server) {
  grpc_tcp_server_shutdown_starting_add(server, &weak_ref->server_shutdown);
  weak_ref->server = server;
}

static void test_addr_init_str(test_addr* addr) {
  std::string str = grpc_sockaddr_to_string(&addr->addr, false).value();
  size_t str_len = std::min(str.size(), sizeof(addr->str) - 1);
  memcpy(addr->str, str.c_str(), str_len);
  addr->str[str_len] = '\0';
}

static void on_connect(void* /*arg*/, grpc_endpoint* tcp,
                       grpc_pollset* /*pollset*/,
                       grpc_tcp_server_acceptor* acceptor) {
  grpc_endpoint_shutdown(tcp, GRPC_ERROR_CREATE("Connected"));
  grpc_endpoint_destroy(tcp);

  on_connect_result temp_result;
  on_connect_result_set(&temp_result, acceptor);
  gpr_free(acceptor);

  gpr_mu_lock(g_mu);
  g_result = temp_result;
  g_nconnects++;
  ASSERT_TRUE(
      GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(g_pollset, nullptr)));
  gpr_mu_unlock(g_mu);
}

static void test_no_op(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_tcp_server* s;
  auto args = grpc_core::CoreConfiguration::Get()
                  .channel_args_preconditioning()
                  .PreconditionChannelArgs(nullptr);
  ASSERT_EQ(
      absl::OkStatus(),
      grpc_tcp_server_create(
          nullptr,
          grpc_event_engine::experimental::ChannelArgsEndpointConfig(args),
          on_connect, nullptr, &s));
  grpc_tcp_server_unref(s);
}

static void test_no_op_with_start(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_tcp_server* s;
  auto args = grpc_core::CoreConfiguration::Get()
                  .channel_args_preconditioning()
                  .PreconditionChannelArgs(nullptr);
  ASSERT_EQ(
      absl::OkStatus(),
      grpc_tcp_server_create(
          nullptr,
          grpc_event_engine::experimental::ChannelArgsEndpointConfig(args),
          on_connect, nullptr, &s));
  LOG_TEST("test_no_op_with_start");
  std::vector<grpc_pollset*> empty_pollset;
  grpc_tcp_server_start(s, &empty_pollset);
  grpc_tcp_server_unref(s);
}

static void test_no_op_with_port(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_resolved_address resolved_addr;
  struct sockaddr_in* addr =
      reinterpret_cast<struct sockaddr_in*>(resolved_addr.addr);
  grpc_tcp_server* s;
  auto args = grpc_core::CoreConfiguration::Get()
                  .channel_args_preconditioning()
                  .PreconditionChannelArgs(nullptr);
  ASSERT_EQ(
      absl::OkStatus(),
      grpc_tcp_server_create(
          nullptr,
          grpc_event_engine::experimental::ChannelArgsEndpointConfig(args),
          on_connect, nullptr, &s));
  LOG_TEST("test_no_op_with_port");

  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  int port = -1;
  ASSERT_EQ(grpc_tcp_server_add_port(s, &resolved_addr, &port),
            absl::OkStatus());
  ASSERT_GT(port, 0);

  grpc_tcp_server_unref(s);
}

static void test_no_op_with_port_and_start(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_resolved_address resolved_addr;
  struct sockaddr_in* addr =
      reinterpret_cast<struct sockaddr_in*>(resolved_addr.addr);
  grpc_tcp_server* s;
  auto args = grpc_core::CoreConfiguration::Get()
                  .channel_args_preconditioning()
                  .PreconditionChannelArgs(nullptr);
  ASSERT_EQ(
      absl::OkStatus(),
      grpc_tcp_server_create(
          nullptr,
          grpc_event_engine::experimental::ChannelArgsEndpointConfig(args),
          on_connect, nullptr, &s));
  LOG_TEST("test_no_op_with_port_and_start");
  int port = -1;

  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  ASSERT_EQ(grpc_tcp_server_add_port(s, &resolved_addr, &port),
            absl::OkStatus());
  ASSERT_GT(port, 0);

  std::vector<grpc_pollset*> empty_pollset;
  grpc_tcp_server_start(s, &empty_pollset);

  grpc_tcp_server_unref(s);
}

static grpc_error_handle tcp_connect(const test_addr* remote,
                                     on_connect_result* result) {
  grpc_core::Timestamp deadline = grpc_core::Timestamp::FromTimespecRoundUp(
      grpc_timeout_seconds_to_deadline(10));
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
         deadline > grpc_core::Timestamp::Now()) {
    grpc_pollset_worker* worker = nullptr;
    grpc_error_handle err;
    if ((err = grpc_pollset_work(g_pollset, &worker, deadline)) !=
        absl::OkStatus()) {
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
    return GRPC_ERROR_CREATE("Didn't connect");
  }
  close(clifd);
  *result = g_result;

  gpr_mu_unlock(g_mu);
  gpr_log(GPR_INFO, "Result (%d, %d) fd %d", result->port_index,
          result->fd_index, result->server_fd);
  grpc_tcp_server_unref(result->server);
  return absl::OkStatus();
}

// Tests a tcp server on "::" listeners with multiple ports. If channel_args is
// non-NULL, pass them to the server. If dst_addrs is non-NULL, use valid addrs
// as destination addrs (port is not set). If dst_addrs is NULL, use listener
// addrs as destination addrs. If test_dst_addrs is true, test connectivity with
// each destination address, set grpc_resolved_address::len=0 for failures, but
// don't fail the overall unitest.
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
  auto new_channel_args = grpc_core::CoreConfiguration::Get()
                              .channel_args_preconditioning()
                              .PreconditionChannelArgs(channel_args);
  ASSERT_EQ(absl::OkStatus(),
            grpc_tcp_server_create(
                nullptr,
                grpc_event_engine::experimental::ChannelArgsEndpointConfig(
                    new_channel_args),
                on_connect, nullptr, &s));
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
  ASSERT_TRUE(GRPC_LOG_IF_ERROR(
      "grpc_tcp_server_add_port",
      grpc_tcp_server_add_port(s, &resolved_addr, &svr_port)));
  gpr_log(GPR_INFO, "Allocated port %d", svr_port);
  ASSERT_GT(svr_port, 0);
  // Cannot use wildcard (port==0), because add_port() will try to reuse the
  // same port as a previous add_port().
  svr1_port = grpc_pick_unused_port_or_die();
  ASSERT_GT(svr1_port, 0);
  gpr_log(GPR_INFO, "Picked unused port %d", svr1_port);
  grpc_sockaddr_set_port(&resolved_addr1, svr1_port);
  ASSERT_EQ(grpc_tcp_server_add_port(s, &resolved_addr1, &port),
            absl::OkStatus());
  ASSERT_EQ(port, svr1_port);

  // Bad port_index.
  ASSERT_EQ(grpc_tcp_server_port_fd_count(s, 2), 0);
  ASSERT_LT(grpc_tcp_server_port_fd(s, 2, 0), 0);

  // Bad fd_index.
  ASSERT_LT(grpc_tcp_server_port_fd(s, 0, 100), 0);
  ASSERT_LT(grpc_tcp_server_port_fd(s, 1, 100), 0);

  // Got at least one fd per port.
  svr_fd_count = grpc_tcp_server_port_fd_count(s, 0);
  ASSERT_GE(svr_fd_count, 1);
  svr1_fd_count = grpc_tcp_server_port_fd_count(s, 1);
  ASSERT_GE(svr1_fd_count, 1);

  std::vector<grpc_pollset*> test_pollset;
  test_pollset.push_back(g_pollset);
  grpc_tcp_server_start(s, &test_pollset);

  if (dst_addrs != nullptr) {
    int ports[] = {svr_port, svr1_port};
    for (port_num = 0; port_num < num_ports; ++port_num) {
      size_t dst_idx;
      size_t num_tested = 0;
      for (dst_idx = 0; dst_idx < dst_addrs->naddrs; ++dst_idx) {
        test_addr dst = dst_addrs->addrs[dst_idx];
        on_connect_result result;
        grpc_error_handle err;
        if (dst.addr.len == 0) {
          gpr_log(GPR_DEBUG, "Skipping test of non-functional local IP %s",
                  dst.str);
          continue;
        }
        ASSERT_TRUE(grpc_sockaddr_set_port(&dst.addr, ports[port_num]));
        test_addr_init_str(&dst);
        ++num_tested;
        on_connect_result_init(&result);
        if ((err = tcp_connect(&dst, &result)) == absl::OkStatus() &&
            result.server_fd >= 0 && result.server == s) {
          continue;
        }
        gpr_log(GPR_ERROR, "Failed to connect to %s: %s", dst.str,
                grpc_core::StatusToString(err).c_str());
        ASSERT_TRUE(test_dst_addrs);
        dst_addrs->addrs[dst_idx].addr.len = 0;
      }
      ASSERT_GT(num_tested, 0);
    }
  } else {
    for (port_num = 0; port_num < num_ports; ++port_num) {
      const unsigned num_fds = grpc_tcp_server_port_fd_count(s, port_num);
      unsigned fd_num;
      for (fd_num = 0; fd_num < num_fds; ++fd_num) {
        int fd = grpc_tcp_server_port_fd(s, port_num, fd_num);
        size_t connect_num;
        test_addr dst;
        ASSERT_GE(fd, 0);
        dst.addr.len = static_cast<socklen_t>(sizeof(dst.addr.addr));
        ASSERT_EQ(getsockname(fd, (struct sockaddr*)dst.addr.addr,
                              (socklen_t*)&dst.addr.len),
                  0);
        ASSERT_LE(dst.addr.len, sizeof(dst.addr.addr));
        test_addr_init_str(&dst);
        gpr_log(GPR_INFO, "(%d, %d) fd %d family %s listening on %s", port_num,
                fd_num, fd, sock_family_name(addr->ss_family), dst.str);
        for (connect_num = 0; connect_num < num_connects; ++connect_num) {
          on_connect_result result;
          on_connect_result_init(&result);
          ASSERT_TRUE(
              GRPC_LOG_IF_ERROR("tcp_connect", tcp_connect(&dst, &result)));
          ASSERT_EQ(result.server_fd, fd);
          ASSERT_EQ(result.port_index, port_num);
          ASSERT_EQ(result.fd_index, fd_num);
          ASSERT_EQ(result.server, s);
          ASSERT_EQ(
              grpc_tcp_server_port_fd(s, result.port_index, result.fd_index),
              result.server_fd);
        }
      }
    }
  }
  // Weak ref to server valid until final unref.
  ASSERT_NE(weak_ref.server, nullptr);
  ASSERT_GE(grpc_tcp_server_port_fd(s, 0, 0), 0);

  grpc_tcp_server_unref(s);
  grpc_core::ExecCtx::Get()->Flush();

  // Weak ref lost.
  ASSERT_EQ(weak_ref.server, nullptr);
}

static int pre_allocate_inet_sock(grpc_tcp_server* s, int family, int port,
                                  int* fd) {
  struct sockaddr_in6 address;
  memset(&address, 0, sizeof(address));
  address.sin6_family = family;
  address.sin6_port = htons(port);

  int pre_fd = socket(address.sin6_family, SOCK_STREAM, 0);
  if (pre_fd < 0) {
    gpr_log(GPR_ERROR, "Unable to create inet socket: %m");
    return -1;
  }

  const int enable = 1;
  setsockopt(pre_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

  int b = bind(pre_fd, reinterpret_cast<struct sockaddr*>(&address),
               sizeof(address));
  if (b < 0) {
    gpr_log(GPR_ERROR, "Unable to bind inet socket: %m");
    return -1;
  }

  int l = listen(pre_fd, SOMAXCONN);
  if (l < 0) {
    gpr_log(GPR_ERROR, "Unable to listen on inet socket: %m");
    return -1;
  }

  grpc_tcp_server_set_pre_allocated_fd(s, pre_fd);
  *fd = pre_fd;

  return 0;
}

static void test_pre_allocated_inet_fd() {
  grpc_core::ExecCtx exec_ctx;
  grpc_resolved_address resolved_addr;
  struct sockaddr_in6* addr =
      reinterpret_cast<struct sockaddr_in6*>(resolved_addr.addr);
  grpc_tcp_server* s;
  if (grpc_event_engine::experimental::UseEventEngineListener()) {
    // TODO(vigneshbabu): Skip the test when event engine is enabled.
    // Pre-allocated fd support will be added to event engine later.
    return;
  }
  auto args = grpc_core::CoreConfiguration::Get()
                  .channel_args_preconditioning()
                  .PreconditionChannelArgs(nullptr);
  ASSERT_EQ(
      absl::OkStatus(),
      grpc_tcp_server_create(
          nullptr,
          grpc_event_engine::experimental::ChannelArgsEndpointConfig(args),
          on_connect, nullptr, &s));
  LOG_TEST("test_pre_allocated_inet_fd");

  // Pre allocate FD
  int pre_fd;
  int port = grpc_pick_unused_port_or_die();

  int res_pre = pre_allocate_inet_sock(s, AF_INET6, port, &pre_fd);
  if (res_pre < 0) {
    grpc_tcp_server_unref(s);
    close(pre_fd);
    return;
  }
  ASSERT_EQ(grpc_tcp_server_pre_allocated_fd(s), pre_fd);

  // Add port
  int pt;
  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_in));
  addr->sin6_family = AF_INET6;
  addr->sin6_port = htons(port);
  ASSERT_EQ(grpc_tcp_server_add_port(s, &resolved_addr, &pt), absl::OkStatus());
  ASSERT_GE(grpc_tcp_server_port_fd_count(s, 0), 1);
  ASSERT_EQ(grpc_tcp_server_port_fd(s, 0, 0), pre_fd);

  // Start server
  std::vector<grpc_pollset*> test_pollset;
  test_pollset.push_back(g_pollset);
  grpc_tcp_server_start(s, &test_pollset);

  // Test connection
  test_addr dst;
  dst.addr.len = static_cast<socklen_t>(sizeof(dst.addr.addr));
  ASSERT_EQ(getsockname(pre_fd, (struct sockaddr*)dst.addr.addr,
                        (socklen_t*)&dst.addr.len),
            0);
  ASSERT_LE(dst.addr.len, sizeof(dst.addr.addr));
  test_addr_init_str(&dst);
  on_connect_result result;
  on_connect_result_init(&result);
  ASSERT_EQ(tcp_connect(&dst, &result), absl::OkStatus());
  ASSERT_EQ(result.server_fd, pre_fd);
  ASSERT_EQ(result.server, s);
  ASSERT_EQ(grpc_tcp_server_port_fd(s, result.port_index, result.fd_index),
            result.server_fd);

  grpc_tcp_server_unref(s);
  close(pre_fd);
}

#ifdef GRPC_HAVE_UNIX_SOCKET
static int pre_allocate_unix_sock(grpc_tcp_server* s, const char* path,
                                  int* fd) {
  struct sockaddr_un address;
  memset(&address, 0, sizeof(struct sockaddr_un));
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, path);

  int pre_fd = socket(address.sun_family, SOCK_STREAM, 0);
  if (pre_fd < 0) {
    gpr_log(GPR_ERROR, "Unable to create unix socket: %m");
    return -1;
  }

  int b = bind(pre_fd, reinterpret_cast<struct sockaddr*>(&address),
               sizeof(address));
  if (b < 0) {
    gpr_log(GPR_ERROR, "Unable to bind unix socket: %m");
    return -1;
  }

  int l = listen(pre_fd, SOMAXCONN);
  if (l < 0) {
    gpr_log(GPR_ERROR, "Unable to listen on unix socket: %m");
    return -1;
  }

  grpc_tcp_server_set_pre_allocated_fd(s, pre_fd);
  *fd = pre_fd;

  return 0;
}

static void test_pre_allocated_unix_fd() {
  grpc_core::ExecCtx exec_ctx;
  grpc_resolved_address resolved_addr;
  struct sockaddr_un* addr =
      reinterpret_cast<struct sockaddr_un*>(resolved_addr.addr);
  grpc_tcp_server* s;
  if (grpc_event_engine::experimental::UseEventEngineListener()) {
    // TODO(vigneshbabu): Skip the test when event engine is enabled.
    // Pre-allocated fd support will be added to event engine later.
    return;
  }
  auto args = grpc_core::CoreConfiguration::Get()
                  .channel_args_preconditioning()
                  .PreconditionChannelArgs(nullptr);
  ASSERT_EQ(
      absl::OkStatus(),
      grpc_tcp_server_create(
          nullptr,
          grpc_event_engine::experimental::ChannelArgsEndpointConfig(args),
          on_connect, nullptr, &s));
  LOG_TEST("test_pre_allocated_unix_fd");

  // Pre allocate FD
  int pre_fd;
  char path[100];
  srand(time(nullptr));
  memset(path, 0, sizeof(path));
  sprintf(path, "/tmp/pre_fd_test_%d", rand());

  int res_pre = pre_allocate_unix_sock(s, path, &pre_fd);
  if (res_pre < 0) {
    grpc_tcp_server_unref(s);
    close(pre_fd);
    unlink(path);
    return;
  }

  ASSERT_EQ(grpc_tcp_server_pre_allocated_fd(s), pre_fd);

  // Add port
  int pt;
  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_un));
  addr->sun_family = AF_UNIX;
  strcpy(addr->sun_path, path);
  ASSERT_EQ(grpc_tcp_server_add_port(s, &resolved_addr, &pt), absl::OkStatus());
  ASSERT_GE(grpc_tcp_server_port_fd_count(s, 0), 1);
  ASSERT_EQ(grpc_tcp_server_port_fd(s, 0, 0), pre_fd);

  // Start server
  std::vector<grpc_pollset*> test_pollset;
  test_pollset.push_back(g_pollset);
  grpc_tcp_server_start(s, &test_pollset);

  // Test connection
  test_addr dst;
  dst.addr.len = static_cast<socklen_t>(sizeof(dst.addr.addr));
  ASSERT_EQ(getsockname(pre_fd, (struct sockaddr*)dst.addr.addr,
                        (socklen_t*)&dst.addr.len),
            0);
  ASSERT_LE(dst.addr.len, sizeof(dst.addr.addr));
  test_addr_init_str(&dst);
  on_connect_result result;
  on_connect_result_init(&result);

  grpc_error_handle res_conn = tcp_connect(&dst, &result);
  // If the path no longer exists, errno is 2. This can happen when
  // runninig the test multiple times in parallel. Do not fail the test
  if (absl::IsUnknown(res_conn) && res_conn.raw_code() == 2) {
    gpr_log(GPR_ERROR,
            "Unable to test pre_allocated unix socket: path does not exist");
    grpc_tcp_server_unref(s);
    close(pre_fd);
    return;
  }

  ASSERT_EQ(res_conn, absl::OkStatus());
  ASSERT_EQ(result.server_fd, pre_fd);
  ASSERT_EQ(result.server, s);
  ASSERT_EQ(grpc_tcp_server_port_fd(s, result.port_index, result.fd_index),
            result.server_fd);

  grpc_tcp_server_unref(s);
  close(pre_fd);
  unlink(path);
}
#endif  // GRPC_HAVE_UNIX_SOCKET

static void destroy_pollset(void* p, grpc_error_handle /*error*/) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(p));
}

TEST(TcpServerPosixTest, MainTest) {
  grpc_closure destroyed;
  grpc_arg chan_args[1];
  chan_args[0].type = GRPC_ARG_INTEGER;
  chan_args[0].key = const_cast<char*>(GRPC_ARG_EXPAND_WILDCARD_ADDRS);
  chan_args[0].value.integer = 1;
  const grpc_channel_args channel_args = {1, chan_args};
  struct ifaddrs* ifa = nullptr;
  struct ifaddrs* ifa_it;
  // Zalloc dst_addrs to avoid oversized frames.
  test_addrs* dst_addrs = grpc_core::Zalloc<test_addrs>();
  grpc_init();
  // wait a few seconds to make sure IPv6 link-local addresses can be bound
  // if we are running under docker container that has just started.
  // See https://github.com/moby/moby/issues/38491
  // See https://github.com/grpc/grpc/issues/15610
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(4));
  {
    grpc_core::ExecCtx exec_ctx;
    g_pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(g_pollset, &g_mu);

    test_no_op();
    test_no_op_with_start();
    test_no_op_with_port();
    test_no_op_with_port_and_start();
    test_pre_allocated_inet_fd();
#ifdef GRPC_HAVE_UNIX_SOCKET
    test_pre_allocated_unix_fd();
#endif

    if (getifaddrs(&ifa) != 0 || ifa == nullptr) {
      FAIL() << "getifaddrs: " << grpc_core::StrError(errno);
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
      ASSERT_TRUE(
          grpc_sockaddr_set_port(&dst_addrs->addrs[dst_addrs->naddrs].addr, 0));
      test_addr_init_str(&dst_addrs->addrs[dst_addrs->naddrs]);
      ++dst_addrs->naddrs;
    }
    freeifaddrs(ifa);
    ifa = nullptr;

    // Connect to same addresses as listeners.
    test_connect(1, nullptr, nullptr, false);
    test_connect(10, nullptr, nullptr, false);

    // Set dst_addrs->addrs[i].len=0 for dst_addrs that are unreachable with a
    // "::" listener.
    test_connect(1, nullptr, dst_addrs, true);

    // Test connect(2) with dst_addrs.
    test_connect(1, &channel_args, dst_addrs, false);
    // Test connect(2) with dst_addrs.
    test_connect(10, &channel_args, dst_addrs, false);

    GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, g_pollset,
                      grpc_schedule_on_exec_ctx);
    grpc_pollset_shutdown(g_pollset, &destroyed);
  }
  grpc_shutdown();
  gpr_free(dst_addrs);
  gpr_free(g_pollset);
}

#endif  // GRPC_POSIX_SOCKET_SERVER

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
