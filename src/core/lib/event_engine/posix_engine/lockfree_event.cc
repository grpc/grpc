// Copyright 2022 The gRPC Authors
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
#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/posix_engine/lockfree_event.h"

#include <atomic>
#include <cstdint>

#include "absl/status/status.h"

#include <grpc/support/atm.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/status_helper.h"

//  'state' holds the to call when the fd is readable or writable respectively.
//    It can contain one of the following values:
//      kClosureReady     : The fd has an I/O event of interest but there is no
//                          closure yet to execute

//      kClosureNotReady : The fd has no I/O event of interest

//      closure ptr       : The closure to be executed when the fd has an I/O
//                          event of interest

//      shutdown_error | kShutdownBit :
//                         'shutdown_error' field ORed with kShutdownBit.
//                          This indicates that the fd is shutdown. Since all
//                          memory allocations are word-aligned, the lower two
//                          bits of the shutdown_error pointer are always 0. So
//                          it is safe to OR these with kShutdownBit

//    Valid state transitions:

//    <closure ptr> <-----3------ kClosureNotReady -----1------->  kClosureReady
//        |  |                         ^   |    ^                         |  |
//        |  |                         |   |    |                         |  |
//        |  +--------------4----------+   6    +---------2---------------+  |
//        |                                |                                 |
//        |                                v                                 |
//        +-----5------->  [shutdown_error | kShutdownBit] <-------7---------+

//     For 1, 4 : See SetReady() function
//     For 2, 3 : See NotifyOn() function
//     For 5,6,7: See SetShutdown() function

namespace grpc_event_engine {
namespace experimental {

void LockfreeEvent::InitEvent() {
  // Perform an atomic store to start the state machine.

  // Note carefully that LockfreeEvent *MAY* be used whilst in a destroyed
  // state, while a file descriptor is on a freelist. In such a state it may
  // be SetReady'd, and so we need to perform an atomic operation here to
  // ensure no races
  state_.store(kClosureNotReady, std::memory_order_relaxed);
}

void LockfreeEvent::DestroyEvent() {
  intptr_t curr;
  do {
    curr = state_.load(std::memory_order_relaxed);
    if (curr & kShutdownBit) {
      grpc_core::internal::StatusFreeHeapPtr(curr & ~kShutdownBit);
    } else {
      GPR_ASSERT(curr == kClosureNotReady || curr == kClosureReady);
    }
    // we CAS in a shutdown, no error value here. If this event is interacted
    // with post-deletion (see the note in the constructor) we want the bit
    // pattern to prevent error retention in a deleted object
  } while (!state_.compare_exchange_strong(curr, kShutdownBit,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire));
}

void LockfreeEvent::NotifyOn(PosixEngineClosure* closure) {
  // This load needs to be an acquire load because this can be a shutdown
  // error that we might need to reference. Adding acquire semantics makes
  // sure that the shutdown error has been initialized properly before us
  // referencing it. The load() needs to be performed only once before entry
  // into the loop. This is because if any of the compare_exchange_strong
  // operations inside the loop return false, they automatically update curr
  // with the new value. So it doesn't need to be loaded again.
  intptr_t curr = state_.load(std::memory_order_acquire);
  while (true) {
    switch (curr) {
      case kClosureNotReady: {
        // kClosureNotReady -> <closure>.

        if (state_.compare_exchange_strong(
                curr, reinterpret_cast<intptr_t>(closure),
                std::memory_order_acq_rel, std::memory_order_acquire)) {
          return;  // Successful. Return
        }

        break;  // retry
      }

      case kClosureReady: {
        // Change the state to kClosureNotReady. Schedule the closure if
        // successful. If not, the state most likely transitioned to shutdown.
        // We should retry.

        // This can be a no-barrier cas since the state is being transitioned to
        // kClosureNotReady; set_ready and set_shutdown do not schedule any
        // closure when transitioning out of CLOSURE_NO_READY state (i.e there
        // is no other code that needs to 'happen-after' this)
        if (state_.compare_exchange_strong(curr, kClosureNotReady,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
          scheduler_->Run(closure);
          return;  // Successful. Return.
        }
        break;  // retry
      }

      default: {
        // 'curr' is either a closure or the fd is shutdown(in which case 'curr'
        // contains a pointer to the shutdown-error). If the fd is shutdown,
        // schedule the closure with the shutdown error
        if ((curr & kShutdownBit) > 0) {
          absl::Status shutdown_err =
              grpc_core::internal::StatusGetFromHeapPtr(curr & ~kShutdownBit);
          closure->SetStatus(shutdown_err);
          scheduler_->Run(closure);
          return;
        }

        // There is already a closure!. This indicates a bug in the code.
        grpc_core::Crash(
            "LockfreeEvent::NotifyOn: notify_on called with a previous "
            "callback still pending");
      }
    }
  }

  GPR_UNREACHABLE_CODE(return);
}

bool LockfreeEvent::SetShutdown(absl::Status shutdown_error) {
  intptr_t status_ptr = grpc_core::internal::StatusAllocHeapPtr(shutdown_error);
  gpr_atm new_state = status_ptr | kShutdownBit;
  // The load() needs to be performed only once before entry
  // into the loop. This is because if any of the compare_exchange_strong
  // operations inside the loop return false, they automatically update curr
  // with the new value. So it doesn't need to be loaded again.
  intptr_t curr = state_.load(std::memory_order_acquire);

  while (true) {
    switch (curr) {
      case kClosureReady:
      case kClosureNotReady:
        if (state_.compare_exchange_strong(curr, new_state,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
          return true;  // early out
        }
        break;  // retry

      default: {
        // 'curr' is either a closure or the fd is already shutdown

        // If fd is already shutdown, we are done.
        if ((curr & kShutdownBit) > 0) {
          grpc_core::internal::StatusFreeHeapPtr(status_ptr);
          return false;
        }

        // Fd is not shutdown. Schedule the closure and move the state to
        // shutdown state.
        // Needs an acquire to pair with setting the closure (and get a
        // happens-after on that edge), and a release to pair with anything
        // loading the shutdown state.
        if (state_.compare_exchange_strong(curr, new_state,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
          auto closure = reinterpret_cast<PosixEngineClosure*>(curr);
          closure->SetStatus(shutdown_error);
          scheduler_->Run(closure);
          return true;
        }
        // 'curr' was a closure but now changed to a different state. We will
        // have to retry
        break;
      }
    }
  }
  GPR_UNREACHABLE_CODE(return false);
}

void LockfreeEvent::SetReady() {
  // The load() needs to be performed only once before entry
  // into the loop. This is because if any of the compare_exchange_strong
  // operations inside the loop return false, they automatically update curr
  // with the new value. So it doesn't need to be loaded again.
  intptr_t curr = state_.load(std::memory_order_acquire);
  while (true) {
    switch (curr) {
      case kClosureReady: {
        // Already ready. We are done here.
        return;
      }

      case kClosureNotReady: {
        if (state_.compare_exchange_strong(curr, kClosureReady,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
          return;  // early out
        }
        break;  // retry
      }

      default: {
        // 'curr' is either a closure or the fd is shutdown
        if ((curr & kShutdownBit) > 0) {
          // The fd is shutdown. Do nothing.
          return;
        } else if (state_.compare_exchange_strong(curr, kClosureNotReady,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
          // Full cas: acquire pairs with this cas' release in the event of a
          // spurious set_ready; release pairs with this or the acquire in
          // notify_on (or set_shutdown)
          auto closure = reinterpret_cast<PosixEngineClosure*>(curr);
          closure->SetStatus(absl::OkStatus());
          scheduler_->Run(closure);
          return;
        }
        // else the state changed again (only possible by either a racing
        // set_ready or set_shutdown functions. In both these cases, the
        // closure would have been scheduled for execution. So we are done
        // here
        return;
      }
    }
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine
