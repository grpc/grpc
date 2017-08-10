/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/iomgr/lockfree_event.h"

#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"

extern grpc_tracer_flag grpc_polling_trace;

/* 'state' holds the to call when the fd is readable or writable respectively.
   It can contain one of the following values:
     CLOSURE_READY     : The fd has an I/O event of interest but there is no
                         closure yet to execute

     CLOSURE_NOT_READY : The fd has no I/O event of interest

     closure ptr       : The closure to be executed when the fd has an I/O
                         event of interest

     shutdown_error | FD_SHUTDOWN_BIT :
                        'shutdown_error' field ORed with FD_SHUTDOWN_BIT.
                         This indicates that the fd is shutdown. Since all
                         memory allocations are word-aligned, the lower two
                         bits of the shutdown_error pointer are always 0. So
                         it is safe to OR these with FD_SHUTDOWN_BIT

   Valid state transitions:

     <closure ptr> <-----3------ CLOSURE_NOT_READY ----1---->  CLOSURE_READY
       |  |                         ^   |    ^                         |  |
       |  |                         |   |    |                         |  |
       |  +--------------4----------+   6    +---------2---------------+  |
       |                                |                                 |
       |                                v                                 |
       +-----5------->  [shutdown_error | FD_SHUTDOWN_BIT] <----7---------+

    For 1, 4 : See grpc_lfev_set_ready() function
    For 2, 3 : See grpc_lfev_notify_on() function
    For 5,6,7: See grpc_lfev_set_shutdown() function */

#define CLOSURE_NOT_READY ((gpr_atm)0)
#define CLOSURE_READY ((gpr_atm)2)

#define FD_SHUTDOWN_BIT ((gpr_atm)1)

void grpc_lfev_init(gpr_atm *state) {
  gpr_atm_no_barrier_store(state, CLOSURE_NOT_READY);
}

void grpc_lfev_destroy(gpr_atm *state) {
  gpr_atm curr = gpr_atm_no_barrier_load(state);
  if (curr & FD_SHUTDOWN_BIT) {
    GRPC_ERROR_UNREF((grpc_error *)(curr & ~FD_SHUTDOWN_BIT));
  } else {
    GPR_ASSERT(curr == CLOSURE_NOT_READY || curr == CLOSURE_READY);
  }
}

bool grpc_lfev_is_shutdown(gpr_atm *state) {
  gpr_atm curr = gpr_atm_no_barrier_load(state);
  return (curr & FD_SHUTDOWN_BIT) != 0;
}

void grpc_lfev_notify_on(grpc_exec_ctx *exec_ctx, gpr_atm *state,
                         grpc_closure *closure, const char *variable) {
  while (true) {
    gpr_atm curr = gpr_atm_no_barrier_load(state);
    if (GRPC_TRACER_ON(grpc_polling_trace)) {
      gpr_log(GPR_ERROR, "lfev_notify_on[%s]: %p curr=%p closure=%p", variable,
              state, (void *)curr, closure);
    }
    switch (curr) {
      case CLOSURE_NOT_READY: {
        /* CLOSURE_NOT_READY -> <closure>.

           We're guaranteed by API that there's an acquire barrier before here,
           so there's no need to double-dip and this can be a release-only.

           The release itself pairs with the acquire half of a set_ready full
           barrier. */
        if (gpr_atm_rel_cas(state, CLOSURE_NOT_READY, (gpr_atm)closure)) {
          return; /* Successful. Return */
        }

        break; /* retry */
      }

      case CLOSURE_READY: {
        /* Change the state to CLOSURE_NOT_READY. Schedule the closure if
           successful. If not, the state most likely transitioned to shutdown.
           We should retry.

           This can be a no-barrier cas since the state is being transitioned to
           CLOSURE_NOT_READY; set_ready and set_shutdown do not schedule any
           closure when transitioning out of CLOSURE_NO_READY state (i.e there
           is no other code that needs to 'happen-after' this) */
        if (gpr_atm_no_barrier_cas(state, CLOSURE_READY, CLOSURE_NOT_READY)) {
          GRPC_CLOSURE_SCHED(exec_ctx, closure, GRPC_ERROR_NONE);
          return; /* Successful. Return */
        }

        break; /* retry */
      }

      default: {
        /* 'curr' is either a closure or the fd is shutdown(in which case 'curr'
           contains a pointer to the shutdown-error). If the fd is shutdown,
           schedule the closure with the shutdown error */
        if ((curr & FD_SHUTDOWN_BIT) > 0) {
          grpc_error *shutdown_err = (grpc_error *)(curr & ~FD_SHUTDOWN_BIT);
          GRPC_CLOSURE_SCHED(exec_ctx, closure,
                             GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                 "FD Shutdown", &shutdown_err, 1));
          return;
        }

        /* There is already a closure!. This indicates a bug in the code */
        gpr_log(GPR_ERROR,
                "notify_on called with a previous callback still pending");
        abort();
      }
    }
  }

  GPR_UNREACHABLE_CODE(return );
}

bool grpc_lfev_set_shutdown(grpc_exec_ctx *exec_ctx, gpr_atm *state,
                            grpc_error *shutdown_err) {
  gpr_atm new_state = (gpr_atm)shutdown_err | FD_SHUTDOWN_BIT;

  while (true) {
    gpr_atm curr = gpr_atm_no_barrier_load(state);
    if (GRPC_TRACER_ON(grpc_polling_trace)) {
      gpr_log(GPR_ERROR, "lfev_set_shutdown: %p curr=%p err=%s", state,
              (void *)curr, grpc_error_string(shutdown_err));
    }
    switch (curr) {
      case CLOSURE_READY:
      case CLOSURE_NOT_READY:
        /* Need a full barrier here so that the initial load in notify_on
           doesn't need a barrier */
        if (gpr_atm_full_cas(state, curr, new_state)) {
          return true; /* early out */
        }
        break; /* retry */

      default: {
        /* 'curr' is either a closure or the fd is already shutdown */

        /* If fd is already shutdown, we are done */
        if ((curr & FD_SHUTDOWN_BIT) > 0) {
          GRPC_ERROR_UNREF(shutdown_err);
          return false;
        }

        /* Fd is not shutdown. Schedule the closure and move the state to
           shutdown state.
           Needs an acquire to pair with setting the closure (and get a
           happens-after on that edge), and a release to pair with anything
           loading the shutdown state. */
        if (gpr_atm_full_cas(state, curr, new_state)) {
          GRPC_CLOSURE_SCHED(exec_ctx, (grpc_closure *)curr,
                             GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                 "FD Shutdown", &shutdown_err, 1));
          return true;
        }

        /* 'curr' was a closure but now changed to a different state. We will
          have to retry */
        break;
      }
    }
  }

  GPR_UNREACHABLE_CODE(return false);
}

void grpc_lfev_set_ready(grpc_exec_ctx *exec_ctx, gpr_atm *state,
                         const char *variable) {
  while (true) {
    gpr_atm curr = gpr_atm_no_barrier_load(state);

    if (GRPC_TRACER_ON(grpc_polling_trace)) {
      gpr_log(GPR_ERROR, "lfev_set_ready[%s]: %p curr=%p", variable, state,
              (void *)curr);
    }

    switch (curr) {
      case CLOSURE_READY: {
        /* Already ready. We are done here */
        return;
      }

      case CLOSURE_NOT_READY: {
        /* No barrier required as we're transitioning to a state that does not
           involve a closure */
        if (gpr_atm_no_barrier_cas(state, CLOSURE_NOT_READY, CLOSURE_READY)) {
          return; /* early out */
        }
        break; /* retry */
      }

      default: {
        /* 'curr' is either a closure or the fd is shutdown */
        if ((curr & FD_SHUTDOWN_BIT) > 0) {
          /* The fd is shutdown. Do nothing */
          return;
        }
        /* Full cas: acquire pairs with this cas' release in the event of a
           spurious set_ready; release pairs with this or the acquire in
           notify_on (or set_shutdown) */
        else if (gpr_atm_full_cas(state, curr, CLOSURE_NOT_READY)) {
          GRPC_CLOSURE_SCHED(exec_ctx, (grpc_closure *)curr, GRPC_ERROR_NONE);
          return;
        }
        /* else the state changed again (only possible by either a racing
           set_ready or set_shutdown functions. In both these cases, the closure
           would have been scheduled for execution. So we are done here */
        return;
      }
    }
  }
}
