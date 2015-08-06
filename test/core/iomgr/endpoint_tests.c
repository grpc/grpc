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

size_t count_and_unref_slices(gpr_slice *slices, size_t nslices,
                              int *current_data) {
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
    gpr_slice_unref(slices[i]);
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
                                  size_t *num_blocks, int *current_data) {
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
      *current_data = (*current_data + 1) % 256;
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
  int current_write_data;
  int read_done;
  int write_done;
};

static void read_and_write_test_read_handler(void *data, gpr_slice *slices,
                                             size_t nslices,
                                             grpc_endpoint_cb_status error) {
  struct read_and_write_test_state *state = data;
  GPR_ASSERT(error != GRPC_ENDPOINT_CB_ERROR);
  if (error == GRPC_ENDPOINT_CB_SHUTDOWN) {
    gpr_log(GPR_INFO, "Read handler shutdown");
    gpr_mu_lock(GRPC_POLLSET_MU(g_pollset));
    state->read_done = 1;
    grpc_pollset_kick(g_pollset, NULL);
    gpr_mu_unlock(GRPC_POLLSET_MU(g_pollset));
    return;
  }

  state->bytes_read +=
      count_and_unref_slices(slices, nslices, &state->current_read_data);
  if (state->bytes_read == state->target_bytes) {
    gpr_log(GPR_INFO, "Read handler done");
    gpr_mu_lock(GRPC_POLLSET_MU(g_pollset));
    state->read_done = 1;
    grpc_pollset_kick(g_pollset, NULL);
    gpr_mu_unlock(GRPC_POLLSET_MU(g_pollset));
  } else {
    grpc_endpoint_notify_on_read(state->read_ep,
                                 read_and_write_test_read_handler, data);
  }
}

static void read_and_write_test_write_handler(void *data,
                                              grpc_endpoint_cb_status error) {
  struct read_and_write_test_state *state = data;
  gpr_slice *slices = NULL;
  size_t nslices;
  grpc_endpoint_write_status write_status;

  GPR_ASSERT(error != GRPC_ENDPOINT_CB_ERROR);

  gpr_log(GPR_DEBUG, "%s: error=%d", "read_and_write_test_write_handler",
          error);

  if (error == GRPC_ENDPOINT_CB_SHUTDOWN) {
    gpr_log(GPR_INFO, "Write handler shutdown");
    gpr_mu_lock(GRPC_POLLSET_MU(g_pollset));
    state->write_done = 1;
    grpc_pollset_kick(g_pollset, NULL);
    gpr_mu_unlock(GRPC_POLLSET_MU(g_pollset));
    return;
  }

  for (;;) {
    /* Need to do inline writes until they don't succeed synchronously or we
       finish writing */
    state->bytes_written += state->current_write_size;
    if (state->target_bytes - state->bytes_written <
        state->current_write_size) {
      state->current_write_size = state->target_bytes - state->bytes_written;
    }
    if (state->current_write_size == 0) {
      break;
    }

    slices = allocate_blocks(state->current_write_size, 8192, &nslices,
                             &state->current_write_data);
    write_status =
        grpc_endpoint_write(state->write_ep, slices, nslices,
                            read_and_write_test_write_handler, state);
    gpr_log(GPR_DEBUG, "write_status=%d", write_status);
    GPR_ASSERT(write_status != GRPC_ENDPOINT_WRITE_ERROR);
    free(slices);
    if (write_status == GRPC_ENDPOINT_WRITE_PENDING) {
      return;
    }
  }
  GPR_ASSERT(state->bytes_written == state->target_bytes);

  gpr_log(GPR_INFO, "Write handler done");
  gpr_mu_lock(GRPC_POLLSET_MU(g_pollset));
  state->write_done = 1;
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

  /* Get started by pretending an initial write completed */
  /* NOTE: Sets up initial conditions so we can have the same write handler
     for the first iteration as for later iterations. It does the right thing
     even when bytes_written is unsigned. */
  state.bytes_written -= state.current_write_size;
  read_and_write_test_write_handler(&state, GRPC_ENDPOINT_CB_OK);

  grpc_endpoint_notify_on_read(state.read_ep, read_and_write_test_read_handler,
                               &state);

  if (shutdown) {
    gpr_log(GPR_DEBUG, "shutdown read");
    grpc_endpoint_shutdown(state.read_ep);
    gpr_log(GPR_DEBUG, "shutdown write");
    grpc_endpoint_shutdown(state.write_ep);
  }

  gpr_mu_lock(GRPC_POLLSET_MU(g_pollset));
  while (!state.read_done || !state.write_done) {
    grpc_pollset_worker worker;
    GPR_ASSERT(gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline) < 0);
    grpc_pollset_work(g_pollset, &worker, deadline);
  }
  gpr_mu_unlock(GRPC_POLLSET_MU(g_pollset));

  grpc_endpoint_destroy(state.read_ep);
  grpc_endpoint_destroy(state.write_ep);
  end_test(config);
}

struct timeout_test_state {
  int io_done;
};

typedef struct {
  int done;
  grpc_endpoint *ep;
} shutdown_during_write_test_state;

static void shutdown_during_write_test_read_handler(
    void *user_data, gpr_slice *slices, size_t nslices,
    grpc_endpoint_cb_status error) {
  size_t i;
  shutdown_during_write_test_state *st = user_data;

  for (i = 0; i < nslices; i++) {
    gpr_slice_unref(slices[i]);
  }

  if (error != GRPC_ENDPOINT_CB_OK) {
    grpc_endpoint_destroy(st->ep);
    gpr_mu_lock(GRPC_POLLSET_MU(g_pollset));
    st->done = error;
    grpc_pollset_kick(g_pollset, NULL);
    gpr_mu_unlock(GRPC_POLLSET_MU(g_pollset));
  } else {
    grpc_endpoint_notify_on_read(
        st->ep, shutdown_during_write_test_read_handler, user_data);
  }
}

static void shutdown_during_write_test_write_handler(
    void *user_data, grpc_endpoint_cb_status error) {
  shutdown_during_write_test_state *st = user_data;
  gpr_log(GPR_INFO, "shutdown_during_write_test_write_handler: error = %d",
          error);
  if (error == 0) {
    /* This happens about 0.5% of the time when run under TSAN, and is entirely
       legitimate, but means we aren't testing the path we think we are. */
    /* TODO(klempner): Change this test to retry the write in that case */
    gpr_log(GPR_ERROR,
            "shutdown_during_write_test_write_handler completed unexpectedly");
  }
  gpr_mu_lock(GRPC_POLLSET_MU(g_pollset));
  st->done = 1;
  grpc_pollset_kick(g_pollset, NULL);
  gpr_mu_unlock(GRPC_POLLSET_MU(g_pollset));
}

static void shutdown_during_write_test(grpc_endpoint_test_config config,
                                       size_t slice_size) {
  /* test that shutdown with a pending write creates no leaks */
  gpr_timespec deadline;
  size_t size;
  size_t nblocks;
  int current_data = 1;
  shutdown_during_write_test_state read_st;
  shutdown_during_write_test_state write_st;
  gpr_slice *slices;
  grpc_endpoint_test_fixture f =
      begin_test(config, "shutdown_during_write_test", slice_size);

  gpr_log(GPR_INFO, "testing shutdown during a write");

  read_st.ep = f.client_ep;
  write_st.ep = f.server_ep;
  read_st.done = 0;
  write_st.done = 0;

  grpc_endpoint_notify_on_read(
      read_st.ep, shutdown_during_write_test_read_handler, &read_st);
  for (size = 1;; size *= 2) {
    slices = allocate_blocks(size, 1, &nblocks, &current_data);
    switch (grpc_endpoint_write(write_st.ep, slices, nblocks,
                                shutdown_during_write_test_write_handler,
                                &write_st)) {
      case GRPC_ENDPOINT_WRITE_DONE:
        break;
      case GRPC_ENDPOINT_WRITE_ERROR:
        gpr_log(GPR_ERROR, "error writing");
        abort();
      case GRPC_ENDPOINT_WRITE_PENDING:
        grpc_endpoint_shutdown(write_st.ep);
        deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(10);
        gpr_mu_lock(GRPC_POLLSET_MU(g_pollset));
        while (!write_st.done) {
          grpc_pollset_worker worker;
          GPR_ASSERT(gpr_time_cmp(gpr_now(deadline.clock_type), deadline) < 0);
          grpc_pollset_work(g_pollset, &worker, deadline);
        }
        gpr_mu_unlock(GRPC_POLLSET_MU(g_pollset));
        grpc_endpoint_destroy(write_st.ep);
        gpr_mu_lock(GRPC_POLLSET_MU(g_pollset));
        while (!read_st.done) {
          grpc_pollset_worker worker;
          GPR_ASSERT(gpr_time_cmp(gpr_now(deadline.clock_type), deadline) < 0);
          grpc_pollset_work(g_pollset, &worker, deadline);
        }
        gpr_mu_unlock(GRPC_POLLSET_MU(g_pollset));
        gpr_free(slices);
        end_test(config);
        return;
    }
    gpr_free(slices);
  }

  gpr_log(GPR_ERROR, "should never reach here");
  abort();
}

void grpc_endpoint_tests(grpc_endpoint_test_config config,
                         grpc_pollset *pollset) {
  g_pollset = pollset;
  read_and_write_test(config, 10000000, 100000, 8192, 0);
  read_and_write_test(config, 1000000, 100000, 1, 0);
  read_and_write_test(config, 100000000, 100000, 1, 1);
  shutdown_during_write_test(config, 1000);
  g_pollset = NULL;
}
