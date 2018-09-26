/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"

#include <string.h>
#ifdef GRPC_HAVE_UNIX_SOCKET
#include <sys/un.h>
#endif

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "test/core/util/test_config.h"

#ifdef GRPC_HAVE_UNIX_SOCKET

static void test_grpc_parse_unix(const char* uri_text, const char* pathname) {
  grpc_core::ExecCtx exec_ctx;
  grpc_uri* uri = grpc_uri_parse(uri_text, 0);
  grpc_resolved_address addr;

  GPR_ASSERT(1 == grpc_parse_unix(uri, &addr));
  struct sockaddr_un* addr_un =
      reinterpret_cast<struct sockaddr_un*>(addr.addr);
  GPR_ASSERT(AF_UNIX == addr_un->sun_family);
  GPR_ASSERT(0 == strcmp(addr_un->sun_path, pathname));

  grpc_uri_destroy(uri);
}

#else /* GRPC_HAVE_UNIX_SOCKET */

static void test_grpc_parse_unix(const char* uri_text, const char* pathname) {}

#endif /* GRPC_HAVE_UNIX_SOCKET */

static void test_grpc_parse_ipv4(const char* uri_text, const char* host,
                                 unsigned short port) {
  grpc_core::ExecCtx exec_ctx;
  grpc_uri* uri = grpc_uri_parse(uri_text, 0);
  grpc_resolved_address addr;
  char ntop_buf[GRPC_INET_ADDRSTRLEN];

  GPR_ASSERT(1 == grpc_parse_ipv4(uri, &addr));
  grpc_sockaddr_in* addr_in = reinterpret_cast<grpc_sockaddr_in*>(addr.addr);
  GPR_ASSERT(GRPC_AF_INET == addr_in->sin_family);
  GPR_ASSERT(nullptr != grpc_inet_ntop(GRPC_AF_INET, &addr_in->sin_addr,
                                       ntop_buf, sizeof(ntop_buf)));
  GPR_ASSERT(0 == strcmp(ntop_buf, host));
  GPR_ASSERT(grpc_ntohs(addr_in->sin_port) == port);

  grpc_uri_destroy(uri);
}

static void test_grpc_parse_ipv6(const char* uri_text, const char* host,
                                 unsigned short port, uint32_t scope_id) {
  grpc_core::ExecCtx exec_ctx;
  grpc_uri* uri = grpc_uri_parse(uri_text, 0);
  grpc_resolved_address addr;
  char ntop_buf[GRPC_INET6_ADDRSTRLEN];

  GPR_ASSERT(1 == grpc_parse_ipv6(uri, &addr));
  grpc_sockaddr_in6* addr_in6 = reinterpret_cast<grpc_sockaddr_in6*>(addr.addr);
  GPR_ASSERT(GRPC_AF_INET6 == addr_in6->sin6_family);
  GPR_ASSERT(nullptr != grpc_inet_ntop(GRPC_AF_INET6, &addr_in6->sin6_addr,
                                       ntop_buf, sizeof(ntop_buf)));
  GPR_ASSERT(0 == strcmp(ntop_buf, host));
  GPR_ASSERT(grpc_ntohs(addr_in6->sin6_port) == port);
  GPR_ASSERT(addr_in6->sin6_scope_id == scope_id);

  grpc_uri_destroy(uri);
}

/* Test parsing invalid ipv6 addresses (valid uri_text but invalid ipv6 addr) */
static void test_grpc_parse_ipv6_invalid(const char* uri_text) {
  grpc_core::ExecCtx exec_ctx;
  grpc_uri* uri = grpc_uri_parse(uri_text, 0);
  grpc_resolved_address addr;
  GPR_ASSERT(!grpc_parse_ipv6(uri, &addr));
  grpc_uri_destroy(uri);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  test_grpc_parse_unix("unix:/path/name", "/path/name");
  test_grpc_parse_ipv4("ipv4:192.0.2.1:12345", "192.0.2.1", 12345);
  test_grpc_parse_ipv6("ipv6:[2001:db8::1]:12345", "2001:db8::1", 12345, 0);
  test_grpc_parse_ipv6("ipv6:[2001:db8::1%252]:12345", "2001:db8::1", 12345, 2);

  /* Address length greater than GRPC_INET6_ADDRSTRLEN */
  test_grpc_parse_ipv6_invalid(
      "ipv6:WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW45%"
      "v6:45%x$1*");

  grpc_shutdown();
}
