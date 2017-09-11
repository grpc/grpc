/*
 *
 * Copyright 2016 gRPC authors.
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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/iomgr/timer.h"

//
// grpc_handshaker
//

void grpc_handshaker_init(const grpc_handshaker_vtable* vtable,
                          grpc_handshaker* handshaker) {
  handshaker->vtable = vtable;
}

void grpc_handshaker_destroy(grpc_exec_ctx* exec_ctx,
                             grpc_handshaker* handshaker) {
  handshaker->vtable->destroy(exec_ctx, handshaker);
}

void grpc_handshaker_shutdown(grpc_exec_ctx* exec_ctx,
                              grpc_handshaker* handshaker, grpc_error* why) {
  handshaker->vtable->shutdown(exec_ctx, handshaker, why);
}

void grpc_handshaker_do_handshake(grpc_exec_ctx* exec_ctx,
                                  grpc_handshaker* handshaker,
                                  grpc_tcp_server_acceptor* acceptor,
                                  grpc_closure* on_handshake_done,
                                  grpc_handshaker_args* args) {
  handshaker->vtable->do_handshake(exec_ctx, handshaker, acceptor,
                                   on_handshake_done, args);
}

//
// grpc_handshake_manager
//

struct grpc_handshake_manager {
  gpr_mu mu;
  gpr_refcount refs;
  bool shutdown;
  // An array of handshakers added via grpc_handshake_manager_add().
  size_t count;
  grpc_handshaker** handshakers;
  // The index of the handshaker to invoke next and closure to invoke it.
  size_t index;
  grpc_closure call_next_handshaker;
  // The acceptor to call the handshakers with.
  grpc_tcp_server_acceptor* acceptor;
  // Deadline timer across all handshakers.
  grpc_timer deadline_timer;
  grpc_closure on_timeout;
  // The final callback and user_data to invoke after the last handshaker.
  grpc_closure on_handshake_done;
  void* user_data;
  // Handshaker args.
  grpc_handshaker_args args;
  // Links to the previous and next managers in a list of all pending handshakes
  // Used at server side only.
  grpc_handshake_manager* prev;
  grpc_handshake_manager* next;
};

grpc_handshake_manager* grpc_handshake_manager_create() {
  grpc_handshake_manager* mgr =
      (grpc_handshake_manager*)gpr_zalloc(sizeof(grpc_handshake_manager));
  gpr_mu_init(&mgr->mu);
  gpr_ref_init(&mgr->refs, 1);
  return mgr;
}

void grpc_handshake_manager_pending_list_add(grpc_handshake_manager** head,
                                             grpc_handshake_manager* mgr) {
  GPR_ASSERT(mgr->prev == NULL);
  GPR_ASSERT(mgr->next == NULL);
  mgr->next = *head;
  if (*head) {
    (*head)->prev = mgr;
  }
  *head = mgr;
}

void grpc_handshake_manager_pending_list_remove(grpc_handshake_manager** head,
                                                grpc_handshake_manager* mgr) {
  if (mgr->next != NULL) {
    mgr->next->prev = mgr->prev;
  }
  if (mgr->prev != NULL) {
    mgr->prev->next = mgr->next;
  } else {
    GPR_ASSERT(*head == mgr);
    *head = mgr->next;
  }
}

void grpc_handshake_manager_pending_list_shutdown_all(
    grpc_exec_ctx* exec_ctx, grpc_handshake_manager* head, grpc_error* why) {
  while (head != NULL) {
    grpc_handshake_manager_shutdown(exec_ctx, head, GRPC_ERROR_REF(why));
    head = head->next;
  }
  GRPC_ERROR_UNREF(why);
}

static bool is_power_of_2(size_t n) { return (n & (n - 1)) == 0; }

void grpc_handshake_manager_add(grpc_handshake_manager* mgr,
                                grpc_handshaker* handshaker) {
  gpr_mu_lock(&mgr->mu);
  // To avoid allocating memory for each handshaker we add, we double
  // the number of elements every time we need more.
  size_t realloc_count = 0;
  if (mgr->count == 0) {
    realloc_count = 2;
  } else if (mgr->count >= 2 && is_power_of_2(mgr->count)) {
    realloc_count = mgr->count * 2;
  }
  if (realloc_count > 0) {
    mgr->handshakers = (grpc_handshaker**)gpr_realloc(
        mgr->handshakers, realloc_count * sizeof(grpc_handshaker*));
  }
  mgr->handshakers[mgr->count++] = handshaker;
  gpr_mu_unlock(&mgr->mu);
}

static void grpc_handshake_manager_unref(grpc_exec_ctx* exec_ctx,
                                         grpc_handshake_manager* mgr) {
  if (gpr_unref(&mgr->refs)) {
    for (size_t i = 0; i < mgr->count; ++i) {
      grpc_handshaker_destroy(exec_ctx, mgr->handshakers[i]);
    }
    gpr_free(mgr->handshakers);
    gpr_mu_destroy(&mgr->mu);
    gpr_free(mgr);
  }
}

void grpc_handshake_manager_destroy(grpc_exec_ctx* exec_ctx,
                                    grpc_handshake_manager* mgr) {
  grpc_handshake_manager_unref(exec_ctx, mgr);
}

void grpc_handshake_manager_shutdown(grpc_exec_ctx* exec_ctx,
                                     grpc_handshake_manager* mgr,
                                     grpc_error* why) {
  gpr_mu_lock(&mgr->mu);
  // Shutdown the handshaker that's currently in progress, if any.
  if (!mgr->shutdown && mgr->index > 0) {
    mgr->shutdown = true;
    grpc_handshaker_shutdown(exec_ctx, mgr->handshakers[mgr->index - 1],
                             GRPC_ERROR_REF(why));
  }
  gpr_mu_unlock(&mgr->mu);
  GRPC_ERROR_UNREF(why);
}

// Helper function to call either the next handshaker or the
// on_handshake_done callback.
// Returns true if we've scheduled the on_handshake_done callback.
static bool call_next_handshaker_locked(grpc_exec_ctx* exec_ctx,
                                        grpc_handshake_manager* mgr,
                                        grpc_error* error) {
  GPR_ASSERT(mgr->index <= mgr->count);
  // If we got an error or we've been shut down or we're exiting early or
  // we've finished the last handshaker, invoke the on_handshake_done
  // callback.  Otherwise, call the next handshaker.
  if (error != GRPC_ERROR_NONE || mgr->shutdown || mgr->args.exit_early ||
      mgr->index == mgr->count) {
    // Cancel deadline timer, since we're invoking the on_handshake_done
    // callback now.
    grpc_timer_cancel(exec_ctx, &mgr->deadline_timer);
    GRPC_CLOSURE_SCHED(exec_ctx, &mgr->on_handshake_done, error);
    mgr->shutdown = true;
  } else {
    grpc_handshaker_do_handshake(exec_ctx, mgr->handshakers[mgr->index],
                                 mgr->acceptor, &mgr->call_next_handshaker,
                                 &mgr->args);
  }
  ++mgr->index;
  return mgr->shutdown;
}

// A function used as the handshaker-done callback when chaining
// handshakers together.
static void call_next_handshaker(grpc_exec_ctx* exec_ctx, void* arg,
                                 grpc_error* error) {
  grpc_handshake_manager* mgr = (grpc_handshake_manager*)arg;
  gpr_mu_lock(&mgr->mu);
  bool done = call_next_handshaker_locked(exec_ctx, mgr, GRPC_ERROR_REF(error));
  gpr_mu_unlock(&mgr->mu);
  // If we're invoked the final callback, we won't be coming back
  // to this function, so we can release our reference to the
  // handshake manager.
  if (done) {
    grpc_handshake_manager_unref(exec_ctx, mgr);
  }
}

// Callback invoked when deadline is exceeded.
static void on_timeout(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {
  grpc_handshake_manager* mgr = (grpc_handshake_manager*)arg;
  if (error == GRPC_ERROR_NONE) {  // Timer fired, rather than being cancelled.
    grpc_handshake_manager_shutdown(
        exec_ctx, mgr,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Handshake timed out"));
  }
  grpc_handshake_manager_unref(exec_ctx, mgr);
}

void grpc_handshake_manager_do_handshake(
    grpc_exec_ctx* exec_ctx, grpc_handshake_manager* mgr,
    grpc_endpoint* endpoint, const grpc_channel_args* channel_args,
    gpr_timespec deadline, grpc_tcp_server_acceptor* acceptor,
    grpc_iomgr_cb_func on_handshake_done, void* user_data) {
  gpr_mu_lock(&mgr->mu);
  GPR_ASSERT(mgr->index == 0);
  GPR_ASSERT(!mgr->shutdown);
  // Construct handshaker args.  These will be passed through all
  // handshakers and eventually be freed by the on_handshake_done callback.
  mgr->args.endpoint = endpoint;
  mgr->args.args = grpc_channel_args_copy(channel_args);
  mgr->args.user_data = user_data;
  mgr->args.read_buffer =
      (grpc_slice_buffer*)gpr_malloc(sizeof(*mgr->args.read_buffer));
  grpc_slice_buffer_init(mgr->args.read_buffer);
  // Initialize state needed for calling handshakers.
  mgr->acceptor = acceptor;
  GRPC_CLOSURE_INIT(&mgr->call_next_handshaker, call_next_handshaker, mgr,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&mgr->on_handshake_done, on_handshake_done, &mgr->args,
                    grpc_schedule_on_exec_ctx);
  // Start deadline timer, which owns a ref.
  gpr_ref(&mgr->refs);
  GRPC_CLOSURE_INIT(&mgr->on_timeout, on_timeout, mgr,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(exec_ctx, &mgr->deadline_timer,
                  gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC),
                  &mgr->on_timeout, gpr_now(GPR_CLOCK_MONOTONIC));
  // Start first handshaker, which also owns a ref.
  gpr_ref(&mgr->refs);
  bool done = call_next_handshaker_locked(exec_ctx, mgr, GRPC_ERROR_NONE);
  gpr_mu_unlock(&mgr->mu);
  if (done) {
    grpc_handshake_manager_unref(exec_ctx, mgr);
  }
}
