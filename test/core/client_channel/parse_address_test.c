/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/lib/iomgr/sockaddr.h"

#include <string.h>
#ifdef GRPC_HAVE_UNIX_SOCKET
#include <sys/un.h>
#endif

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "test/core/util/test_config.h"

#ifdef GRPC_HAVE_UNIX_SOCKET

static void test_parse_unix(const char *uri_text, const char *pathname) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_uri *uri = grpc_uri_parse(&exec_ctx, uri_text, 0);
  grpc_resolved_address addr;

  GPR_ASSERT(1 == parse_unix(uri, &addr));
  struct sockaddr_un *addr_un = (struct sockaddr_un *)addr.addr;
  GPR_ASSERT(AF_UNIX == addr_un->sun_family);
  GPR_ASSERT(0 == strcmp(addr_un->sun_path, pathname));

  grpc_uri_destroy(uri);
  grpc_exec_ctx_finish(&exec_ctx);
}

#else /* GRPC_HAVE_UNIX_SOCKET */

static void test_parse_unix(const char *uri_text, const char *pathname) {}

#endif /* GRPC_HAVE_UNIX_SOCKET */

static void test_parse_ipv4(const char *uri_text, const char *host,
                            unsigned short port) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_uri *uri = grpc_uri_parse(&exec_ctx, uri_text, 0);
  grpc_resolved_address addr;
  char ntop_buf[INET_ADDRSTRLEN];

  GPR_ASSERT(1 == parse_ipv4(uri, &addr));
  struct sockaddr_in *addr_in = (struct sockaddr_in *)addr.addr;
  GPR_ASSERT(AF_INET == addr_in->sin_family);
  GPR_ASSERT(NULL != grpc_inet_ntop(AF_INET, &addr_in->sin_addr, ntop_buf,
                                    sizeof(ntop_buf)));
  GPR_ASSERT(0 == strcmp(ntop_buf, host));
  GPR_ASSERT(ntohs(addr_in->sin_port) == port);

  grpc_uri_destroy(uri);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_parse_ipv6(const char *uri_text, const char *host,
                            unsigned short port, uint32_t scope_id) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_uri *uri = grpc_uri_parse(&exec_ctx, uri_text, 0);
  grpc_resolved_address addr;
  char ntop_buf[INET6_ADDRSTRLEN];

  GPR_ASSERT(1 == parse_ipv6(uri, &addr));
  struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)addr.addr;
  GPR_ASSERT(AF_INET6 == addr_in6->sin6_family);
  GPR_ASSERT(NULL != grpc_inet_ntop(AF_INET6, &addr_in6->sin6_addr, ntop_buf,
                                    sizeof(ntop_buf)));
  GPR_ASSERT(0 == strcmp(ntop_buf, host));
  GPR_ASSERT(ntohs(addr_in6->sin6_port) == port);
  GPR_ASSERT(addr_in6->sin6_scope_id == scope_id);

  grpc_uri_destroy(uri);
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  test_parse_unix("unix:/path/name", "/path/name");
  test_parse_ipv4("ipv4:192.0.2.1:12345", "192.0.2.1", 12345);
  test_parse_ipv6("ipv6:[2001:db8::1]:12345", "2001:db8::1", 12345, 0);
  test_parse_ipv6("ipv6:[2001:db8::1%252]:12345", "2001:db8::1", 12345, 2);
}
