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

#include <limits.h>

#include <netinet/in.h>

#include <grpc/grpc.h>
#include <grpc/impl/codegen/sync.h>
#include <grpc/support/sync.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/resource_quota/api.h"
#include "test/core/util/test_config.h"

#include <chrono>
#include <future>

static const int kConnectTimeout = 5;
static const int kWriteTimeout = 5;
static const int kReadTimeout = 5;

static const int kBufferSize = 10000;

static const int kRunLoopTimeout = 1;

static void set_error_handle_promise(void *arg, grpc_error_handle error) {
  std::promise<grpc_error_handle> *p = static_cast<std::promise<grpc_error_handle> *>(arg);
  p->set_value(error);
}

static void init_event_closure(grpc_closure *closure,
                               std::promise<grpc_error_handle> *error_handle) {
  GRPC_CLOSURE_INIT(closure, set_error_handle_promise, static_cast<void *>(error_handle),
                    grpc_schedule_on_exec_ctx);
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

- (BOOL)waitForEvent:(std::future<grpc_error_handle> *)event timeout:(int)timeout {
  grpc_core::ExecCtx::Get()->Flush();
  return event->wait_for(std::chrono::seconds(timeout)) != std::future_status::timeout;
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

  int svr_fd;
  int r;
  std::promise<grpc_error_handle> connected_promise;
  grpc_closure done;

  gpr_log(GPR_DEBUG, "test_succeeds");

  auto resolved_addr = grpc_core::StringToSockaddr("127.0.0.1:0");
  struct sockaddr_in *addr = reinterpret_cast<struct sockaddr_in *>(resolved_addr->addr);

  /* create a phony server */
  svr_fd = socket(AF_INET, SOCK_STREAM, 0);
  XCTAssertGreaterThanOrEqual(svr_fd, 0);
  XCTAssertEqual(bind(svr_fd, (struct sockaddr *)addr, (socklen_t)resolved_addr->len), 0);
  XCTAssertEqual(listen(svr_fd, 1), 0);

  /* connect to it */
  XCTAssertEqual(getsockname(svr_fd, (struct sockaddr *)addr, (socklen_t *)&resolved_addr->len), 0);
  init_event_closure(&done, &connected_promise);
  auto args =
      grpc_core::CoreConfiguration::Get().channel_args_preconditioning().PreconditionChannelArgs(
          nullptr);
  grpc_tcp_client_connect(&done, &ep_, nullptr,
                          grpc_event_engine::experimental::ChannelArgsEndpointConfig(args),
                          &*resolved_addr, grpc_core::Timestamp::InfFuture());

  /* await the connection */
  do {
    resolved_addr->len = sizeof(addr);
    r = accept(svr_fd, reinterpret_cast<struct sockaddr *>(addr),
               reinterpret_cast<socklen_t *>(&resolved_addr->len));
  } while (r == -1 && errno == EINTR);
  XCTAssertGreaterThanOrEqual(r, 0, @"connection failed with return code %@ and errno %@", @(r),
                              @(errno));
  svr_fd_ = r;

  /* wait for the connection callback to finish */
  std::future<grpc_error_handle> connected_future = connected_promise.get_future();
  XCTAssertEqual([self waitForEvent:&connected_future timeout:kConnectTimeout], YES);
  XCTAssertEqual(connected_future.get(), absl::OkStatus());
}

- (void)tearDown {
  grpc_core::ExecCtx exec_ctx;
  close(svr_fd_);
  grpc_endpoint_destroy(ep_);
}

- (void)testReadWrite {
  grpc_core::ExecCtx exec_ctx;

  grpc_closure read_done;
  grpc_slice_buffer read_slices;
  grpc_slice_buffer read_one_slice;
  std::promise<grpc_error_handle> write_promise;
  grpc_closure write_done;
  grpc_slice_buffer write_slices;

  grpc_slice slice;
  char write_buffer[kBufferSize];
  char read_buffer[kBufferSize];
  size_t recv_size = 0;

  grpc_slice_buffer_init(&write_slices);
  slice = grpc_slice_from_static_buffer(write_buffer, kBufferSize);
  grpc_slice_buffer_add(&write_slices, slice);
  init_event_closure(&write_done, &write_promise);
  grpc_endpoint_write(ep_, &write_slices, &write_done, nullptr, /*max_frame_size=*/INT_MAX);

  std::future<grpc_error_handle> write_future = write_promise.get_future();
  XCTAssertEqual([self waitForEvent:&write_future timeout:kWriteTimeout], YES);
  XCTAssertEqual(write_future.get(), absl::OkStatus());

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
    std::promise<grpc_error_handle> read_promise;
    init_event_closure(&read_done, &read_promise);
    grpc_endpoint_read(ep_, &read_one_slice, &read_done, /*urgent=*/false,
                       /*min_progress_size=*/1);
    std::future<grpc_error_handle> read_future = read_promise.get_future();
    XCTAssertEqual([self waitForEvent:&read_future timeout:kReadTimeout], YES);
    XCTAssertEqual(read_future.get(), absl::OkStatus());
    grpc_slice_buffer_move_into(&read_one_slice, &read_slices);
    XCTAssertLessThanOrEqual(read_slices.length, kBufferSize);
  }
  XCTAssertTrue(compare_slice_buffer_with_buffer(&read_slices, read_buffer, kBufferSize));

  grpc_endpoint_shutdown(ep_, absl::OkStatus());
  grpc_slice_buffer_reset_and_unref(&read_slices);
  grpc_slice_buffer_reset_and_unref(&write_slices);
  grpc_slice_buffer_reset_and_unref(&read_one_slice);
}

- (void)testShutdownBeforeRead {
  grpc_core::ExecCtx exec_ctx;

  std::promise<grpc_error_handle> read_promise;
  grpc_closure read_done;
  grpc_slice_buffer read_slices;
  std::promise<grpc_error_handle> write_promise;
  grpc_closure write_done;
  grpc_slice_buffer write_slices;

  grpc_slice slice;
  char write_buffer[kBufferSize];
  char read_buffer[kBufferSize];
  size_t recv_size = 0;

  grpc_slice_buffer_init(&read_slices);
  init_event_closure(&read_done, &read_promise);
  grpc_endpoint_read(ep_, &read_slices, &read_done, /*urgent=*/false,
                     /*min_progress_size=*/1);

  grpc_slice_buffer_init(&write_slices);
  slice = grpc_slice_from_static_buffer(write_buffer, kBufferSize);
  grpc_slice_buffer_add(&write_slices, slice);
  init_event_closure(&write_done, &write_promise);
  grpc_endpoint_write(ep_, &write_slices, &write_done, nullptr, /*max_frame_size=*/INT_MAX);

  std::future<grpc_error_handle> write_future = write_promise.get_future();
  XCTAssertEqual([self waitForEvent:&write_future timeout:kWriteTimeout], YES);
  XCTAssertEqual(write_future.get(), absl::OkStatus());

  while (recv_size < kBufferSize) {
    ssize_t size = recv(svr_fd_, read_buffer, kBufferSize, 0);
    XCTAssertGreaterThanOrEqual(size, 0);
    recv_size += size;
  }

  XCTAssertEqual(recv_size, kBufferSize);
  XCTAssertEqual(memcmp(read_buffer, write_buffer, kBufferSize), 0);

  std::future<grpc_error_handle> read_future = read_promise.get_future();
  XCTAssertEqual([self waitForEvent:&read_future timeout:kReadTimeout], NO);

  grpc_endpoint_shutdown(ep_, absl::OkStatus());

  grpc_core::ExecCtx::Get()->Flush();
  XCTAssertEqual([self waitForEvent:&read_future timeout:kReadTimeout], YES);
  XCTAssertNotEqual(read_future.get(), absl::OkStatus());

  grpc_slice_buffer_reset_and_unref(&read_slices);
  grpc_slice_buffer_reset_and_unref(&write_slices);
}

- (void)testRemoteClosed {
  grpc_core::ExecCtx exec_ctx;

  std::promise<grpc_error_handle> read_promise;
  grpc_closure read_done;
  grpc_slice_buffer read_slices;
  std::promise<grpc_error_handle> write_promise;
  grpc_closure write_done;
  grpc_slice_buffer write_slices;

  grpc_slice slice;
  char write_buffer[kBufferSize];
  char read_buffer[kBufferSize];
  size_t recv_size = 0;

  init_event_closure(&read_done, &read_promise);
  grpc_slice_buffer_init(&read_slices);
  grpc_endpoint_read(ep_, &read_slices, &read_done, /*urgent=*/false,
                     /*min_progress_size=*/1);

  grpc_slice_buffer_init(&write_slices);
  slice = grpc_slice_from_static_buffer(write_buffer, kBufferSize);
  grpc_slice_buffer_add(&write_slices, slice);

  init_event_closure(&write_done, &write_promise);
  grpc_endpoint_write(ep_, &write_slices, &write_done, nullptr, /*max_frame_size=*/INT_MAX);

  std::future<grpc_error_handle> write_future = write_promise.get_future();
  XCTAssertEqual([self waitForEvent:&write_future timeout:kWriteTimeout], YES);
  XCTAssertEqual(write_future.get(), absl::OkStatus());

  while (recv_size < kBufferSize) {
    ssize_t size = recv(svr_fd_, read_buffer, kBufferSize, 0);
    XCTAssertGreaterThanOrEqual(size, 0);
    recv_size += size;
  }

  XCTAssertEqual(recv_size, kBufferSize);
  XCTAssertEqual(memcmp(read_buffer, write_buffer, kBufferSize), 0);

  close(svr_fd_);

  std::future<grpc_error_handle> read_future = read_promise.get_future();
  XCTAssertEqual([self waitForEvent:&read_future timeout:kReadTimeout], YES);
  XCTAssertNotEqual(read_future.get(), absl::OkStatus());

  grpc_endpoint_shutdown(ep_, absl::OkStatus());
  grpc_slice_buffer_reset_and_unref(&read_slices);
  grpc_slice_buffer_reset_and_unref(&write_slices);
}

- (void)testRemoteReset {
  grpc_core::ExecCtx exec_ctx;

  std::promise<grpc_error_handle> read_promise;
  grpc_closure read_done;
  grpc_slice_buffer read_slices;

  init_event_closure(&read_done, &read_promise);
  grpc_slice_buffer_init(&read_slices);
  grpc_endpoint_read(ep_, &read_slices, &read_done, /*urgent=*/false,
                     /*min_progress_size=*/1);

  struct linger so_linger;
  so_linger.l_onoff = 1;
  so_linger.l_linger = 0;
  setsockopt(svr_fd_, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));

  close(svr_fd_);

  std::future<grpc_error_handle> read_future = read_promise.get_future();
  XCTAssertEqual([self waitForEvent:&read_future timeout:kReadTimeout], YES);
  XCTAssertNotEqual(read_future.get(), absl::OkStatus());

  grpc_endpoint_shutdown(ep_, absl::OkStatus());
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
