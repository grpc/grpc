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

#include <grpc/impl/codegen/alloc.h>
#include <grpc/impl/codegen/log.h>

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
                                  gpr_timespec deadline,
                                  grpc_handshaker_done_cb cb, void* arg) {
  handshaker->vtable->do_handshake(exec_ctx, handshaker, endpoint, deadline, cb,
                                   arg);
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
  // The final callback and arg to invoke after the last handshaker.
  grpc_handshaker_done_cb final_cb;
  void* final_arg;
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

void grpc_handshake_manager_add(grpc_handshaker* handshaker,
                                grpc_handshake_manager* mgr) {
  mgr->handshakers = gpr_realloc(mgr->handshakers,
                                 (mgr->count + 1) * sizeof(grpc_handshaker*));
  mgr->handshakers[mgr->count++] = handshaker;
}

void grpc_handshake_manager_destroy(grpc_exec_ctx* exec_ctx,
                                    grpc_handshake_manager* mgr) {
  for (size_t i = 0; i < mgr->count; ++i) {
    grpc_handshaker_destroy(exec_ctx, mgr->handshakers[i]);
  }
  gpr_free(mgr);
}

void grpc_handshake_manager_shutdown(grpc_exec_ctx* exec_ctx,
                                     grpc_handshake_manager* mgr) {
  // FIXME: maybe check which handshaker is currently in progress, and
  // only shut down that one?
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
                                 grpc_endpoint* endpoint, void* arg) {
  grpc_handshake_manager* mgr = arg;
  GPR_ASSERT(mgr->state != NULL);
  GPR_ASSERT(mgr->state->index < mgr->count);
  grpc_handshaker_done_cb cb = call_next_handshaker;
  // If this is the last handshaker, use the caller-supplied callback
  // and arg instead of chaining back to this function again.
  if (mgr->state->index == mgr->count - 1) {
    cb = mgr->state->final_cb;
    arg = mgr->state->final_arg;
  }
  // Invoke handshaker.
  grpc_handshaker_do_handshake(exec_ctx, mgr->handshakers[mgr->state->index],
                               endpoint, mgr->state->deadline, cb, arg);
  ++mgr->state->index;
  // If this is the last handshaker, clean up state.
  if (mgr->state->index == mgr->count) {
    gpr_free(mgr->state);
    mgr->state = NULL;
  }
}

void grpc_handshake_manager_do_handshake(grpc_exec_ctx* exec_ctx,
                                         grpc_handshake_manager* mgr,
                                         grpc_endpoint* endpoint,
                                         gpr_timespec deadline,
                                         grpc_handshaker_done_cb cb,
                                         void* arg) {
  if (mgr->count == 0) {
    // No handshakers registered, so we just immediately call the done
    // callback with the passed-in endpoint.
    cb(exec_ctx, endpoint, arg);
  } else {
    GPR_ASSERT(mgr->state == NULL);
    mgr->state = gpr_malloc(sizeof(struct grpc_handshaker_state));
    memset(mgr->state, 0, sizeof(*mgr->state));
    mgr->state->deadline = deadline;
    mgr->state->final_cb = cb;
    mgr->state->final_arg = arg;
    call_next_handshaker(exec_ctx, endpoint, mgr);
  }
}
