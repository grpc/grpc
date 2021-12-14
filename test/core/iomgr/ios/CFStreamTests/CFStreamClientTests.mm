/*
 *
 * Copyright 2018 gRPC authors.
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

#import <XCTest/XCTest.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_CFSTREAM

#include <arpa/inet.h>
#include <netinet/in.h>

#include <grpc/grpc.h>
#include <grpc/impl/codegen/sync.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/resource_quota/api.h"
#include "test/core/util/test_config.h"

const uint8_t kIPv4[] = {127, 0, 0, 1};
const uint8_t kIPv6Mapped[] = {0, 0, 0,    0,    0,   0, 0, 0,
                           0, 0, 0xff, 0xff, 127, 0, 0, 1};

static gpr_mu g_mu;
static int g_connections_complete = 0;
static grpc_endpoint* g_connecting = nullptr;

static void finish_connection() {
  gpr_mu_lock(&g_mu);
  g_connections_complete++;
  gpr_mu_unlock(&g_mu);
}

static void must_succeed(void* arg, grpc_error_handle error) {
  GPR_ASSERT(g_connecting != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_endpoint_shutdown(g_connecting, GRPC_ERROR_CREATE_FROM_STATIC_STRING("must_succeed called"));
  grpc_endpoint_destroy(g_connecting);
  g_connecting = nullptr;
  finish_connection();
  NSLog(@"XXX: must_succeed closure");
}

static void must_fail(void* arg, grpc_error_handle error) {
  GPR_ASSERT(g_connecting == nullptr);
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  NSLog(@"%s", grpc_error_std_string(error).c_str());
  finish_connection();
}

static grpc_resolved_address MakeAddr4(const uint8_t* addr, size_t addr_len, in_port_t port) {
  grpc_resolved_address resolved_addr4;
  sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(resolved_addr4.addr);
  memset(&resolved_addr4, 0, sizeof(resolved_addr4));
  addr4->sin_family = AF_INET;
  addr4->sin_port = port;
  GPR_ASSERT(addr_len == sizeof(addr4->sin_addr.s_addr));
  memcpy(&addr4->sin_addr.s_addr, addr, addr_len);
  resolved_addr4.len = static_cast<socklen_t>(sizeof(sockaddr_in));
  return resolved_addr4;
}

static sockaddr_in6 MakeAddr6(const uint8_t* addr, size_t addr_len) {
  sockaddr_in6 addr6;
  memset(&addr6, 0, sizeof(addr6));
  addr6.sin6_family = AF_INET6;
  GPR_ASSERT(addr_len == sizeof(addr6.sin6_addr.s6_addr));
  memcpy(&addr6.sin6_addr.s6_addr, addr, addr_len);
  return addr6;
}

@interface CFStreamClientTests : XCTestCase

@end

@implementation CFStreamClientTests

+ (void)setUp {
  grpc_init();
  gpr_mu_init(&g_mu);
}

+ (void)tearDown {
  grpc_shutdown();
}

- (void)testSucceeds {
  int svr_fd;
  int r;
  int connections_complete_before;
  grpc_closure done;
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_DEBUG, "test_succeeds");

  /* create a dual-stack phony server accepting both ipv4 and ipv6 clients. */
  svr_fd = socket(AF_INET6, SOCK_STREAM, 0);
  const int off = 0;
  setsockopt(svr_fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));

  /* bind and listen. */
  struct sockaddr_in6 server_addr = MakeAddr6(kIPv6Mapped, sizeof(kIPv6Mapped));
  socklen_t socklen = static_cast<socklen_t>(sizeof(sockaddr_in6));
  GPR_ASSERT(svr_fd >= 0);
  const int bind_ret = bind(svr_fd, (struct sockaddr*)&server_addr, socklen);
  if (bind_ret != 0) {
    NSLog(@"bind result: %@ with error %@", @(bind_ret), @(errno));
  }
  GPR_ASSERT(0 == bind_ret);
  GPR_ASSERT(0 == listen(svr_fd, 1));
  GPR_ASSERT(getsockname(svr_fd, (struct sockaddr*)&server_addr, &socklen) == 0);

  gpr_mu_lock(&g_mu);
  connections_complete_before = g_connections_complete;
  gpr_mu_unlock(&g_mu);

  /* connect to it */
  grpc_resolved_address client_addr = MakeAddr4(kIPv4, sizeof(kIPv4), server_addr.sin6_port);
  GRPC_CLOSURE_INIT(&done, must_succeed, nullptr, grpc_schedule_on_exec_ctx);
  const grpc_channel_args* args =
      grpc_core::CoreConfiguration::Get().channel_args_preconditioning().PreconditionChannelArgs(
          nullptr);
  grpc_tcp_client_connect(&done, &g_connecting, nullptr, args, &client_addr,
                          GRPC_MILLIS_INF_FUTURE);
  grpc_channel_args_destroy(args);

  /* await the connection */
  NSLog(@"XXX: start accepting conn");
  do {
    r = accept(svr_fd, reinterpret_cast<struct sockaddr*>(&server_addr),
               reinterpret_cast<socklen_t*>(&socklen));
  } while (r == -1 && errno == EINTR);
  GPR_ASSERT(r >= 0);
  
  NSLog(@"XXX: accepted client and closure...");
  close(r);
  close(svr_fd);

  grpc_core::ExecCtx::Get()->Flush();

  /* wait for the connection callback to finish */
  gpr_mu_lock(&g_mu);
  NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:5];
  while (connections_complete_before == g_connections_complete) {
    gpr_mu_unlock(&g_mu);
    [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode beforeDate:deadline];
    gpr_mu_lock(&g_mu);
  }
  XCTAssertGreaterThan(g_connections_complete, connections_complete_before);

  gpr_mu_unlock(&g_mu);
  
  NSLog(@"XXX: all test done");
}

- (void)testFails {
  grpc_core::ExecCtx exec_ctx;

  grpc_resolved_address resolved_addr;
  struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(resolved_addr.addr);
  int connections_complete_before;
  grpc_closure done;
  int svr_fd;

  gpr_log(GPR_DEBUG, "test_fails");

  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;

  svr_fd = socket(AF_INET, SOCK_STREAM, 0);
  GPR_ASSERT(svr_fd >= 0);
  GPR_ASSERT(0 == bind(svr_fd, (struct sockaddr*)addr, (socklen_t)resolved_addr.len));
  GPR_ASSERT(0 == listen(svr_fd, 1));
  GPR_ASSERT(getsockname(svr_fd, (struct sockaddr*)addr, (socklen_t*)&resolved_addr.len) == 0);
  close(svr_fd);

  gpr_mu_lock(&g_mu);
  connections_complete_before = g_connections_complete;
  gpr_mu_unlock(&g_mu);

  /* connect to a broken address */
  GRPC_CLOSURE_INIT(&done, must_fail, nullptr, grpc_schedule_on_exec_ctx);
  const grpc_channel_args* args =
      grpc_core::CoreConfiguration::Get().channel_args_preconditioning().PreconditionChannelArgs(
          nullptr);
  grpc_tcp_client_connect(&done, &g_connecting, nullptr, args, &resolved_addr,
                          GRPC_MILLIS_INF_FUTURE);
  grpc_channel_args_destroy(args);

  grpc_core::ExecCtx::Get()->Flush();

  /* wait for the connection callback to finish */
  gpr_mu_lock(&g_mu);
  NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:5];
  while (g_connections_complete == connections_complete_before) {
    gpr_mu_unlock(&g_mu);
    [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode beforeDate:deadline];
    gpr_mu_lock(&g_mu);
  }

  XCTAssertGreaterThan(g_connections_complete, connections_complete_before);

  gpr_mu_unlock(&g_mu);
}

@end

#else  // GRPC_CFSTREAM

// Phony test suite
@interface CFStreamClientTests : XCTestCase

@end

@implementation CFStreamClientTests

- (void)setUp {
  [super setUp];
}

- (void)tearDown {
  [super tearDown];
}

@end

#endif  // GRPC_CFSTREAM
