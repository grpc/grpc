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

#include "test/core/iomgr/endpoint_tests.h"

#include <stdbool.h>
#include <sys/types.h>

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

#include "src/core/lib/slice/slice_internal.h"
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

static gpr_mu *g_mu;
static grpc_pollset *g_pollset;

size_t count_slices(grpc_slice *slices, size_t nslices, int *current_data) {
  size_t num_bytes = 0;
  size_t i;
  size_t j;
  unsigned char *buf;
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

static grpc_endpoint_test_fixture begin_test(grpc_endpoint_test_config config,
                                             const char *test_name,
                                             size_t slice_size) {
  gpr_log(GPR_INFO, "%s/%s", test_name, config.name);
  return config.create_fixture(slice_size);
}

static void end_test(grpc_endpoint_test_config config) { config.clean_up(); }

static grpc_slice *allocate_blocks(size_t num_bytes, size_t slice_size,
                                   size_t *num_blocks, uint8_t *current_data) {
  size_t nslices = num_bytes / slice_size + (num_bytes % slice_size ? 1 : 0);
  grpc_slice *slices = gpr_malloc(sizeof(grpc_slice) * nslices);
  size_t num_bytes_left = num_bytes;
  size_t i;
  size_t j;
  unsigned char *buf;
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

struct read_and_write_test_state {
  grpc_endpoint *read_ep;
  grpc_endpoint *write_ep;
  size_t target_bytes;
  size_t bytes_read;
  size_t current_write_size;
  size_t bytes_written;
  int current_read_data;
  uint8_t current_write_data;
  int read_done;
  int write_done;
  grpc_slice_buffer incoming;
  grpc_slice_buffer outgoing;
  grpc_closure done_read;
  grpc_closure done_write;
};

static void read_and_write_test_read_handler(grpc_exec_ctx *exec_ctx,
                                             void *data, grpc_error *error) {
  struct read_and_write_test_state *state = data;

  state->bytes_read += count_slices(
      state->incoming.slices, state->incoming.count, &state->current_read_data);
  if (state->bytes_read == state->target_bytes || error != GRPC_ERROR_NONE) {
    gpr_log(GPR_INFO, "Read handler done");
    gpr_mu_lock(g_mu);
    state->read_done = 1 + (error == GRPC_ERROR_NONE);
    GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(g_pollset, NULL));
    gpr_mu_unlock(g_mu);
  } else if (error == GRPC_ERROR_NONE) {
    grpc_endpoint_read(exec_ctx, state->read_ep, &state->incoming,
                       &state->done_read);
  }
}

static void read_and_write_test_write_handler(grpc_exec_ctx *exec_ctx,
                                              void *data, grpc_error *error) {
  struct read_and_write_test_state *state = data;
  grpc_slice *slices = NULL;
  size_t nslices;

  if (error == GRPC_ERROR_NONE) {
    state->bytes_written += state->current_write_size;
    if (state->target_bytes - state->bytes_written <
        state->current_write_size) {
      state->current_write_size = state->target_bytes - state->bytes_written;
    }
    if (state->current_write_size != 0) {
      slices = allocate_blocks(state->current_write_size, 8192, &nslices,
                               &state->current_write_data);
      grpc_slice_buffer_reset_and_unref(&state->outgoing);
      grpc_slice_buffer_addn(&state->outgoing, slices, nslices);
      grpc_endpoint_write(exec_ctx, state->write_ep, &state->outgoing,
                          &state->done_write);
      gpr_free(slices);
      return;
    }
  }

  gpr_log(GPR_INFO, "Write handler done");
  gpr_mu_lock(g_mu);
  state->write_done = 1 + (error == GRPC_ERROR_NONE);
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(g_pollset, NULL));
  gpr_mu_unlock(g_mu);
}

/* Do both reading and writing using the grpc_endpoint API.

   This also includes a test of the shutdown behavior.
 */
static void read_and_write_test(grpc_endpoint_test_config config,
                                size_t num_bytes, size_t write_size,
                                size_t slice_size, bool shutdown) {
  struct read_and_write_test_state state;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(20);
  grpc_endpoint_test_fixture f =
      begin_test(config, "read_and_write_test", slice_size);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_log(GPR_DEBUG, "num_bytes=%" PRIuPTR " write_size=%" PRIuPTR
                     " slice_size=%" PRIuPTR " shutdown=%d",
          num_bytes, write_size, slice_size, shutdown);

  if (shutdown) {
    gpr_log(GPR_INFO, "Start read and write shutdown test");
  } else {
    gpr_log(GPR_INFO, "Start read and write test with %" PRIuPTR
                      " bytes, slice size %" PRIuPTR,
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
  GRPC_CLOSURE_INIT(&state.done_read, read_and_write_test_read_handler, &state,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&state.done_write, read_and_write_test_write_handler,
                    &state, grpc_schedule_on_exec_ctx);
  grpc_slice_buffer_init(&state.outgoing);
  grpc_slice_buffer_init(&state.incoming);

  /* Get started by pretending an initial write completed */
  /* NOTE: Sets up initial conditions so we can have the same write handler
     for the first iteration as for later iterations. It does the right thing
     even when bytes_written is unsigned. */
  state.bytes_written -= state.current_write_size;
  read_and_write_test_write_handler(&exec_ctx, &state, GRPC_ERROR_NONE);
  grpc_exec_ctx_flush(&exec_ctx);

  grpc_endpoint_read(&exec_ctx, state.read_ep, &state.incoming,
                     &state.done_read);

  if (shutdown) {
    gpr_log(GPR_DEBUG, "shutdown read");
    grpc_endpoint_shutdown(
        &exec_ctx, state.read_ep,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Test Shutdown"));
    gpr_log(GPR_DEBUG, "shutdown write");
    grpc_endpoint_shutdown(
        &exec_ctx, state.write_ep,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Test Shutdown"));
  }
  grpc_exec_ctx_flush(&exec_ctx);

  gpr_mu_lock(g_mu);
  while (!state.read_done || !state.write_done) {
    grpc_pollset_worker *worker = NULL;
    GPR_ASSERT(gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline) < 0);
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work",
        grpc_pollset_work(&exec_ctx, g_pollset, &worker,
                          gpr_now(GPR_CLOCK_MONOTONIC), deadline)));
  }
  gpr_mu_unlock(g_mu);
  grpc_exec_ctx_flush(&exec_ctx);

  end_test(config);
  grpc_slice_buffer_destroy_internal(&exec_ctx, &state.outgoing);
  grpc_slice_buffer_destroy_internal(&exec_ctx, &state.incoming);
  grpc_endpoint_destroy(&exec_ctx, state.read_ep);
  grpc_endpoint_destroy(&exec_ctx, state.write_ep);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void inc_on_failure(grpc_exec_ctx *exec_ctx, void *arg,
                           grpc_error *error) {
  gpr_mu_lock(g_mu);
  *(int *)arg += (error != GRPC_ERROR_NONE);
  GPR_ASSERT(GRPC_LOG_IF_ERROR("kick", grpc_pollset_kick(g_pollset, NULL)));
  gpr_mu_unlock(g_mu);
}

static void wait_for_fail_count(grpc_exec_ctx *exec_ctx, int *fail_count,
                                int want_fail_count) {
  grpc_exec_ctx_flush(exec_ctx);
  gpr_mu_lock(g_mu);
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(10);
  while (gpr_time_cmp(gpr_now(deadline.clock_type), deadline) < 0 &&
         *fail_count < want_fail_count) {
    grpc_pollset_worker *worker = NULL;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work",
        grpc_pollset_work(exec_ctx, g_pollset, &worker,
                          gpr_now(deadline.clock_type), deadline)));
    gpr_mu_unlock(g_mu);
    grpc_exec_ctx_flush(exec_ctx);
    gpr_mu_lock(g_mu);
  }
  GPR_ASSERT(*fail_count == want_fail_count);
  gpr_mu_unlock(g_mu);
}

static void multiple_shutdown_test(grpc_endpoint_test_config config) {
  grpc_endpoint_test_fixture f =
      begin_test(config, "multiple_shutdown_test", 128);
  int fail_count = 0;

  grpc_slice_buffer slice_buffer;
  grpc_slice_buffer_init(&slice_buffer);

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_endpoint_add_to_pollset(&exec_ctx, f.client_ep, g_pollset);
  grpc_endpoint_read(&exec_ctx, f.client_ep, &slice_buffer,
                     GRPC_CLOSURE_CREATE(inc_on_failure, &fail_count,
                                         grpc_schedule_on_exec_ctx));
  wait_for_fail_count(&exec_ctx, &fail_count, 0);
  grpc_endpoint_shutdown(&exec_ctx, f.client_ep,
                         GRPC_ERROR_CREATE_FROM_STATIC_STRING("Test Shutdown"));
  wait_for_fail_count(&exec_ctx, &fail_count, 1);
  grpc_endpoint_read(&exec_ctx, f.client_ep, &slice_buffer,
                     GRPC_CLOSURE_CREATE(inc_on_failure, &fail_count,
                                         grpc_schedule_on_exec_ctx));
  wait_for_fail_count(&exec_ctx, &fail_count, 2);
  grpc_slice_buffer_add(&slice_buffer, grpc_slice_from_copied_string("a"));
  grpc_endpoint_write(&exec_ctx, f.client_ep, &slice_buffer,
                      GRPC_CLOSURE_CREATE(inc_on_failure, &fail_count,
                                          grpc_schedule_on_exec_ctx));
  wait_for_fail_count(&exec_ctx, &fail_count, 3);
  grpc_endpoint_shutdown(&exec_ctx, f.client_ep,
                         GRPC_ERROR_CREATE_FROM_STATIC_STRING("Test Shutdown"));
  wait_for_fail_count(&exec_ctx, &fail_count, 3);

  grpc_slice_buffer_destroy_internal(&exec_ctx, &slice_buffer);

  grpc_endpoint_destroy(&exec_ctx, f.client_ep);
  grpc_endpoint_destroy(&exec_ctx, f.server_ep);
  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_endpoint_tests(grpc_endpoint_test_config config,
                         grpc_pollset *pollset, gpr_mu *mu) {
  size_t i;
  g_pollset = pollset;
  g_mu = mu;
  multiple_shutdown_test(config);
  read_and_write_test(config, 10000000, 100000, 8192, false);
  read_and_write_test(config, 1000000, 100000, 1, false);
  read_and_write_test(config, 100000000, 100000, 1, true);
  for (i = 1; i < 1000; i = GPR_MAX(i + 1, i * 5 / 4)) {
    read_and_write_test(config, 40320, i, i, false);
  }
  g_pollset = NULL;
  g_mu = NULL;
}
