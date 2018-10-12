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

#include <grpc/support/port_platform.h>

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_internal.h"

grpc_core::TraceFlag grpc_handshaker_trace(false, "handshaker");

//
// grpc_handshaker
//

void grpc_handshaker_init(const grpc_handshaker_vtable* vtable,
                          grpc_handshaker* handshaker) {
  handshaker->vtable = vtable;
}

void grpc_handshaker_destroy(grpc_handshaker* handshaker) {
  handshaker->vtable->destroy(handshaker);
}

void grpc_handshaker_shutdown(grpc_handshaker* handshaker, grpc_error* why) {
  handshaker->vtable->shutdown(handshaker, why);
}

void grpc_handshaker_do_handshake(grpc_handshaker* handshaker,
                                  grpc_tcp_server_acceptor* acceptor,
                                  grpc_closure* on_handshake_done,
                                  grpc_handshaker_args* args) {
  handshaker->vtable->do_handshake(handshaker, acceptor, on_handshake_done,
                                   args);
}

const char* grpc_handshaker_name(grpc_handshaker* handshaker) {
  return handshaker->vtable->name;
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
  grpc_handshake_manager* mgr = static_cast<grpc_handshake_manager*>(
      gpr_zalloc(sizeof(grpc_handshake_manager)));
  gpr_mu_init(&mgr->mu);
  gpr_ref_init(&mgr->refs, 1);
  return mgr;
}

void grpc_handshake_manager_pending_list_add(grpc_handshake_manager** head,
                                             grpc_handshake_manager* mgr) {
  GPR_ASSERT(mgr->prev == nullptr);
  GPR_ASSERT(mgr->next == nullptr);
  mgr->next = *head;
  if (*head) {
    (*head)->prev = mgr;
  }
  *head = mgr;
}

void grpc_handshake_manager_pending_list_remove(grpc_handshake_manager** head,
                                                grpc_handshake_manager* mgr) {
  if (mgr->next != nullptr) {
    mgr->next->prev = mgr->prev;
  }
  if (mgr->prev != nullptr) {
    mgr->prev->next = mgr->next;
  } else {
    GPR_ASSERT(*head == mgr);
    *head = mgr->next;
  }
}

void grpc_handshake_manager_pending_list_shutdown_all(
    grpc_handshake_manager* head, grpc_error* why) {
  while (head != nullptr) {
    grpc_handshake_manager_shutdown(head, GRPC_ERROR_REF(why));
    head = head->next;
  }
  GRPC_ERROR_UNREF(why);
}

static bool is_power_of_2(size_t n) { return (n & (n - 1)) == 0; }

void grpc_handshake_manager_add(grpc_handshake_manager* mgr,
                                grpc_handshaker* handshaker) {
  if (grpc_handshaker_trace.enabled()) {
    gpr_log(
        GPR_INFO,
        "handshake_manager %p: adding handshaker %s [%p] at index %" PRIuPTR,
        mgr, grpc_handshaker_name(handshaker), handshaker, mgr->count);
  }
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
    mgr->handshakers = static_cast<grpc_handshaker**>(gpr_realloc(
        mgr->handshakers, realloc_count * sizeof(grpc_handshaker*)));
  }
  mgr->handshakers[mgr->count++] = handshaker;
  gpr_mu_unlock(&mgr->mu);
}

static void grpc_handshake_manager_unref(grpc_handshake_manager* mgr) {
  if (gpr_unref(&mgr->refs)) {
    for (size_t i = 0; i < mgr->count; ++i) {
      grpc_handshaker_destroy(mgr->handshakers[i]);
    }
    gpr_free(mgr->handshakers);
    gpr_mu_destroy(&mgr->mu);
    gpr_free(mgr);
  }
}

void grpc_handshake_manager_destroy(grpc_handshake_manager* mgr) {
  grpc_handshake_manager_unref(mgr);
}

void grpc_handshake_manager_shutdown(grpc_handshake_manager* mgr,
                                     grpc_error* why) {
  gpr_mu_lock(&mgr->mu);
  // Shutdown the handshaker that's currently in progress, if any.
  if (!mgr->shutdown && mgr->index > 0) {
    mgr->shutdown = true;
    grpc_handshaker_shutdown(mgr->handshakers[mgr->index - 1],
                             GRPC_ERROR_REF(why));
  }
  gpr_mu_unlock(&mgr->mu);
  GRPC_ERROR_UNREF(why);
}

static char* handshaker_args_string(grpc_handshaker_args* args) {
  char* args_str = grpc_channel_args_string(args->args);
  size_t num_args = args->args != nullptr ? args->args->num_args : 0;
  size_t read_buffer_length =
      args->read_buffer != nullptr ? args->read_buffer->length : 0;
  char* str;
  gpr_asprintf(&str,
               "{endpoint=%p, args=%p {size=%" PRIuPTR
               ": %s}, read_buffer=%p (length=%" PRIuPTR "), exit_early=%d}",
               args->endpoint, args->args, num_args, args_str,
               args->read_buffer, read_buffer_length, args->exit_early);
  gpr_free(args_str);
  return str;
}

// Helper function to call either the next handshaker or the
// on_handshake_done callback.
// Returns true if we've scheduled the on_handshake_done callback.
static bool call_next_handshaker_locked(grpc_handshake_manager* mgr,
                                        grpc_error* error) {
  if (grpc_handshaker_trace.enabled()) {
    char* args_str = handshaker_args_string(&mgr->args);
    gpr_log(GPR_INFO,
            "handshake_manager %p: error=%s shutdown=%d index=%" PRIuPTR
            ", args=%s",
            mgr, grpc_error_string(error), mgr->shutdown, mgr->index, args_str);
    gpr_free(args_str);
  }
  GPR_ASSERT(mgr->index <= mgr->count);
  // If we got an error or we've been shut down or we're exiting early or
  // we've finished the last handshaker, invoke the on_handshake_done
  // callback.  Otherwise, call the next handshaker.
  if (error != GRPC_ERROR_NONE || mgr->shutdown || mgr->args.exit_early ||
      mgr->index == mgr->count) {
    if (error == GRPC_ERROR_NONE && mgr->shutdown) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("handshaker shutdown");
      // It is possible that the endpoint has already been destroyed by
      // a shutdown call while this callback was sitting on the ExecCtx
      // with no error.
      if (mgr->args.endpoint != nullptr) {
        // TODO(roth): It is currently necessary to shutdown endpoints
        // before destroying then, even when we know that there are no
        // pending read/write callbacks.  This should be fixed, at which
        // point this can be removed.
        grpc_endpoint_shutdown(mgr->args.endpoint, GRPC_ERROR_REF(error));
        grpc_endpoint_destroy(mgr->args.endpoint);
        mgr->args.endpoint = nullptr;
        grpc_channel_args_destroy(mgr->args.args);
        mgr->args.args = nullptr;
        grpc_slice_buffer_destroy_internal(mgr->args.read_buffer);
        gpr_free(mgr->args.read_buffer);
        mgr->args.read_buffer = nullptr;
      }
    }
    if (grpc_handshaker_trace.enabled()) {
      gpr_log(GPR_INFO,
              "handshake_manager %p: handshaking complete -- scheduling "
              "on_handshake_done with error=%s",
              mgr, grpc_error_string(error));
    }
    // Cancel deadline timer, since we're invoking the on_handshake_done
    // callback now.
    grpc_timer_cancel(&mgr->deadline_timer);
    GRPC_CLOSURE_SCHED(&mgr->on_handshake_done, error);
    mgr->shutdown = true;
  } else {
    if (grpc_handshaker_trace.enabled()) {
      gpr_log(
          GPR_INFO,
          "handshake_manager %p: calling handshaker %s [%p] at index %" PRIuPTR,
          mgr, grpc_handshaker_name(mgr->handshakers[mgr->index]),
          mgr->handshakers[mgr->index], mgr->index);
    }
    grpc_handshaker_do_handshake(mgr->handshakers[mgr->index], mgr->acceptor,
                                 &mgr->call_next_handshaker, &mgr->args);
  }
  ++mgr->index;
  return mgr->shutdown;
}

// A function used as the handshaker-done callback when chaining
// handshakers together.
static void call_next_handshaker(void* arg, grpc_error* error) {
  grpc_handshake_manager* mgr = static_cast<grpc_handshake_manager*>(arg);
  gpr_mu_lock(&mgr->mu);
  bool done = call_next_handshaker_locked(mgr, GRPC_ERROR_REF(error));
  gpr_mu_unlock(&mgr->mu);
  // If we're invoked the final callback, we won't be coming back
  // to this function, so we can release our reference to the
  // handshake manager.
  if (done) {
    grpc_handshake_manager_unref(mgr);
  }
}

// Callback invoked when deadline is exceeded.
static void on_timeout(void* arg, grpc_error* error) {
  grpc_handshake_manager* mgr = static_cast<grpc_handshake_manager*>(arg);
  if (error == GRPC_ERROR_NONE) {  // Timer fired, rather than being cancelled.
    grpc_handshake_manager_shutdown(
        mgr, GRPC_ERROR_CREATE_FROM_STATIC_STRING("Handshake timed out"));
  }
  grpc_handshake_manager_unref(mgr);
}

void grpc_handshake_manager_do_handshake(grpc_handshake_manager* mgr,
                                         grpc_endpoint* endpoint,
                                         const grpc_channel_args* channel_args,
                                         grpc_millis deadline,
                                         grpc_tcp_server_acceptor* acceptor,
                                         grpc_iomgr_cb_func on_handshake_done,
                                         void* user_data) {
  gpr_mu_lock(&mgr->mu);
  GPR_ASSERT(mgr->index == 0);
  GPR_ASSERT(!mgr->shutdown);
  // Construct handshaker args.  These will be passed through all
  // handshakers and eventually be freed by the on_handshake_done callback.
  mgr->args.endpoint = endpoint;
  mgr->args.args = grpc_channel_args_copy(channel_args);
  mgr->args.user_data = user_data;
  mgr->args.read_buffer = static_cast<grpc_slice_buffer*>(
      gpr_malloc(sizeof(*mgr->args.read_buffer)));
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
  grpc_timer_init(&mgr->deadline_timer, deadline, &mgr->on_timeout);
  // Start first handshaker, which also owns a ref.
  gpr_ref(&mgr->refs);
  bool done = call_next_handshaker_locked(mgr, GRPC_ERROR_NONE);
  gpr_mu_unlock(&mgr->mu);
  if (done) {
    grpc_handshake_manager_unref(mgr);
  }
}
