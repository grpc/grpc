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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/lockfree_event.h"

#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"

extern grpc_core::DebugOnlyTraceFlag grpc_polling_trace;

/* 'state' holds the to call when the fd is readable or writable respectively.
   It can contain one of the following values:
     kClosureReady     : The fd has an I/O event of interest but there is no
                         closure yet to execute

     kClosureNotReady : The fd has no I/O event of interest

     closure ptr       : The closure to be executed when the fd has an I/O
                         event of interest

     shutdown_error | kShutdownBit :
                        'shutdown_error' field ORed with kShutdownBit.
                         This indicates that the fd is shutdown. Since all
                         memory allocations are word-aligned, the lower two
                         bits of the shutdown_error pointer are always 0. So
                         it is safe to OR these with kShutdownBit

   Valid state transitions:

     <closure ptr> <-----3------ kClosureNotReady -----1------->  kClosureReady
       |  |                         ^   |    ^                         |  |
       |  |                         |   |    |                         |  |
       |  +--------------4----------+   6    +---------2---------------+  |
       |                                |                                 |
       |                                v                                 |
       +-----5------->  [shutdown_error | kShutdownBit] <-------7---------+

    For 1, 4 : See SetReady() function
    For 2, 3 : See NotifyOn() function
    For 5,6,7: See SetShutdown() function */

namespace grpc_core {

LockfreeEvent::LockfreeEvent() { InitEvent(); }

void LockfreeEvent::InitEvent() {
  /* Perform an atomic store to start the state machine.

     Note carefully that LockfreeEvent *MAY* be used whilst in a destroyed
     state, while a file descriptor is on a freelist. In such a state it may
     be SetReady'd, and so we need to perform an atomic operation here to
     ensure no races */
  gpr_atm_no_barrier_store(&state_, kClosureNotReady);
}

void LockfreeEvent::DestroyEvent() {
  gpr_atm curr;
  do {
    curr = gpr_atm_no_barrier_load(&state_);
    if (curr & kShutdownBit) {
      GRPC_ERROR_UNREF((grpc_error*)(curr & ~kShutdownBit));
    } else {
      GPR_ASSERT(curr == kClosureNotReady || curr == kClosureReady);
    }
    /* we CAS in a shutdown, no error value here. If this event is interacted
       with post-deletion (see the note in the constructor) we want the bit
       pattern to prevent error retention in a deleted object */
  } while (!gpr_atm_no_barrier_cas(&state_, curr,
                                   kShutdownBit /* shutdown, no error */));
}

void LockfreeEvent::NotifyOn(grpc_closure* closure) {
  while (true) {
    /* This load needs to be an acquire load because this can be a shutdown
     * error that we might need to reference. Adding acquire semantics makes
     * sure that the shutdown error has been initialized properly before us
     * referencing it. */
    gpr_atm curr = gpr_atm_acq_load(&state_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_polling_trace)) {
      gpr_log(GPR_DEBUG, "LockfreeEvent::NotifyOn: %p curr=%p closure=%p", this,
              (void*)curr, closure);
    }
    switch (curr) {
      case kClosureNotReady: {
        /* kClosureNotReady -> <closure>.

           We're guaranteed by API that there's an acquire barrier before here,
           so there's no need to double-dip and this can be a release-only.

           The release itself pairs with the acquire half of a set_ready full
           barrier. */
        if (gpr_atm_rel_cas(&state_, kClosureNotReady, (gpr_atm)closure)) {
          return; /* Successful. Return */
        }

        break; /* retry */
      }

      case kClosureReady: {
        /* Change the state to kClosureNotReady. Schedule the closure if
           successful. If not, the state most likely transitioned to shutdown.
           We should retry.

           This can be a no-barrier cas since the state is being transitioned to
           kClosureNotReady; set_ready and set_shutdown do not schedule any
           closure when transitioning out of CLOSURE_NO_READY state (i.e there
           is no other code that needs to 'happen-after' this) */
        if (gpr_atm_no_barrier_cas(&state_, kClosureReady, kClosureNotReady)) {
          ExecCtx::Run(DEBUG_LOCATION, closure, GRPC_ERROR_NONE);
          return; /* Successful. Return */
        }

        break; /* retry */
      }

      default: {
        /* 'curr' is either a closure or the fd is shutdown(in which case 'curr'
           contains a pointer to the shutdown-error). If the fd is shutdown,
           schedule the closure with the shutdown error */
        if ((curr & kShutdownBit) > 0) {
          grpc_error* shutdown_err = (grpc_error*)(curr & ~kShutdownBit);
          ExecCtx::Run(DEBUG_LOCATION, closure,
                       GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                           "FD Shutdown", &shutdown_err, 1));
          return;
        }

        /* There is already a closure!. This indicates a bug in the code */
        gpr_log(GPR_ERROR,
                "LockfreeEvent::NotifyOn: notify_on called with a previous "
                "callback still pending");
        abort();
      }
    }
  }

  GPR_UNREACHABLE_CODE(return );
}

bool LockfreeEvent::SetShutdown(grpc_error* shutdown_err) {
  gpr_atm new_state = (gpr_atm)shutdown_err | kShutdownBit;

  while (true) {
    gpr_atm curr = gpr_atm_no_barrier_load(&state_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_polling_trace)) {
      gpr_log(GPR_DEBUG, "LockfreeEvent::SetShutdown: %p curr=%p err=%s",
              &state_, (void*)curr, grpc_error_string(shutdown_err));
    }
    switch (curr) {
      case kClosureReady:
      case kClosureNotReady:
        /* Need a full barrier here so that the initial load in notify_on
           doesn't need a barrier */
        if (gpr_atm_full_cas(&state_, curr, new_state)) {
          return true; /* early out */
        }
        break; /* retry */

      default: {
        /* 'curr' is either a closure or the fd is already shutdown */

        /* If fd is already shutdown, we are done */
        if ((curr & kShutdownBit) > 0) {
          GRPC_ERROR_UNREF(shutdown_err);
          return false;
        }

        /* Fd is not shutdown. Schedule the closure and move the state to
           shutdown state.
           Needs an acquire to pair with setting the closure (and get a
           happens-after on that edge), and a release to pair with anything
           loading the shutdown state. */
        if (gpr_atm_full_cas(&state_, curr, new_state)) {
          ExecCtx::Run(DEBUG_LOCATION, (grpc_closure*)curr,
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

void LockfreeEvent::SetReady() {
  while (true) {
    gpr_atm curr = gpr_atm_no_barrier_load(&state_);

    if (GRPC_TRACE_FLAG_ENABLED(grpc_polling_trace)) {
      gpr_log(GPR_DEBUG, "LockfreeEvent::SetReady: %p curr=%p", &state_,
              (void*)curr);
    }

    switch (curr) {
      case kClosureReady: {
        /* Already ready. We are done here */
        return;
      }

      case kClosureNotReady: {
        /* No barrier required as we're transitioning to a state that does not
           involve a closure */
        if (gpr_atm_no_barrier_cas(&state_, kClosureNotReady, kClosureReady)) {
          return; /* early out */
        }
        break; /* retry */
      }

      default: {
        /* 'curr' is either a closure or the fd is shutdown */
        if ((curr & kShutdownBit) > 0) {
          /* The fd is shutdown. Do nothing */
          return;
        }
        /* Full cas: acquire pairs with this cas' release in the event of a
           spurious set_ready; release pairs with this or the acquire in
           notify_on (or set_shutdown) */
        else if (gpr_atm_full_cas(&state_, curr, kClosureNotReady)) {
          ExecCtx::Run(DEBUG_LOCATION, (grpc_closure*)curr, GRPC_ERROR_NONE);
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

}  // namespace grpc_core
