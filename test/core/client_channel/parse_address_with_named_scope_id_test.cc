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

#include <net/if.h>
#include <string.h>
#ifdef GRPC_HAVE_UNIX_SOCKET
#include <sys/un.h>
#endif

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "test/core/util/test_config.h"

static void test_grpc_parse_ipv6_parity_with_getaddrinfo(
    const char* target, const struct sockaddr_in6 result_from_getaddrinfo) {
  // Get the sockaddr that gRPC's ipv6 resolver resolves this too.
  grpc_core::ExecCtx exec_ctx;
  grpc_uri* uri = grpc_uri_parse(target, 0);
  grpc_resolved_address addr;
  GPR_ASSERT(1 == grpc_parse_ipv6(uri, &addr));
  grpc_sockaddr_in6* result_from_grpc_parser =
      reinterpret_cast<grpc_sockaddr_in6*>(addr.addr);
  // Compare the sockaddr returned from gRPC's ipv6 resolver with that returned
  // from getaddrinfo.
  GPR_ASSERT(result_from_grpc_parser->sin6_family == AF_INET6);
  GPR_ASSERT(result_from_getaddrinfo.sin6_family == AF_INET6);
  GPR_ASSERT(memcmp(&result_from_grpc_parser->sin6_addr,
                    &result_from_getaddrinfo.sin6_addr, sizeof(in6_addr)) == 0);
  GPR_ASSERT(result_from_grpc_parser->sin6_scope_id ==
             result_from_getaddrinfo.sin6_scope_id);
  GPR_ASSERT(result_from_grpc_parser->sin6_scope_id != 0);
  // TODO: compare sin6_flow_info fields? parse_ipv6 zero's this field as is.
  // Cleanup
  grpc_uri_destroy(uri);
}

struct sockaddr_in6 resolve_with_gettaddrinfo(const char* uri_text) {
  grpc_uri* uri = grpc_uri_parse(uri_text, 0);
  grpc_core::UniquePtr<char> host;
  grpc_core::UniquePtr<char> port;
  grpc_core::SplitHostPort(uri->path, &host, &port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_NUMERICHOST;
  struct addrinfo* result;
  int res = getaddrinfo(host.get(), port.get(), &hints, &result);
  if (res != 0) {
    gpr_log(GPR_ERROR,
            "getaddrinfo failed to resolve host:%s port:%s. Error: %d.",
            host.get(), port.get(), res);
    abort();
  }
  size_t num_addrs_from_getaddrinfo = 0;
  for (struct addrinfo* resp = result; resp != nullptr; resp = resp->ai_next) {
    num_addrs_from_getaddrinfo++;
  }
  GPR_ASSERT(num_addrs_from_getaddrinfo == 1);
  GPR_ASSERT(result->ai_family == AF_INET6);
  struct sockaddr_in6 out =
      *reinterpret_cast<struct sockaddr_in6*>(result->ai_addr);
  // Cleanup
  freeaddrinfo(result);
  grpc_uri_destroy(uri);
  return out;
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  char* arbitrary_interface_name = static_cast<char*>(gpr_zalloc(IF_NAMESIZE));
  // Per RFC 3493, an interface index is a "small positive integer starts at 1".
  // Probe candidate interface index numbers until we find one that the
  // system recognizes, and then use that for the test.
  for (size_t i = 1; i < 65536; i++) {
    if (if_indextoname(i, arbitrary_interface_name) != nullptr) {
      gpr_log(
          GPR_DEBUG,
          "Found interface at index %d named %s. Will use this for the test",
          (int)i, arbitrary_interface_name);
      break;
    }
  }
  GPR_ASSERT(strlen(arbitrary_interface_name) > 0);
  char* target = nullptr;
  gpr_asprintf(&target, "ipv6:[fe80::1234%%%s]:12345",
               arbitrary_interface_name);
  struct sockaddr_in6 result_from_getaddrinfo =
      resolve_with_gettaddrinfo(target);
  // Run the test
  gpr_log(GPR_DEBUG,
          "Run test_grpc_parse_ipv6_parity_with_getaddrinfo with target: %s",
          target);
  test_grpc_parse_ipv6_parity_with_getaddrinfo(target, result_from_getaddrinfo);
  // Cleanup
  gpr_free(target);
  gpr_free(arbitrary_interface_name);
  grpc_shutdown();
}
