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

#include "src/core/lib/iomgr/tcp_posix.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/iomgr/endpoint_tests.h"
#include "test/core/util/test_config.h"

static gpr_mu* g_mu;
static grpc_pollset* g_pollset;

/*
   General test notes:

   All tests which write data into a socket write i%256 into byte i, which is
   verified by readers.

   In general there are a few interesting things to vary which may lead to
   exercising different codepaths in an implementation:
   1. Total amount of data written to the socket
   2. Size of slice allocations
   3. Amount of data we read from or write to the socket at once

   The tests here tend to parameterize these where applicable.

 */

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

static void read_cb(void* user_data, grpc_error* error) {
  struct read_socket_state* state =
      static_cast<struct read_socket_state*>(user_data);
  size_t read_bytes;
  int current_data;

  GPR_ASSERT(error == GRPC_ERROR_NONE);

  gpr_mu_lock(g_mu);
  current_data = state->read_bytes % 256;
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
    grpc_endpoint_read(state->ep, &state->incoming, &state->read_cb);
    gpr_mu_unlock(g_mu);
  }
}

/* Write to a socket, then read from it using the grpc_tcp API. */
static void read_test(size_t num_bytes, size_t slice_size) {
  int sv[2];
  grpc_endpoint* ep;
  struct read_socket_state state;
  size_t written_bytes;
  grpc_millis deadline =
      grpc_timespec_to_millis_round_up(grpc_timeout_seconds_to_deadline(20));
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_INFO, "Read test of size %" PRIuPTR ", slice size %" PRIuPTR,
          num_bytes, slice_size);

  create_sockets(sv);

  grpc_arg a[1];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER,
  a[0].value.integer = static_cast<int>(slice_size);
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  ep = grpc_tcp_create(grpc_fd_create(sv[1], "read_test"), &args, "test");
  grpc_endpoint_add_to_pollset(ep, g_pollset);

  written_bytes = fill_socket_partial(sv[0], num_bytes);
  gpr_log(GPR_INFO, "Wrote %" PRIuPTR " bytes", written_bytes);

  state.ep = ep;
  state.read_bytes = 0;
  state.target_read_bytes = written_bytes;
  grpc_slice_buffer_init(&state.incoming);
  GRPC_CLOSURE_INIT(&state.read_cb, read_cb, &state, grpc_schedule_on_exec_ctx);

  grpc_endpoint_read(ep, &state.incoming, &state.read_cb);

  gpr_mu_lock(g_mu);
  while (state.read_bytes < state.target_read_bytes) {
    grpc_pollset_worker* worker = nullptr;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(g_pollset, &worker, deadline)));
    gpr_mu_unlock(g_mu);

    gpr_mu_lock(g_mu);
  }
  GPR_ASSERT(state.read_bytes == state.target_read_bytes);
  gpr_mu_unlock(g_mu);

  grpc_slice_buffer_destroy_internal(&state.incoming);
  grpc_endpoint_destroy(ep);
}

/* Write to a socket until it fills up, then read from it using the grpc_tcp
   API. */
static void large_read_test(size_t slice_size) {
  int sv[2];
  grpc_endpoint* ep;
  struct read_socket_state state;
  ssize_t written_bytes;
  grpc_millis deadline =
      grpc_timespec_to_millis_round_up(grpc_timeout_seconds_to_deadline(20));
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_INFO, "Start large read test, slice size %" PRIuPTR, slice_size);

  create_sockets(sv);

  grpc_arg a[1];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER;
  a[0].value.integer = static_cast<int>(slice_size);
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  ep = grpc_tcp_create(grpc_fd_create(sv[1], "large_read_test"), &args, "test");
  grpc_endpoint_add_to_pollset(ep, g_pollset);

  written_bytes = fill_socket(sv[0]);
  gpr_log(GPR_INFO, "Wrote %" PRIuPTR " bytes", written_bytes);

  state.ep = ep;
  state.read_bytes = 0;
  state.target_read_bytes = static_cast<size_t>(written_bytes);
  grpc_slice_buffer_init(&state.incoming);
  GRPC_CLOSURE_INIT(&state.read_cb, read_cb, &state, grpc_schedule_on_exec_ctx);

  grpc_endpoint_read(ep, &state.incoming, &state.read_cb);

  gpr_mu_lock(g_mu);
  while (state.read_bytes < state.target_read_bytes) {
    grpc_pollset_worker* worker = nullptr;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(g_pollset, &worker, deadline)));
    gpr_mu_unlock(g_mu);

    gpr_mu_lock(g_mu);
  }
  GPR_ASSERT(state.read_bytes == state.target_read_bytes);
  gpr_mu_unlock(g_mu);

  grpc_slice_buffer_destroy_internal(&state.incoming);
  grpc_endpoint_destroy(ep);
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
                       grpc_error* error) {
  struct write_socket_state* state =
      static_cast<struct write_socket_state*>(user_data);
  gpr_log(GPR_INFO, "Write done callback called");
  gpr_mu_lock(g_mu);
  gpr_log(GPR_INFO, "Signalling write done");
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
                          grpc_timespec_to_millis_round_up(
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

/* Write to a socket using the grpc_tcp API, then drain it directly.
   Note that if the write does not complete immediately we need to drain the
   socket in parallel with the read. */
static void write_test(size_t num_bytes, size_t slice_size) {
  int sv[2];
  grpc_endpoint* ep;
  struct write_socket_state state;
  size_t num_blocks;
  grpc_slice* slices;
  uint8_t current_data = 0;
  grpc_slice_buffer outgoing;
  grpc_closure write_done_closure;
  grpc_millis deadline =
      grpc_timespec_to_millis_round_up(grpc_timeout_seconds_to_deadline(20));
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_INFO,
          "Start write test with %" PRIuPTR " bytes, slice size %" PRIuPTR,
          num_bytes, slice_size);

  create_sockets(sv);

  grpc_arg a[1];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER,
  a[0].value.integer = static_cast<int>(slice_size);
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  ep = grpc_tcp_create(grpc_fd_create(sv[1], "write_test"), &args, "test");
  grpc_endpoint_add_to_pollset(ep, g_pollset);

  state.ep = ep;
  state.write_done = 0;

  slices = allocate_blocks(num_bytes, slice_size, &num_blocks, &current_data);

  grpc_slice_buffer_init(&outgoing);
  grpc_slice_buffer_addn(&outgoing, slices, num_blocks);
  GRPC_CLOSURE_INIT(&write_done_closure, write_done, &state,
                    grpc_schedule_on_exec_ctx);

  grpc_endpoint_write(ep, &outgoing, &write_done_closure);
  drain_socket_blocking(sv[0], num_bytes, num_bytes);
  gpr_mu_lock(g_mu);
  for (;;) {
    grpc_pollset_worker* worker = nullptr;
    if (state.write_done) {
      break;
    }
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(g_pollset, &worker, deadline)));
    gpr_mu_unlock(g_mu);

    gpr_mu_lock(g_mu);
  }
  gpr_mu_unlock(g_mu);

  grpc_slice_buffer_destroy_internal(&outgoing);
  grpc_endpoint_destroy(ep);
  gpr_free(slices);
}

void on_fd_released(void* arg, grpc_error* errors) {
  int* done = static_cast<int*>(arg);
  *done = 1;
  GPR_ASSERT(
      GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(g_pollset, nullptr)));
}

/* Do a read_test, then release fd and try to read/write again. Verify that
   grpc_tcp_fd() is available before the fd is released. */
static void release_fd_test(size_t num_bytes, size_t slice_size) {
  int sv[2];
  grpc_endpoint* ep;
  struct read_socket_state state;
  size_t written_bytes;
  int fd;
  grpc_millis deadline =
      grpc_timespec_to_millis_round_up(grpc_timeout_seconds_to_deadline(20));
  grpc_core::ExecCtx exec_ctx;
  grpc_closure fd_released_cb;
  int fd_released_done = 0;
  GRPC_CLOSURE_INIT(&fd_released_cb, &on_fd_released, &fd_released_done,
                    grpc_schedule_on_exec_ctx);

  gpr_log(GPR_INFO,
          "Release fd read_test of size %" PRIuPTR ", slice size %" PRIuPTR,
          num_bytes, slice_size);

  create_sockets(sv);

  grpc_arg a[1];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER;
  a[0].value.integer = static_cast<int>(slice_size);
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  ep = grpc_tcp_create(grpc_fd_create(sv[1], "read_test"), &args, "test");
  GPR_ASSERT(grpc_tcp_fd(ep) == sv[1] && sv[1] >= 0);
  grpc_endpoint_add_to_pollset(ep, g_pollset);

  written_bytes = fill_socket_partial(sv[0], num_bytes);
  gpr_log(GPR_INFO, "Wrote %" PRIuPTR " bytes", written_bytes);

  state.ep = ep;
  state.read_bytes = 0;
  state.target_read_bytes = written_bytes;
  grpc_slice_buffer_init(&state.incoming);
  GRPC_CLOSURE_INIT(&state.read_cb, read_cb, &state, grpc_schedule_on_exec_ctx);

  grpc_endpoint_read(ep, &state.incoming, &state.read_cb);

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

  grpc_slice_buffer_destroy_internal(&state.incoming);
  grpc_tcp_destroy_and_release_fd(ep, &fd, &fd_released_cb);
  grpc_core::ExecCtx::Get()->Flush();
  gpr_mu_lock(g_mu);
  while (!fd_released_done) {
    grpc_pollset_worker* worker = nullptr;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work", grpc_pollset_work(g_pollset, &worker, deadline)));
    gpr_log(GPR_DEBUG, "wakeup: fd_released_done=%d", fd_released_done);
  }
  gpr_mu_unlock(g_mu);
  GPR_ASSERT(fd_released_done == 1);
  GPR_ASSERT(fd == sv[1]);

  written_bytes = fill_socket_partial(sv[0], num_bytes);
  drain_socket_blocking(fd, written_bytes, written_bytes);
  written_bytes = fill_socket_partial(fd, num_bytes);
  drain_socket_blocking(sv[0], written_bytes, written_bytes);
  close(fd);
}

void run_tests(void) {
  size_t i = 0;

  read_test(100, 8192);
  read_test(10000, 8192);
  read_test(10000, 137);
  read_test(10000, 1);
  large_read_test(8192);
  large_read_test(1);

  write_test(100, 8192);
  write_test(100, 1);
  write_test(100000, 8192);
  write_test(100000, 1);
  write_test(100000, 137);

  for (i = 1; i < 1000; i = GPR_MAX(i + 1, i * 5 / 4)) {
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
  grpc_resource_quota* resource_quota =
      grpc_resource_quota_create("tcp_posix_test_socketpair");
  grpc_arg a[1];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER;
  a[0].value.integer = static_cast<int>(slice_size);
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  f.client_ep =
      grpc_tcp_create(grpc_fd_create(sv[0], "fixture:client"), &args, "test");
  f.server_ep =
      grpc_tcp_create(grpc_fd_create(sv[1], "fixture:server"), &args, "test");
  grpc_resource_quota_unref_internal(resource_quota);
  grpc_endpoint_add_to_pollset(f.client_ep, g_pollset);
  grpc_endpoint_add_to_pollset(f.server_ep, g_pollset);

  return f;
}

static grpc_endpoint_test_config configs[] = {
    {"tcp/tcp_socketpair", create_fixture_tcp_socketpair, clean_up},
};

static void destroy_pollset(void* p, grpc_error* error) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(p));
}

int main(int argc, char** argv) {
  grpc_closure destroyed;
  grpc_test_init(argc, argv);
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

#else /* GRPC_POSIX_SOCKET */

int main(int argc, char** argv) { return 1; }

#endif /* GRPC_POSIX_SOCKET */
