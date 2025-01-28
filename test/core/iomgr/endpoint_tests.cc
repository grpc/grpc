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

#include "test/core/iomgr/endpoint_tests.h"

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/time.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/types.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/crash.h"
#include "src/core/util/time.h"
#include "src/core/util/useful.h"
#include "test/core/test_util/test_config.h"

//
// General test notes:

// All tests which write data into an endpoint write i%256 into byte i, which
// is verified by readers.

// In general there are a few interesting things to vary which may lead to
// exercising different codepaths in an implementation:
// 1. Total amount of data written to the endpoint
// 2. Size of slice allocations
// 3. Amount of data we read from or write to the endpoint at once

// The tests here tend to parameterize these where applicable.

//

static gpr_mu* g_mu;
static grpc_pollset* g_pollset;

size_t count_slices(grpc_slice* slices, size_t nslices, int* current_data) {
  size_t num_bytes = 0;
  size_t i;
  size_t j;
  unsigned char* buf;
  for (i = 0; i < nslices; ++i) {
    buf = GRPC_SLICE_START_PTR(slices[i]);
    for (j = 0; j < GRPC_SLICE_LENGTH(slices[i]); ++j) {
      CHECK(buf[j] == *current_data);
      *current_data = (*current_data + 1) % 256;
    }
    num_bytes += GRPC_SLICE_LENGTH(slices[i]);
  }
  return num_bytes;
}

static grpc_endpoint_test_fixture begin_test(grpc_endpoint_test_config config,
                                             const char* test_name,
                                             size_t slice_size) {
  LOG(INFO) << test_name << "/" << config.name;
  return config.create_fixture(slice_size);
}

static void end_test(grpc_endpoint_test_config config) { config.clean_up(); }

static grpc_slice* allocate_blocks(size_t num_bytes, size_t slice_size,
                                   size_t* num_blocks, uint8_t* current_data) {
  size_t nslices = (num_bytes / slice_size) + (num_bytes % slice_size ? 1 : 0);
  grpc_slice* slices =
      static_cast<grpc_slice*>(gpr_malloc(sizeof(grpc_slice) * nslices));
  size_t num_bytes_left = num_bytes;
  size_t i;
  size_t j;
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
  CHECK_EQ(num_bytes_left, 0u);
  return slices;
}

struct read_and_write_test_state {
  grpc_core::Mutex ep_mu;  // Guards read_ep and write_ep.
  grpc_endpoint* read_ep;
  grpc_endpoint* write_ep;
  size_t target_bytes;
  size_t bytes_read;
  size_t current_write_size;
  size_t bytes_written;
  int current_read_data;
  uint8_t current_write_data;
  int read_done;
  int write_done;
  int max_write_frame_size;
  grpc_slice_buffer incoming;
  grpc_slice_buffer outgoing;
  grpc_closure done_read;
  grpc_closure done_write;
  grpc_closure read_scheduler;
  grpc_closure write_scheduler;
};

static void read_scheduler(void* data, grpc_error_handle error) {
  struct read_and_write_test_state* state =
      static_cast<struct read_and_write_test_state*>(data);
  if (error.ok() && state->bytes_read < state->target_bytes) {
    grpc_core::MutexLock lock(&state->ep_mu);
    if (state->read_ep != nullptr) {
      grpc_endpoint_read(state->read_ep, &state->incoming, &state->done_read,
                         /*urgent=*/false, /*min_progress_size=*/1);
      return;
    }
  }
  VLOG(2) << "Read handler done";
  gpr_mu_lock(g_mu);
  state->read_done = 1 + error.ok();
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(g_pollset, nullptr));
  gpr_mu_unlock(g_mu);
}

static void read_and_write_test_read_handler(void* data,
                                             grpc_error_handle error) {
  struct read_and_write_test_state* state =
      static_cast<struct read_and_write_test_state*>(data);
  if (error.ok()) {
    state->bytes_read +=
        count_slices(state->incoming.slices, state->incoming.count,
                     &state->current_read_data);
  }
  // We perform many reads one after another. If grpc_endpoint_read and the
  // read_handler are both run inline, we might end up growing the stack
  // beyond the limit. Schedule the read on ExecCtx to avoid this.
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, &state->read_scheduler,
                          std::move(error));
}

static void write_scheduler(void* data, grpc_error_handle error) {
  struct read_and_write_test_state* state =
      static_cast<struct read_and_write_test_state*>(data);
  if (error.ok() && state->current_write_size != 0) {
    grpc_core::MutexLock lock(&state->ep_mu);
    if (state->write_ep != nullptr) {
      grpc_endpoint_write(state->write_ep, &state->outgoing, &state->done_write,
                          nullptr,
                          /*max_frame_size=*/state->max_write_frame_size);
      return;
    }
  }
  VLOG(2) << "Write handler done";
  gpr_mu_lock(g_mu);
  state->write_done = 1 + error.ok();
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(g_pollset, nullptr));
  gpr_mu_unlock(g_mu);
}

static void read_and_write_test_write_handler(void* data,
                                              grpc_error_handle error) {
  struct read_and_write_test_state* state =
      static_cast<struct read_and_write_test_state*>(data);
  if (error.ok()) {
    state->bytes_written += state->current_write_size;
    if (state->target_bytes - state->bytes_written <
        state->current_write_size) {
      state->current_write_size = state->target_bytes - state->bytes_written;
    }
    if (state->current_write_size != 0) {
      size_t nslices;
      grpc_slice* slices =
          allocate_blocks(state->current_write_size, 8192, &nslices,
                          &state->current_write_data);
      grpc_slice_buffer_reset_and_unref(&state->outgoing);
      grpc_slice_buffer_addn(&state->outgoing, slices, nslices);
      gpr_free(slices);
    }
  }
  // We perform many writes one after another. If grpc_endpoint_write and
  // the write_handler are both run inline, we might end up growing the
  // stack beyond the limit. Schedule the write on ExecCtx to avoid this.
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, &state->write_scheduler,
                          std::move(error));
}

// Do both reading and writing using the grpc_endpoint API.

// This also includes a test of the shutdown behavior.
//
static void read_and_write_test(grpc_endpoint_test_config config,
                                size_t num_bytes, size_t write_size,
                                size_t slice_size, int max_write_frame_size,
                                bool shutdown) {
  struct read_and_write_test_state state;
  grpc_endpoint_test_fixture f =
      begin_test(config, "read_and_write_test", slice_size);
  grpc_core::ExecCtx exec_ctx;
  auto deadline = grpc_core::Timestamp::FromTimespecRoundUp(
      grpc_timeout_seconds_to_deadline(300));
  VLOG(2) << "num_bytes=" << num_bytes << " write_size=" << write_size
          << " slice_size=" << slice_size << " shutdown=" << shutdown;

  if (shutdown) {
    LOG(INFO) << "Start read and write shutdown test";
  } else {
    LOG(INFO) << "Start read and write test with " << num_bytes
              << " bytes, slice size " << slice_size;
  }

  state.read_ep = f.client_ep;
  state.write_ep = f.server_ep;
  state.target_bytes = num_bytes;
  state.bytes_read = 0;
  state.current_write_size = write_size;
  state.max_write_frame_size = max_write_frame_size;
  state.bytes_written = 0;
  state.read_done = 0;
  state.write_done = 0;
  state.current_read_data = 0;
  state.current_write_data = 0;
  GRPC_CLOSURE_INIT(&state.read_scheduler, read_scheduler, &state,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&state.done_read, read_and_write_test_read_handler, &state,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&state.write_scheduler, write_scheduler, &state,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&state.done_write, read_and_write_test_write_handler,
                    &state, grpc_schedule_on_exec_ctx);
  grpc_slice_buffer_init(&state.outgoing);
  grpc_slice_buffer_init(&state.incoming);

  // Get started by pretending an initial write completed
  // NOTE: Sets up initial conditions so we can have the same write handler
  // for the first iteration as for later iterations. It does the right thing
  // even when bytes_written is unsigned.
  state.bytes_written -= state.current_write_size;
  read_and_write_test_write_handler(&state, absl::OkStatus());
  grpc_core::ExecCtx::Get()->Flush();

  grpc_endpoint_read(state.read_ep, &state.incoming, &state.done_read,
                     /*urgent=*/false, /*min_progress_size=*/1);
  if (shutdown) {
    grpc_core::MutexLock lock(&state.ep_mu);
    VLOG(2) << "shutdown read";
    grpc_endpoint_destroy(state.read_ep);
    state.read_ep = nullptr;
    VLOG(2) << "shutdown write";
    grpc_endpoint_destroy(state.write_ep);
    state.write_ep = nullptr;
  }
  grpc_core::ExecCtx::Get()->Flush();

  gpr_mu_lock(g_mu);
  while (!state.read_done || !state.write_done) {
    grpc_pollset_worker* worker = nullptr;
    CHECK(grpc_core::Timestamp::Now() < deadline);
    CHECK(GRPC_LOG_IF_ERROR("pollset_work",
                            grpc_pollset_work(g_pollset, &worker, deadline)));
  }
  gpr_mu_unlock(g_mu);
  grpc_core::ExecCtx::Get()->Flush();

  end_test(config);
  grpc_slice_buffer_destroy(&state.outgoing);
  grpc_slice_buffer_destroy(&state.incoming);
  if (!shutdown) {
    grpc_endpoint_destroy(state.read_ep);
    grpc_endpoint_destroy(state.write_ep);
  }
}

static void inc_on_failure(void* arg, grpc_error_handle error) {
  gpr_mu_lock(g_mu);
  *static_cast<int*>(arg) += (!error.ok());
  CHECK(GRPC_LOG_IF_ERROR("kick", grpc_pollset_kick(g_pollset, nullptr)));
  gpr_mu_unlock(g_mu);
}

static void wait_for_fail_count(int* fail_count, int want_fail_count) {
  grpc_core::ExecCtx::Get()->Flush();
  gpr_mu_lock(g_mu);
  grpc_core::Timestamp deadline = grpc_core::Timestamp::FromTimespecRoundUp(
      grpc_timeout_seconds_to_deadline(10));
  while (grpc_core::Timestamp::Now() < deadline &&
         *fail_count < want_fail_count) {
    grpc_pollset_worker* worker = nullptr;
    CHECK(GRPC_LOG_IF_ERROR("pollset_work",
                            grpc_pollset_work(g_pollset, &worker, deadline)));
    gpr_mu_unlock(g_mu);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(g_mu);
  }
  CHECK(*fail_count == want_fail_count);
  gpr_mu_unlock(g_mu);
}

void grpc_endpoint_tests(grpc_endpoint_test_config config,
                         grpc_pollset* pollset, gpr_mu* mu) {
  size_t i;
  g_pollset = pollset;
  g_mu = mu;
  for (int i = 1; i <= 10000; i = i * 10) {
    read_and_write_test(config, 10000000, 100000, 8192, i, false);
    read_and_write_test(config, 1000000, 100000, 1, i, false);
    read_and_write_test(config, 100000000, 100000, 1, i, true);
  }
  for (i = 1; i < 1000; i = std::max(i + 1, i * 5 / 4)) {
    read_and_write_test(config, 40320, i, i, i, false);
  }
  g_pollset = nullptr;
  g_mu = nullptr;
}
