/*
 *
 * Copyright 2015, Google Inc.
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

#include "test/core/iomgr/endpoint_tests.h"

#include <sys/types.h>

#include <grpc/support/alloc.h>
#include <grpc/support/slice.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "test/core/util/test_config.h"

/*
   General test notes:

   All tests which write data into an endpoint write i%256 into byte i, which
   is verified by readers.

   In general there are a few interesting things to vary which may lead to
   exercising different codepaths in an implementation:
   1. Total amount of data written to the endpoint
   2. Size of slice allocations
   3. Amount of data we read from or write to the endpoint at once

   The tests here tend to parameterize these where applicable.

*/

static grpc_pollset *g_pollset;

size_t count_slices(gpr_slice *slices, size_t nslices, int *current_data) {
  size_t num_bytes = 0;
  size_t i;
  size_t j;
  unsigned char *buf;
  for (i = 0; i < nslices; ++i) {
    buf = GPR_SLICE_START_PTR(slices[i]);
    for (j = 0; j < GPR_SLICE_LENGTH(slices[i]); ++j) {
      GPR_ASSERT(buf[j] == *current_data);
      *current_data = (*current_data + 1) % 256;
    }
    num_bytes += GPR_SLICE_LENGTH(slices[i]);
  }
  return num_bytes;
}

static grpc_endpoint_test_fixture begin_test(grpc_endpoint_test_config config,
                                             const char *test_name,
                                             size_t slice_size) {
  gpr_log(GPR_INFO, "%s/%s", test_name, config.name);
  return config.create_fixture(slice_size);
}

static void end_test(grpc_endpoint_test_config config) { config.clean_up(); }

static gpr_slice *allocate_blocks(size_t num_bytes, size_t slice_size,
                                  size_t *num_blocks, gpr_uint8 *current_data) {
  size_t nslices = num_bytes / slice_size + (num_bytes % slice_size ? 1 : 0);
  gpr_slice *slices = malloc(sizeof(gpr_slice) * nslices);
  size_t num_bytes_left = num_bytes;
  size_t i;
  size_t j;
  unsigned char *buf;
  *num_blocks = nslices;

  for (i = 0; i < nslices; ++i) {
    slices[i] = gpr_slice_malloc(slice_size > num_bytes_left ? num_bytes_left
                                                             : slice_size);
    num_bytes_left -= GPR_SLICE_LENGTH(slices[i]);
    buf = GPR_SLICE_START_PTR(slices[i]);
    for (j = 0; j < GPR_SLICE_LENGTH(slices[i]); ++j) {
      buf[j] = *current_data;
      (*current_data)++;
    }
  }
  GPR_ASSERT(num_bytes_left == 0);
  return slices;
}

struct read_and_write_test_state {
  grpc_endpoint *read_ep;
  grpc_endpoint *write_ep;
  size_t target_bytes;
  size_t bytes_read;
  size_t current_write_size;
  size_t bytes_written;
  int current_read_data;
  gpr_uint8 current_write_data;
  int read_done;
  int write_done;
  gpr_slice_buffer incoming;
  gpr_slice_buffer outgoing;
  grpc_closure done_read;
  grpc_closure done_write;
};

static void read_and_write_test_read_handler(grpc_exec_ctx *exec_ctx,
                                             void *data, int success) {
  struct read_and_write_test_state *state = data;

  state->bytes_read += count_slices(
      state->incoming.slices, state->incoming.count, &state->current_read_data);
  if (state->bytes_read == state->target_bytes || !success) {
    gpr_log(GPR_INFO, "Read handler done");
    gpr_mu_lock(GRPC_POLLSET_MU(g_pollset));
    state->read_done = 1 + success;
    grpc_pollset_kick(g_pollset, NULL);
    gpr_mu_unlock(GRPC_POLLSET_MU(g_pollset));
  } else if (success) {
    grpc_endpoint_read(exec_ctx, state->read_ep, &state->incoming,
                       &state->done_read);
  }
}

static void read_and_write_test_write_handler(grpc_exec_ctx *exec_ctx,
                                              void *data, int success) {
  struct read_and_write_test_state *state = data;
  gpr_slice *slices = NULL;
  size_t nslices;

  if (success) {
    state->bytes_written += state->current_write_size;
    if (state->target_bytes - state->bytes_written <
        state->current_write_size) {
      state->current_write_size = state->target_bytes - state->bytes_written;
    }
    if (state->current_write_size != 0) {
      slices = allocate_blocks(state->current_write_size, 8192, &nslices,
                               &state->current_write_data);
      gpr_slice_buffer_reset_and_unref(&state->outgoing);
      gpr_slice_buffer_addn(&state->outgoing, slices, nslices);
      grpc_endpoint_write(exec_ctx, state->write_ep, &state->outgoing,
                          &state->done_write);
      free(slices);
      return;
    }
  }

  gpr_log(GPR_INFO, "Write handler done");
  gpr_mu_lock(GRPC_POLLSET_MU(g_pollset));
  state->write_done = 1 + success;
  grpc_pollset_kick(g_pollset, NULL);
  gpr_mu_unlock(GRPC_POLLSET_MU(g_pollset));
}

/* Do both reading and writing using the grpc_endpoint API.

   This also includes a test of the shutdown behavior.
 */
static void read_and_write_test(grpc_endpoint_test_config config,
                                size_t num_bytes, size_t write_size,
                                size_t slice_size, int shutdown) {
  struct read_and_write_test_state state;
  gpr_timespec deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(20);
  grpc_endpoint_test_fixture f =
      begin_test(config, "read_and_write_test", slice_size);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_log(GPR_DEBUG, "num_bytes=%d write_size=%d slice_size=%d shutdown=%d",
          num_bytes, write_size, slice_size, shutdown);

  if (shutdown) {
    gpr_log(GPR_INFO, "Start read and write shutdown test");
  } else {
    gpr_log(GPR_INFO, "Start read and write test with %d bytes, slice size %d",
            num_bytes, slice_size);
  }

  state.read_ep = f.client_ep;
  state.write_ep = f.server_ep;
  state.target_bytes = num_bytes;
  state.bytes_read = 0;
  state.current_write_size = write_size;
  state.bytes_written = 0;
  state.read_done = 0;
  state.write_done = 0;
  state.current_read_data = 0;
  state.current_write_data = 0;
  grpc_closure_init(&state.done_read, read_and_write_test_read_handler, &state);
  grpc_closure_init(&state.done_write, read_and_write_test_write_handler,
                    &state);
  gpr_slice_buffer_init(&state.outgoing);
  gpr_slice_buffer_init(&state.incoming);

  /* Get started by pretending an initial write completed */
  /* NOTE: Sets up initial conditions so we can have the same write handler
     for the first iteration as for later iterations. It does the right thing
     even when bytes_written is unsigned. */
  state.bytes_written -= state.current_write_size;
  read_and_write_test_write_handler(&exec_ctx, &state, 1);
  grpc_exec_ctx_finish(&exec_ctx);

  grpc_endpoint_read(&exec_ctx, state.read_ep, &state.incoming,
                     &state.done_read);

  if (shutdown) {
    gpr_log(GPR_DEBUG, "shutdown read");
    grpc_endpoint_shutdown(&exec_ctx, state.read_ep);
    gpr_log(GPR_DEBUG, "shutdown write");
    grpc_endpoint_shutdown(&exec_ctx, state.write_ep);
  }
  grpc_exec_ctx_finish(&exec_ctx);

  gpr_mu_lock(GRPC_POLLSET_MU(g_pollset));
  while (!state.read_done || !state.write_done) {
    grpc_pollset_worker worker;
    GPR_ASSERT(gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline) < 0);
    grpc_pollset_work(&exec_ctx, g_pollset, &worker,
                      gpr_now(GPR_CLOCK_MONOTONIC), deadline);
  }
  gpr_mu_unlock(GRPC_POLLSET_MU(g_pollset));
  grpc_exec_ctx_finish(&exec_ctx);

  end_test(config);
  gpr_slice_buffer_destroy(&state.outgoing);
  gpr_slice_buffer_destroy(&state.incoming);
  grpc_endpoint_destroy(&exec_ctx, state.read_ep);
  grpc_endpoint_destroy(&exec_ctx, state.write_ep);
  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_endpoint_tests(grpc_endpoint_test_config config,
                         grpc_pollset *pollset) {
  size_t i;
  g_pollset = pollset;
  read_and_write_test(config, 10000000, 100000, 8192, 0);
  read_and_write_test(config, 1000000, 100000, 1, 0);
  read_and_write_test(config, 100000000, 100000, 1, 1);
  for (i = 1; i < 1000; i = GPR_MAX(i + 1, i * 5 / 4)) {
    read_and_write_test(config, 40320, i, i, 0);
  }
  g_pollset = NULL;
}
