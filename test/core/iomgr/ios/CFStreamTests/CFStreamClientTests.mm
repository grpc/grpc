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

#include <netinet/in.h>

#include <grpc/impl/codegen/sync.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "test/core/util/test_config.h"

// static int g_connections_complete = 0;
static gpr_mu g_mu;
static int g_connections_complete = 0;
static grpc_endpoint* g_connecting = nullptr;

static void finish_connection() {
  gpr_mu_lock(&g_mu);
  g_connections_complete++;
  gpr_mu_unlock(&g_mu);
}

static void must_succeed(void* arg, grpc_error* error) {
  GPR_ASSERT(g_connecting != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_endpoint_shutdown(g_connecting, GRPC_ERROR_CREATE_FROM_STATIC_STRING("must_succeed called"));
  grpc_endpoint_destroy(g_connecting);
  g_connecting = nullptr;
  finish_connection();
}

static void must_fail(void* arg, grpc_error* error) {
  GPR_ASSERT(g_connecting == nullptr);
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  const char* error_str = grpc_error_string(error);
  NSLog(@"%s", error_str);
  finish_connection();
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
  grpc_resolved_address resolved_addr;
  struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(resolved_addr.addr);
  int svr_fd;
  int r;
  int connections_complete_before;
  grpc_closure done;
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_DEBUG, "test_succeeds");

  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = sizeof(struct sockaddr_in);
  addr->sin_family = AF_INET;

  /* create a dummy server */
  svr_fd = socket(AF_INET, SOCK_STREAM, 0);
  GPR_ASSERT(svr_fd >= 0);
  GPR_ASSERT(0 == bind(svr_fd, (struct sockaddr*)addr, (socklen_t)resolved_addr.len));
  GPR_ASSERT(0 == listen(svr_fd, 1));

  gpr_mu_lock(&g_mu);
  connections_complete_before = g_connections_complete;
  gpr_mu_unlock(&g_mu);

  /* connect to it */
  GPR_ASSERT(getsockname(svr_fd, (struct sockaddr*)addr, (socklen_t*)&resolved_addr.len) == 0);
  GRPC_CLOSURE_INIT(&done, must_succeed, nullptr, grpc_schedule_on_exec_ctx);
  grpc_tcp_client_connect(&done, &g_connecting, nullptr, nullptr, &resolved_addr,
                          GRPC_MILLIS_INF_FUTURE);

  /* await the connection */
  do {
    resolved_addr.len = sizeof(addr);
    r = accept(svr_fd, reinterpret_cast<struct sockaddr*>(addr),
               reinterpret_cast<socklen_t*>(&resolved_addr.len));
  } while (r == -1 && errno == EINTR);
  GPR_ASSERT(r >= 0);
  close(r);

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
  grpc_tcp_client_connect(&done, &g_connecting, nullptr, nullptr, &resolved_addr,
                          GRPC_MILLIS_INF_FUTURE);

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

// Dummy test suite
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
