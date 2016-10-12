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

void grpc_handshaker_init(const struct grpc_handshaker_vtable* vtable,
                          grpc_handshaker* handshaker) {
  handshaker->vtable = vtable;
}

void grpc_handshaker_destroy(grpc_exec_ctx* exec_ctx,
                             grpc_handshaker* handshaker) {
  handshaker->vtable->destroy(exec_ctx, handshaker);
}

void grpc_handshaker_shutdown(grpc_exec_ctx* exec_ctx,
                              grpc_handshaker* handshaker) {
  handshaker->vtable->shutdown(exec_ctx, handshaker);
}

void grpc_handshaker_do_handshake(grpc_exec_ctx* exec_ctx,
                                  grpc_handshaker* handshaker,
                                  grpc_endpoint* endpoint,
                                  grpc_channel_args* args,
                                  gpr_slice_buffer* read_buffer,
                                  gpr_timespec deadline,
                                  grpc_tcp_server_acceptor* acceptor,
                                  grpc_handshaker_done_cb cb, void* user_data) {
  handshaker->vtable->do_handshake(exec_ctx, handshaker, endpoint, args,
                                   read_buffer, deadline, acceptor, cb,
                                   user_data);
}

//
// grpc_handshake_manager
//

// State used while chaining handshakers.
struct grpc_handshaker_state {
  // The index of the handshaker to invoke next.
  size_t index;
  // The deadline for all handshakers.
  gpr_timespec deadline;
  // The acceptor to call the handshakers with.
  grpc_tcp_server_acceptor* acceptor;
  // The final callback and user_data to invoke after the last handshaker.
  grpc_handshaker_done_cb final_cb;
  void* final_user_data;
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
static void call_next_handshaker(grpc_exec_ctx* exec_ctx,
                                 grpc_endpoint* endpoint,
                                 grpc_channel_args* args,
                                 gpr_slice_buffer* read_buffer, void* user_data,
                                 grpc_error* error) {
  grpc_handshake_manager* mgr = user_data;
  GPR_ASSERT(mgr->state != NULL);
  GPR_ASSERT(mgr->state->index < mgr->count);
  // If we got an error, skip all remaining handshakers and invoke the
  // caller-supplied callback immediately.
  if (error != GRPC_ERROR_NONE) {
    mgr->state->final_cb(exec_ctx, endpoint, args, read_buffer,
                         mgr->state->final_user_data, error);
    return;
  }
  grpc_handshaker_done_cb cb = call_next_handshaker;
  // If this is the last handshaker, use the caller-supplied callback
  // and user_data instead of chaining back to this function again.
  if (mgr->state->index == mgr->count - 1) {
    cb = mgr->state->final_cb;
    user_data = mgr->state->final_user_data;
  }
  // Invoke handshaker.
  grpc_handshaker_do_handshake(
      exec_ctx, mgr->handshakers[mgr->state->index], endpoint, args,
      read_buffer, mgr->state->deadline, mgr->state->acceptor, cb, user_data);
  ++mgr->state->index;
  // If this is the last handshaker, clean up state.
  if (mgr->state->index == mgr->count) {
    gpr_free(mgr->state);
    mgr->state = NULL;
  }
}

void grpc_handshake_manager_do_handshake(
    grpc_exec_ctx* exec_ctx, grpc_handshake_manager* mgr,
    grpc_endpoint* endpoint, const grpc_channel_args* args,
    gpr_timespec deadline, grpc_tcp_server_acceptor* acceptor,
    grpc_handshaker_done_cb cb, void* user_data) {
  grpc_channel_args* args_copy = grpc_channel_args_copy(args);
  gpr_slice_buffer* read_buffer = malloc(sizeof(*read_buffer));
  gpr_slice_buffer_init(read_buffer);
  if (mgr->count == 0) {
    // No handshakers registered, so we just immediately call the done
    // callback with the passed-in endpoint.
    cb(exec_ctx, endpoint, args_copy, read_buffer, user_data, GRPC_ERROR_NONE);
  } else {
    GPR_ASSERT(mgr->state == NULL);
    mgr->state = gpr_malloc(sizeof(struct grpc_handshaker_state));
    memset(mgr->state, 0, sizeof(*mgr->state));
    mgr->state->deadline = deadline;
    mgr->state->acceptor = acceptor;
    mgr->state->final_cb = cb;
    mgr->state->final_user_data = user_data;
    call_next_handshaker(exec_ctx, endpoint, args_copy, read_buffer, mgr,
                         GRPC_ERROR_NONE);
  }
}
