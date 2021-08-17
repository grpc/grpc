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

static const int kConnectTimeout = 5;
static const int kWriteTimeout = 5;
static const int kReadTimeout = 5;

static const int kBufferSize = 10000;

static const int kRunLoopTimeout = 1;

static void set_atm(void *arg, grpc_error_handle error) {
  gpr_atm *p = static_cast<gpr_atm *>(arg);
  gpr_atm_full_cas(p, -1, reinterpret_cast<gpr_atm>(error));
}

static void init_event_closure(grpc_closure *closure, gpr_atm *atm) {
  *atm = -1;
  GRPC_CLOSURE_INIT(closure, set_atm, static_cast<void *>(atm), grpc_schedule_on_exec_ctx);
}

static bool compare_slice_buffer_with_buffer(grpc_slice_buffer *slices, const char *buffer,
                                             size_t buffer_len) {
  if (slices->length != buffer_len) {
    return false;
  }

  for (int i = 0; i < slices->count; i++) {
    grpc_slice slice = slices->slices[i];
    if (0 != memcmp(buffer, GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice))) {
      return false;
    }
    buffer += GRPC_SLICE_LENGTH(slice);
  }

  return true;
}

@interface CFStreamEndpointTests : XCTestCase

@end

@implementation CFStreamEndpointTests {
  grpc_endpoint *ep_;
  int svr_fd_;
}

- (BOOL)waitForEvent:(gpr_atm *)event timeout:(int)timeout {
  grpc_core::ExecCtx::Get()->Flush();

  NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:kConnectTimeout];
  while (gpr_atm_acq_load(event) == -1 && [deadline timeIntervalSinceNow] > 0) {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:kRunLoopTimeout];
    [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode beforeDate:deadline];
  }

  return (gpr_atm_acq_load(event) != -1);
}

+ (void)setUp {
  grpc_init();
}

+ (void)tearDown {
  grpc_shutdown();
}

- (void)setUp {
  self.continueAfterFailure = NO;

  // Set up CFStream connection before testing the endpoint

  grpc_core::ExecCtx exec_ctx;

  grpc_resolved_address resolved_addr;
  struct sockaddr_in *addr = reinterpret_cast<struct sockaddr_in *>(resolved_addr.addr);
  int svr_fd;
  int r;
  gpr_atm connected = -1;
  grpc_closure done;

  gpr_log(GPR_DEBUG, "test_succeeds");

  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = sizeof(struct sockaddr_in);
  addr->sin_family = AF_INET;

  /* create a phony server */
  svr_fd = socket(AF_INET, SOCK_STREAM, 0);
  XCTAssertGreaterThanOrEqual(svr_fd, 0);
  XCTAssertEqual(bind(svr_fd, (struct sockaddr *)addr, (socklen_t)resolved_addr.len), 0);
  XCTAssertEqual(listen(svr_fd, 1), 0);

  /* connect to it */
  XCTAssertEqual(getsockname(svr_fd, (struct sockaddr *)addr, (socklen_t *)&resolved_addr.len), 0);
  init_event_closure(&done, &connected);
  grpc_tcp_client_connect(&done, &ep_, nullptr, nullptr, &resolved_addr, GRPC_MILLIS_INF_FUTURE);

  /* await the connection */
  do {
    resolved_addr.len = sizeof(addr);
    r = accept(svr_fd, reinterpret_cast<struct sockaddr *>(addr),
               reinterpret_cast<socklen_t *>(&resolved_addr.len));
  } while (r == -1 && errno == EINTR);
  XCTAssertGreaterThanOrEqual(r, 0);
  svr_fd_ = r;

  /* wait for the connection callback to finish */
  XCTAssertEqual([self waitForEvent:&connected timeout:kConnectTimeout], YES);
  XCTAssertEqual(reinterpret_cast<grpc_error_handle>(connected), GRPC_ERROR_NONE);
}

- (void)tearDown {
  grpc_core::ExecCtx exec_ctx;
  close(svr_fd_);
  grpc_endpoint_destroy(ep_);
}

- (void)testReadWrite {
  grpc_core::ExecCtx exec_ctx;

  gpr_atm read;
  grpc_closure read_done;
  grpc_slice_buffer read_slices;
  grpc_slice_buffer read_one_slice;
  gpr_atm write;
  grpc_closure write_done;
  grpc_slice_buffer write_slices;

  grpc_slice slice;
  char write_buffer[kBufferSize];
  char read_buffer[kBufferSize];
  size_t recv_size = 0;

  grpc_slice_buffer_init(&write_slices);
  slice = grpc_slice_from_static_buffer(write_buffer, kBufferSize);
  grpc_slice_buffer_add(&write_slices, slice);
  init_event_closure(&write_done, &write);
  grpc_endpoint_write(ep_, &write_slices, &write_done, nullptr);

  XCTAssertEqual([self waitForEvent:&write timeout:kWriteTimeout], YES);
  XCTAssertEqual(reinterpret_cast<grpc_error_handle>(write), GRPC_ERROR_NONE);

  while (recv_size < kBufferSize) {
    ssize_t size = recv(svr_fd_, read_buffer, kBufferSize, 0);
    XCTAssertGreaterThanOrEqual(size, 0);
    recv_size += size;
  }

  XCTAssertEqual(recv_size, kBufferSize);
  XCTAssertEqual(memcmp(read_buffer, write_buffer, kBufferSize), 0);
  ssize_t send_size = send(svr_fd_, read_buffer, kBufferSize, 0);
  XCTAssertGreaterThanOrEqual(send_size, 0);

  grpc_slice_buffer_init(&read_slices);
  grpc_slice_buffer_init(&read_one_slice);
  while (read_slices.length < kBufferSize) {
    init_event_closure(&read_done, &read);
    grpc_endpoint_read(ep_, &read_one_slice, &read_done, /*urgent=*/false);
    XCTAssertEqual([self waitForEvent:&read timeout:kReadTimeout], YES);
    XCTAssertEqual(reinterpret_cast<grpc_error_handle>(read), GRPC_ERROR_NONE);
    grpc_slice_buffer_move_into(&read_one_slice, &read_slices);
    XCTAssertLessThanOrEqual(read_slices.length, kBufferSize);
  }
  XCTAssertTrue(compare_slice_buffer_with_buffer(&read_slices, read_buffer, kBufferSize));

  grpc_endpoint_shutdown(ep_, GRPC_ERROR_NONE);
  grpc_slice_buffer_reset_and_unref(&read_slices);
  grpc_slice_buffer_reset_and_unref(&write_slices);
  grpc_slice_buffer_reset_and_unref(&read_one_slice);
}

- (void)testShutdownBeforeRead {
  grpc_core::ExecCtx exec_ctx;

  gpr_atm read;
  grpc_closure read_done;
  grpc_slice_buffer read_slices;
  gpr_atm write;
  grpc_closure write_done;
  grpc_slice_buffer write_slices;

  grpc_slice slice;
  char write_buffer[kBufferSize];
  char read_buffer[kBufferSize];
  size_t recv_size = 0;

  grpc_slice_buffer_init(&read_slices);
  init_event_closure(&read_done, &read);
  grpc_endpoint_read(ep_, &read_slices, &read_done, /*urgent=*/false);

  grpc_slice_buffer_init(&write_slices);
  slice = grpc_slice_from_static_buffer(write_buffer, kBufferSize);
  grpc_slice_buffer_add(&write_slices, slice);
  init_event_closure(&write_done, &write);
  grpc_endpoint_write(ep_, &write_slices, &write_done, nullptr);

  XCTAssertEqual([self waitForEvent:&write timeout:kWriteTimeout], YES);
  XCTAssertEqual(reinterpret_cast<grpc_error_handle>(write), GRPC_ERROR_NONE);

  while (recv_size < kBufferSize) {
    ssize_t size = recv(svr_fd_, read_buffer, kBufferSize, 0);
    XCTAssertGreaterThanOrEqual(size, 0);
    recv_size += size;
  }

  XCTAssertEqual(recv_size, kBufferSize);
  XCTAssertEqual(memcmp(read_buffer, write_buffer, kBufferSize), 0);

  XCTAssertEqual([self waitForEvent:&read timeout:kReadTimeout], NO);

  grpc_endpoint_shutdown(ep_, GRPC_ERROR_NONE);

  grpc_core::ExecCtx::Get()->Flush();
  XCTAssertEqual([self waitForEvent:&read timeout:kReadTimeout], YES);
  XCTAssertNotEqual(reinterpret_cast<grpc_error_handle>(read), GRPC_ERROR_NONE);

  grpc_slice_buffer_reset_and_unref(&read_slices);
  grpc_slice_buffer_reset_and_unref(&write_slices);
}

- (void)testRemoteClosed {
  grpc_core::ExecCtx exec_ctx;

  gpr_atm read;
  grpc_closure read_done;
  grpc_slice_buffer read_slices;
  gpr_atm write;
  grpc_closure write_done;
  grpc_slice_buffer write_slices;

  grpc_slice slice;
  char write_buffer[kBufferSize];
  char read_buffer[kBufferSize];
  size_t recv_size = 0;

  init_event_closure(&read_done, &read);
  grpc_slice_buffer_init(&read_slices);
  grpc_endpoint_read(ep_, &read_slices, &read_done, /*urgent=*/false);

  grpc_slice_buffer_init(&write_slices);
  slice = grpc_slice_from_static_buffer(write_buffer, kBufferSize);
  grpc_slice_buffer_add(&write_slices, slice);
  init_event_closure(&write_done, &write);
  grpc_endpoint_write(ep_, &write_slices, &write_done, nullptr);

  XCTAssertEqual([self waitForEvent:&write timeout:kWriteTimeout], YES);
  XCTAssertEqual(reinterpret_cast<grpc_error_handle>(write), GRPC_ERROR_NONE);

  while (recv_size < kBufferSize) {
    ssize_t size = recv(svr_fd_, read_buffer, kBufferSize, 0);
    XCTAssertGreaterThanOrEqual(size, 0);
    recv_size += size;
  }

  XCTAssertEqual(recv_size, kBufferSize);
  XCTAssertEqual(memcmp(read_buffer, write_buffer, kBufferSize), 0);

  close(svr_fd_);

  XCTAssertEqual([self waitForEvent:&read timeout:kReadTimeout], YES);
  XCTAssertNotEqual(reinterpret_cast<grpc_error_handle>(read), GRPC_ERROR_NONE);

  grpc_endpoint_shutdown(ep_, GRPC_ERROR_NONE);
  grpc_slice_buffer_reset_and_unref(&read_slices);
  grpc_slice_buffer_reset_and_unref(&write_slices);
}

- (void)testRemoteReset {
  grpc_core::ExecCtx exec_ctx;

  gpr_atm read;
  grpc_closure read_done;
  grpc_slice_buffer read_slices;

  init_event_closure(&read_done, &read);
  grpc_slice_buffer_init(&read_slices);
  grpc_endpoint_read(ep_, &read_slices, &read_done, /*urgent=*/false);

  struct linger so_linger;
  so_linger.l_onoff = 1;
  so_linger.l_linger = 0;
  setsockopt(svr_fd_, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));

  close(svr_fd_);

  XCTAssertEqual([self waitForEvent:&read timeout:kReadTimeout], YES);
  XCTAssertNotEqual(reinterpret_cast<grpc_error_handle>(read), GRPC_ERROR_NONE);

  grpc_endpoint_shutdown(ep_, GRPC_ERROR_NONE);
  grpc_slice_buffer_reset_and_unref(&read_slices);
}

@end

#else  // GRPC_CFSTREAM

// Phony test suite
@interface CFStreamEndpointTests : XCTestCase
@end

@implementation CFStreamEndpointTests
- (void)setUp {
  [super setUp];
}

- (void)tearDown {
  [super tearDown];
}

@end

#endif  // GRPC_CFSTREAM
