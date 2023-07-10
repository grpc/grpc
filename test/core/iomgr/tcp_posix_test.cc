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

#include "absl/time/time.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/port.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET_TCP

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/posix.h"
#include "src/core/lib/event_engine/shim.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/buffer_list.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/sockaddr_posix.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/iomgr/endpoint_tests.h"
#include "test/core/util/test_config.h"

static gpr_mu* g_mu;
static grpc_pollset* g_pollset;

static constexpr int64_t kDeadlineMillis = 20000;

//
// General test notes:

// All tests which write data into a socket write i%256 into byte i, which is
// verified by readers.

// In general there are a few interesting things to vary which may lead to
// exercising different codepaths in an implementation:
// 1. Total amount of data written to the socket
// 2. Size of slice allocations
// 3. Amount of data we read from or write to the socket at once

// The tests here tend to parameterize these where applicable.

//

static void create_sockets(int sv[2]) {
  int flags;
  GPR_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  flags = fcntl(sv[0], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) == 0);
  flags = fcntl(sv[1], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[1], F_SETFL, flags | O_NONBLOCK) == 0);
}

static ssize_t fill_socket(int fd) {
  ssize_t write_bytes;
  ssize_t total_bytes = 0;
  int i;
  unsigned char buf[256];
  for (i = 0; i < 256; ++i) {
    buf[i] = static_cast<uint8_t>(i);
  }
  do {
    write_bytes = write(fd, buf, 256);
    if (write_bytes > 0) {
      total_bytes += write_bytes;
    }
  } while (write_bytes >= 0 || errno == EINTR);
  GPR_ASSERT(errno == EAGAIN);
  return total_bytes;
}

static size_t fill_socket_partial(int fd, size_t bytes) {
  ssize_t write_bytes;
  size_t total_bytes = 0;
  unsigned char* buf = static_cast<unsigned char*>(gpr_malloc(bytes));
  unsigned i;
  for (i = 0; i < bytes; ++i) {
    buf[i] = static_cast<uint8_t>(i % 256);
  }

  do {
    write_bytes = write(fd, buf, bytes - total_bytes);
    if (write_bytes > 0) {
      total_bytes += static_cast<size_t>(write_bytes);
    }
  } while ((write_bytes >= 0 || errno == EINTR) && bytes > total_bytes);

  gpr_free(buf);

  return total_bytes;
}

struct read_socket_state {
  grpc_endpoint* ep;
  size_t min_progress_size;
  size_t read_bytes;
  size_t target_read_bytes;
  grpc_slice_buffer incoming;
  grpc_closure read_cb;
};

static size_t count_slices(grpc_slice* slices, size_t nslices,
                           int* current_data) {
  size_t num_bytes = 0;
  unsigned i, j;
  unsigned char* buf;
  for (i = 0; i < nslices; ++i) {
    buf = GRPC_SLICE_START_PTR(slices[i]);
    for (j = 0; j < GRPC_SLICE_LENGTH(slices[i]); ++j) {
      GPR_ASSERT(buf[j] == *current_data);
      *current_data = (*current_data + 1) % 256;
    }
    num_bytes += GRPC_SLICE_LENGTH(slices[i]);
  }
  return num_bytes;
}

static void read_cb(void* user_data, grpc_error_handle error) {
  struct read_socket_state* state =
      static_cast<struct read_socket_state*>(user_data);
  size_t read_bytes;
  int current_data;

  GPR_ASSERT(error.ok());

  gpr_mu_lock(g_mu);
  current_data = state->read_bytes % 256;
  // The number of bytes read each time this callback is invoked must be >=
  // the min_progress_size.
  if (grpc_core::IsTcpFrameSizeTuningEnabled()) {
    GPR_ASSERT(state->min_progress_size <= state->incoming.length);
  }
  read_bytes = count_slices(state->incoming.slices, state->incoming.count,
                            &current_data);
  state->read_bytes += read_bytes;
  gpr_log(GPR_INFO, "Read %" PRIuPTR " bytes of %" PRIuPTR, read_bytes,
          state->target_read_bytes);
  if (state->read_bytes >= state->target_read_bytes) {
    GPR_ASSERT(
        GRPC_LOG_IF_ERROR("kick", grpc_pollset_kick(g_pollset, nullptr)));
    gpr_mu_unlock(g_mu);
  } else {
    gpr_mu_unlock(g_mu);
    state->min_progress_size = state->target_read_bytes - state->read_bytes;
    grpc_endpoint_read(state->ep, &state->incoming, &state->read_cb,
                       /*urgent=*/false, state->min_progress_size);
  }
}

// Write to a socket, then read from it using the grpc_tcp API.
static void read_test(size_t num_bytes, size_t slice_size,
                      int min_progress_size) {
  int sv[2];
  grpc_endpoint* ep;
  struct read_socket_state state;
  size_t written_bytes;
  grpc_core::Timestamp deadline = grpc_core::Timestamp::FromTimespecRoundUp(
      grpc_timeout_milliseconds_to_deadline(kDeadlineMillis));
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_INFO, "Read test of size %" PRIuPTR ", slice size %" PRIuPTR,
          num_bytes, slice_size);

  create_sockets(sv);

  grpc_arg a[2];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER,
  a[0].value.integer = static_cast<int>(slice_size);
  a[1].key = const_cast<char*>(GRPC_ARG_RESOURCE_QUOTA);
  a[1].type = GRPC_ARG_POINTER;
  a[1].value.pointer.p = grpc_resource_quota_create("test");
  a[1].value.pointer.vtable = grpc_resource_quota_arg_vtable();
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  ep = grpc_tcp_create(
      grpc_fd_create(sv[1], "read_test", false),
      TcpOptionsFromEndpointConfig(
          grpc_event_engine::experimental::ChannelArgsEndpointConfig(
              grpc_core::ChannelArgs::FromC(&args))),
      "test");
  grpc_endpoint_add_to_pollset(ep, g_pollset);

  written_bytes = fill_socket_partial(sv[0], num_bytes);
  gpr_log(GPR_INFO, "Wrote %" PRIuPTR " bytes", written_bytes);

  state.ep = ep;
  state.read_bytes = 0;
  state.target_read_bytes = written_bytes;
  state.min_progress_size =
      std::min(min_progress_size, static_cast<int>(written_bytes));
  grpc_slice_buffer_init(&state.incoming);
  GRPC_CLOSURE_INIT(&state.read_cb, read_cb, &state, grpc_schedule_on_exec_ctx);

  grpc_endpoint_read(ep, &state.incoming, &state.read_cb, /*urgent=*/false,
                     /*min_progress_size=*/state.min_progress_size);
  grpc_core::ExecCtx::Get()->Flush();
  gpr_mu_lock(g_mu);
  while (state.read_bytes < state.target_read_bytes) {
    grpc_pollset_worker* worker = nullptr;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(g_pollset, &worker, deadline)));
    gpr_mu_unlock(g_mu);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(g_mu);
  }
  GPR_ASSERT(state.read_bytes == state.target_read_bytes);
  gpr_mu_unlock(g_mu);

  grpc_slice_buffer_destroy(&state.incoming);
  grpc_endpoint_destroy(ep);
  grpc_resource_quota_unref(
      static_cast<grpc_resource_quota*>(a[1].value.pointer.p));
}

// Write to a socket until it fills up, then read from it using the grpc_tcp
// API.
static void large_read_test(size_t slice_size, int min_progress_size) {
  int sv[2];
  grpc_endpoint* ep;
  struct read_socket_state state;
  ssize_t written_bytes;
  grpc_core::Timestamp deadline = grpc_core::Timestamp::FromTimespecRoundUp(
      grpc_timeout_milliseconds_to_deadline(kDeadlineMillis));
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_INFO, "Start large read test, slice size %" PRIuPTR, slice_size);

  create_sockets(sv);

  grpc_arg a[2];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER;
  a[0].value.integer = static_cast<int>(slice_size);
  a[1].key = const_cast<char*>(GRPC_ARG_RESOURCE_QUOTA);
  a[1].type = GRPC_ARG_POINTER;
  a[1].value.pointer.p = grpc_resource_quota_create("test");
  a[1].value.pointer.vtable = grpc_resource_quota_arg_vtable();
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  ep = grpc_tcp_create(
      grpc_fd_create(sv[1], "large_read_test", false),
      TcpOptionsFromEndpointConfig(
          grpc_event_engine::experimental::ChannelArgsEndpointConfig(
              grpc_core::ChannelArgs::FromC(&args))),
      "test");
  grpc_endpoint_add_to_pollset(ep, g_pollset);

  written_bytes = fill_socket(sv[0]);
  gpr_log(GPR_INFO, "Wrote %" PRIuPTR " bytes", written_bytes);

  state.ep = ep;
  state.read_bytes = 0;
  state.target_read_bytes = static_cast<size_t>(written_bytes);
  state.min_progress_size =
      std::min(min_progress_size, static_cast<int>(written_bytes));
  grpc_slice_buffer_init(&state.incoming);
  GRPC_CLOSURE_INIT(&state.read_cb, read_cb, &state, grpc_schedule_on_exec_ctx);

  grpc_endpoint_read(ep, &state.incoming, &state.read_cb, /*urgent=*/false,
                     /*min_progress_size=*/state.min_progress_size);
  grpc_core::ExecCtx::Get()->Flush();
  gpr_mu_lock(g_mu);
  while (state.read_bytes < state.target_read_bytes) {
    grpc_pollset_worker* worker = nullptr;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(g_pollset, &worker, deadline)));
    gpr_mu_unlock(g_mu);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(g_mu);
  }
  GPR_ASSERT(state.read_bytes == state.target_read_bytes);
  gpr_mu_unlock(g_mu);

  grpc_slice_buffer_destroy(&state.incoming);
  grpc_endpoint_destroy(ep);
  grpc_resource_quota_unref(
      static_cast<grpc_resource_quota*>(a[1].value.pointer.p));
}

struct write_socket_state {
  grpc_endpoint* ep;
  int write_done;
};

static grpc_slice* allocate_blocks(size_t num_bytes, size_t slice_size,
                                   size_t* num_blocks, uint8_t* current_data) {
  size_t nslices = num_bytes / slice_size + (num_bytes % slice_size ? 1u : 0u);
  grpc_slice* slices =
      static_cast<grpc_slice*>(gpr_malloc(sizeof(grpc_slice) * nslices));
  size_t num_bytes_left = num_bytes;
  unsigned i, j;
  unsigned char* buf;
  *num_blocks = nslices;

  for (i = 0; i < nslices; ++i) {
    slices[i] = grpc_slice_malloc(slice_size > num_bytes_left ? num_bytes_left
                                                              : slice_size);
    num_bytes_left -= GRPC_SLICE_LENGTH(slices[i]);
    buf = GRPC_SLICE_START_PTR(slices[i]);
    for (j = 0; j < GRPC_SLICE_LENGTH(slices[i]); ++j) {
      buf[j] = *current_data;
      (*current_data)++;
    }
  }
  GPR_ASSERT(num_bytes_left == 0);
  return slices;
}

static void write_done(void* user_data /* write_socket_state */,
                       grpc_error_handle error) {
  GPR_ASSERT(error.ok());
  struct write_socket_state* state =
      static_cast<struct write_socket_state*>(user_data);
  gpr_mu_lock(g_mu);
  state->write_done = 1;
  GPR_ASSERT(
      GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(g_pollset, nullptr)));
  gpr_mu_unlock(g_mu);
}

void drain_socket_blocking(int fd, size_t num_bytes, size_t read_size) {
  unsigned char* buf = static_cast<unsigned char*>(gpr_malloc(read_size));
  ssize_t bytes_read;
  size_t bytes_left = num_bytes;
  int flags;
  int current = 0;
  int i;
  grpc_core::ExecCtx exec_ctx;

  flags = fcntl(fd, F_GETFL, 0);
  GPR_ASSERT(fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0);

  for (;;) {
    grpc_pollset_worker* worker = nullptr;
    gpr_mu_lock(g_mu);
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work",
        grpc_pollset_work(g_pollset, &worker,
                          grpc_core::Timestamp::FromTimespecRoundUp(
                              grpc_timeout_milliseconds_to_deadline(10)))));
    gpr_mu_unlock(g_mu);

    do {
      bytes_read =
          read(fd, buf, bytes_left > read_size ? read_size : bytes_left);
    } while (bytes_read < 0 && errno == EINTR);
    GPR_ASSERT(bytes_read >= 0);
    for (i = 0; i < bytes_read; ++i) {
      GPR_ASSERT(buf[i] == current);
      current = (current + 1) % 256;
    }
    bytes_left -= static_cast<size_t>(bytes_read);
    if (bytes_left == 0) break;
  }
  flags = fcntl(fd, F_GETFL, 0);
  GPR_ASSERT(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);

  gpr_free(buf);
}

// Write to a socket using the grpc_tcp API, then drain it directly.
// Note that if the write does not complete immediately we need to drain the
// socket in parallel with the read. If collect_timestamps is true, it will
// try to get timestamps for the write.
static void write_test(size_t num_bytes, size_t slice_size) {
  int sv[2];
  grpc_endpoint* ep;
  struct write_socket_state state;
  size_t num_blocks;
  grpc_slice* slices;
  uint8_t current_data = 0;
  grpc_slice_buffer outgoing;
  grpc_closure write_done_closure;
  grpc_core::Timestamp deadline = grpc_core::Timestamp::FromTimespecRoundUp(
      grpc_timeout_milliseconds_to_deadline(kDeadlineMillis));
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_INFO,
          "Start write test with %" PRIuPTR " bytes, slice size %" PRIuPTR,
          num_bytes, slice_size);

  create_sockets(sv);

  grpc_arg a[2];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER,
  a[0].value.integer = static_cast<int>(slice_size);
  a[1].key = const_cast<char*>(GRPC_ARG_RESOURCE_QUOTA);
  a[1].type = GRPC_ARG_POINTER;
  a[1].value.pointer.p = grpc_resource_quota_create("test");
  a[1].value.pointer.vtable = grpc_resource_quota_arg_vtable();
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  ep = grpc_tcp_create(
      grpc_fd_create(sv[1], "write_test", false),
      TcpOptionsFromEndpointConfig(
          grpc_event_engine::experimental::ChannelArgsEndpointConfig(
              grpc_core::ChannelArgs::FromC(&args))),
      "test");
  grpc_endpoint_add_to_pollset(ep, g_pollset);

  state.ep = ep;
  state.write_done = 0;

  slices = allocate_blocks(num_bytes, slice_size, &num_blocks, &current_data);

  grpc_slice_buffer_init(&outgoing);
  grpc_slice_buffer_addn(&outgoing, slices, num_blocks);
  GRPC_CLOSURE_INIT(&write_done_closure, write_done, &state,
                    grpc_schedule_on_exec_ctx);

  grpc_endpoint_write(ep, &outgoing, &write_done_closure, nullptr,
                      /*max_frame_size=*/INT_MAX);
  drain_socket_blocking(sv[0], num_bytes, num_bytes);
  exec_ctx.Flush();
  gpr_mu_lock(g_mu);
  for (;;) {
    grpc_pollset_worker* worker = nullptr;
    if (state.write_done) {
      break;
    }
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(g_pollset, &worker, deadline)));
    gpr_mu_unlock(g_mu);
    exec_ctx.Flush();
    gpr_mu_lock(g_mu);
  }
  gpr_mu_unlock(g_mu);

  grpc_slice_buffer_destroy(&outgoing);
  grpc_endpoint_destroy(ep);
  gpr_free(slices);
  grpc_resource_quota_unref(
      static_cast<grpc_resource_quota*>(a[1].value.pointer.p));
}

struct release_fd_arg {
  std::atomic<int> fd_released_done{0};
  grpc_core::Notification notify;
};

void on_fd_released(void* arg, grpc_error_handle /*errors*/) {
  release_fd_arg* rel_fd = static_cast<release_fd_arg*>(arg);
  rel_fd->fd_released_done = 1;
  rel_fd->notify.Notify();
}

// Do a read_test, then release fd and try to read/write again. Verify that
// grpc_tcp_fd() is available before the fd is released.
static void release_fd_test(size_t num_bytes, size_t slice_size) {
  int sv[2];
  grpc_endpoint* ep;
  struct read_socket_state state;
  size_t written_bytes;
  int fd;
  grpc_core::Timestamp deadline = grpc_core::Timestamp::FromTimespecRoundUp(
      grpc_timeout_milliseconds_to_deadline(kDeadlineMillis));
  grpc_core::ExecCtx exec_ctx;
  grpc_closure fd_released_cb;
  release_fd_arg rel_fd;
  GRPC_CLOSURE_INIT(&fd_released_cb, &on_fd_released, &rel_fd,
                    grpc_schedule_on_exec_ctx);

  gpr_log(GPR_INFO,
          "Release fd read_test of size %" PRIuPTR ", slice size %" PRIuPTR,
          num_bytes, slice_size);

  create_sockets(sv);

  grpc_arg a[2];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER;
  a[0].value.integer = static_cast<int>(slice_size);
  a[1].key = const_cast<char*>(GRPC_ARG_RESOURCE_QUOTA);
  a[1].type = GRPC_ARG_POINTER;
  a[1].value.pointer.p = grpc_resource_quota_create("test");
  a[1].value.pointer.vtable = grpc_resource_quota_arg_vtable();
  auto memory_quota = std::make_unique<grpc_core::MemoryQuota>("bar");
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  if (grpc_event_engine::experimental::UseEventEngineListener()) {
    // Create an event engine wrapped endpoint to test release_fd operations.
    auto eeep =
        reinterpret_cast<
            grpc_event_engine::experimental::PosixEventEngineWithFdSupport*>(
            grpc_event_engine::experimental::GetDefaultEventEngine().get())
            ->CreatePosixEndpointFromFd(
                sv[1],
                grpc_event_engine::experimental::ChannelArgsEndpointConfig(
                    grpc_core::ChannelArgs::FromC(&args)),
                memory_quota->CreateMemoryAllocator("test"));
    ep = grpc_event_engine::experimental::grpc_event_engine_endpoint_create(
        std::move(eeep));
  } else {
    ep = grpc_tcp_create(
        grpc_fd_create(sv[1], "read_test", false),
        TcpOptionsFromEndpointConfig(
            grpc_event_engine::experimental::ChannelArgsEndpointConfig(
                grpc_core::ChannelArgs::FromC(&args))),
        "test");
    GPR_ASSERT(grpc_tcp_fd(ep) == sv[1] && sv[1] >= 0);
  }
  grpc_endpoint_add_to_pollset(ep, g_pollset);

  written_bytes = fill_socket_partial(sv[0], num_bytes);
  gpr_log(GPR_INFO, "Wrote %" PRIuPTR " bytes", written_bytes);

  state.ep = ep;
  state.read_bytes = 0;
  state.target_read_bytes = written_bytes;
  state.min_progress_size = 1;
  grpc_slice_buffer_init(&state.incoming);
  GRPC_CLOSURE_INIT(&state.read_cb, read_cb, &state, grpc_schedule_on_exec_ctx);

  grpc_endpoint_read(ep, &state.incoming, &state.read_cb, /*urgent=*/false,
                     /*min_progress_size=*/state.min_progress_size);
  grpc_core::ExecCtx::Get()->Flush();
  gpr_mu_lock(g_mu);
  while (state.read_bytes < state.target_read_bytes) {
    grpc_pollset_worker* worker = nullptr;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(g_pollset, &worker, deadline)));
    gpr_log(GPR_DEBUG, "wakeup: read=%" PRIdPTR " target=%" PRIdPTR,
            state.read_bytes, state.target_read_bytes);
    gpr_mu_unlock(g_mu);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(g_mu);
  }
  GPR_ASSERT(state.read_bytes == state.target_read_bytes);
  gpr_mu_unlock(g_mu);

  grpc_slice_buffer_destroy(&state.incoming);
  grpc_tcp_destroy_and_release_fd(ep, &fd, &fd_released_cb);
  grpc_core::ExecCtx::Get()->Flush();
  rel_fd.notify.WaitForNotificationWithTimeout(absl::Seconds(20));
  GPR_ASSERT(rel_fd.fd_released_done == 1);
  GPR_ASSERT(fd == sv[1]);
  written_bytes = fill_socket_partial(sv[0], num_bytes);
  drain_socket_blocking(fd, written_bytes, written_bytes);
  written_bytes = fill_socket_partial(fd, num_bytes);
  drain_socket_blocking(sv[0], written_bytes, written_bytes);
  close(fd);
  grpc_resource_quota_unref(
      static_cast<grpc_resource_quota*>(a[1].value.pointer.p));
}

void run_tests(void) {
  size_t i = 0;
  for (int i = 1; i <= 8192; i = i * 2) {
    read_test(100, 8192, i);
    read_test(10000, 8192, i);
    read_test(10000, 137, i);
    read_test(10000, 1, i);
    large_read_test(8192, i);
    large_read_test(1, i);
  }
  write_test(100, 8192);
  write_test(100, 1);
  write_test(100000, 8192);
  write_test(100000, 1);
  write_test(100000, 137);

  for (i = 1; i < 1000; i = std::max(i + 1, i * 5 / 4)) {
    write_test(40320, i);
  }

  release_fd_test(100, 8192);
}

static void clean_up(void) {}

static grpc_endpoint_test_fixture create_fixture_tcp_socketpair(
    size_t slice_size) {
  int sv[2];
  grpc_endpoint_test_fixture f;
  grpc_core::ExecCtx exec_ctx;

  create_sockets(sv);
  grpc_arg a[2];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER;
  a[0].value.integer = static_cast<int>(slice_size);
  a[1].key = const_cast<char*>(GRPC_ARG_RESOURCE_QUOTA);
  a[1].type = GRPC_ARG_POINTER;
  a[1].value.pointer.p = grpc_resource_quota_create("test");
  a[1].value.pointer.vtable = grpc_resource_quota_arg_vtable();
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  f.client_ep = grpc_tcp_create(
      grpc_fd_create(sv[0], "fixture:client", false),
      TcpOptionsFromEndpointConfig(
          grpc_event_engine::experimental::ChannelArgsEndpointConfig(
              grpc_core::ChannelArgs::FromC(&args))),
      "test");
  f.server_ep = grpc_tcp_create(
      grpc_fd_create(sv[1], "fixture:server", false),
      TcpOptionsFromEndpointConfig(
          grpc_event_engine::experimental::ChannelArgsEndpointConfig(
              grpc_core::ChannelArgs::FromC(&args))),
      "test");
  grpc_endpoint_add_to_pollset(f.client_ep, g_pollset);
  grpc_endpoint_add_to_pollset(f.server_ep, g_pollset);
  grpc_resource_quota_unref(
      static_cast<grpc_resource_quota*>(a[1].value.pointer.p));

  return f;
}

static grpc_endpoint_test_config configs[] = {
    {"tcp/tcp_socketpair", create_fixture_tcp_socketpair, clean_up},
};

static void destroy_pollset(void* p, grpc_error_handle /*error*/) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(p));
}

int main(int argc, char** argv) {
  grpc_closure destroyed;
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;
    g_pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(g_pollset, &g_mu);
    grpc_endpoint_tests(configs[0], g_pollset, g_mu);
    run_tests();
    GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, g_pollset,
                      grpc_schedule_on_exec_ctx);
    grpc_pollset_shutdown(g_pollset, &destroyed);

    grpc_core::ExecCtx::Get()->Flush();
  }
  grpc_shutdown();
  gpr_free(g_pollset);

  return 0;
}

#else  // GRPC_POSIX_SOCKET_TCP

int main(int argc, char** argv) { return 1; }

#endif  // GRPC_POSIX_SOCKET_TCP
