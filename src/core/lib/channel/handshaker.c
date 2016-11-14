/*
 *
 * Copyright 2016, Google Inc.
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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"

//
// grpc_handshaker
//

void grpc_handshaker_init(const grpc_handshaker_vtable* vtable,
                          grpc_handshaker* handshaker) {
  handshaker->vtable = vtable;
}

static void grpc_handshaker_destroy(grpc_exec_ctx* exec_ctx,
                                    grpc_handshaker* handshaker) {
  handshaker->vtable->destroy(exec_ctx, handshaker);
}

static void grpc_handshaker_shutdown(grpc_exec_ctx* exec_ctx,
                                     grpc_handshaker* handshaker) {
  handshaker->vtable->shutdown(exec_ctx, handshaker);
}

static void grpc_handshaker_do_handshake(grpc_exec_ctx* exec_ctx,
                                         grpc_handshaker* handshaker,
                                         gpr_timespec deadline,
                                         grpc_tcp_server_acceptor* acceptor,
                                         grpc_closure* on_handshake_done,
                                         grpc_handshaker_args* args) {
  handshaker->vtable->do_handshake(exec_ctx, handshaker, deadline, acceptor,
                                   on_handshake_done, args);
}

//
// grpc_handshake_manager
//

// State used while chaining handshakers.
struct grpc_handshaker_state {
  // The index of the handshaker to invoke next and the closure to invoke it.
  size_t index;
  grpc_closure call_next_handshaker;
  // The deadline for all handshakers.
  gpr_timespec deadline;
  // The acceptor to call the handshakers with.
  grpc_tcp_server_acceptor* acceptor;
  // The final callback and user_data to invoke after the last handshaker.
  grpc_closure on_handshake_done;
  void* user_data;
};

struct grpc_handshake_manager {
  // An array of handshakers added via grpc_handshake_manager_add().
  size_t count;
  grpc_handshaker** handshakers;
  // State used while chaining handshakers.
  struct grpc_handshaker_state* state;
};

grpc_handshake_manager* grpc_handshake_manager_create() {
  grpc_handshake_manager* mgr = gpr_malloc(sizeof(grpc_handshake_manager));
  memset(mgr, 0, sizeof(*mgr));
  return mgr;
}

static bool is_power_of_2(size_t n) { return (n & (n - 1)) == 0; }

void grpc_handshake_manager_add(grpc_handshake_manager* mgr,
                                grpc_handshaker* handshaker) {
  // To avoid allocating memory for each handshaker we add, we double
  // the number of elements every time we need more.
  size_t realloc_count = 0;
  if (mgr->count == 0) {
    realloc_count = 2;
  } else if (mgr->count >= 2 && is_power_of_2(mgr->count)) {
    realloc_count = mgr->count * 2;
  }
  if (realloc_count > 0) {
    mgr->handshakers =
        gpr_realloc(mgr->handshakers, realloc_count * sizeof(grpc_handshaker*));
  }
  mgr->handshakers[mgr->count++] = handshaker;
}

void grpc_handshake_manager_destroy(grpc_exec_ctx* exec_ctx,
                                    grpc_handshake_manager* mgr) {
  for (size_t i = 0; i < mgr->count; ++i) {
    grpc_handshaker_destroy(exec_ctx, mgr->handshakers[i]);
  }
  gpr_free(mgr->handshakers);
  gpr_free(mgr);
}

void grpc_handshake_manager_shutdown(grpc_exec_ctx* exec_ctx,
                                     grpc_handshake_manager* mgr) {
  for (size_t i = 0; i < mgr->count; ++i) {
    grpc_handshaker_shutdown(exec_ctx, mgr->handshakers[i]);
  }
  if (mgr->state != NULL) {
    gpr_free(mgr->state);
    mgr->state = NULL;
  }
}

// A function used as the handshaker-done callback when chaining
// handshakers together.
static void call_next_handshaker(grpc_exec_ctx* exec_ctx, void* arg,
                                 grpc_error* error) {
  grpc_handshaker_args* args = arg;
  grpc_handshake_manager* mgr = args->user_data;
  GPR_ASSERT(mgr->state != NULL);
  GPR_ASSERT(mgr->state->index <= mgr->count);
  // If we got an error, skip all remaining handshakers and invoke the
  // caller-supplied callback immediately.
  // Otherwise, if this is the last handshaker, then call the final
  // callback instead of chaining back to this function again.
  if (error != GRPC_ERROR_NONE || mgr->state->index == mgr->count) {
    args->user_data = mgr->state->user_data;
    grpc_exec_ctx_sched(exec_ctx, &mgr->state->on_handshake_done,
                        GRPC_ERROR_REF(error), NULL);
    return;
  }
  // Call the next handshaker.
  grpc_handshaker_do_handshake(
      exec_ctx, mgr->handshakers[mgr->state->index], mgr->state->deadline,
      mgr->state->acceptor, &mgr->state->call_next_handshaker, args);
  // If this is the last handshaker, clean up state.
  if (mgr->state->index == mgr->count) {
    gpr_free(mgr->state);
    mgr->state = NULL;
  } else {
    ++mgr->state->index;
  }
}

void grpc_handshake_manager_do_handshake(
    grpc_exec_ctx* exec_ctx, grpc_handshake_manager* mgr,
    grpc_endpoint* endpoint, const grpc_channel_args* channel_args,
    gpr_timespec deadline, grpc_tcp_server_acceptor* acceptor,
    grpc_iomgr_cb_func on_handshake_done, void* user_data) {
  // Construct handshaker args.  These will be passed through all
  // handshakers and eventually be freed by the final callback.
  grpc_handshaker_args* args = gpr_malloc(sizeof(*args));
  args->endpoint = endpoint;
  args->args = grpc_channel_args_copy(channel_args);
  args->read_buffer = gpr_malloc(sizeof(*args->read_buffer));
  grpc_slice_buffer_init(args->read_buffer);
  // Construct state.
  GPR_ASSERT(mgr->state == NULL);
  mgr->state = gpr_malloc(sizeof(struct grpc_handshaker_state));
  memset(mgr->state, 0, sizeof(*mgr->state));
  grpc_closure_init(&mgr->state->call_next_handshaker, call_next_handshaker,
                    args);
  mgr->state->deadline = deadline;
  mgr->state->acceptor = acceptor;
  grpc_closure_init(&mgr->state->on_handshake_done, on_handshake_done, args);
  // While chaining between handshakers, we use args->user_data to
  // store a pointer to the handshake manager.  This will be
  // changed to point to the caller-supplied user_data before calling
  // the final callback.
  args->user_data = mgr;
  mgr->state->user_data = user_data;
  call_next_handshaker(exec_ctx, args, GRPC_ERROR_NONE);
}
