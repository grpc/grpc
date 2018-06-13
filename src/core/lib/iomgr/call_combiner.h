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

#ifndef GRPC_CORE_LIB_IOMGR_CALL_COMBINER_H
#define GRPC_CORE_LIB_IOMGR_CALL_COMBINER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <grpc/support/atm.h>

#include "src/core/lib/gpr/mpscq.h"
#include "src/core/lib/iomgr/closure.h"

// A simple, lock-free mechanism for serializing activity related to a
// single call.  This is similar to a combiner but is more lightweight.
//
// It requires the callback (or, in the common case where the callback
// actually kicks off a chain of callbacks, the last callback in that
// chain) to explicitly indicate (by calling GRPC_CALL_COMBINER_STOP())
// when it is done with the action that was kicked off by the original
// callback.

extern grpc_core::TraceFlag grpc_call_combiner_trace;

typedef struct {
  gpr_atm size;  // size_t, num closures in queue or currently executing
  gpr_mpscq queue;
  // Either 0 (if not cancelled and no cancellation closure set),
  // a grpc_closure* (if the lowest bit is 0),
  // or a grpc_error* (if the lowest bit is 1).
  gpr_atm cancel_state;
} grpc_call_combiner;

// Assumes memory was initialized to zero.
void grpc_call_combiner_init(grpc_call_combiner* call_combiner);

void grpc_call_combiner_destroy(grpc_call_combiner* call_combiner);

#ifndef NDEBUG
#define GRPC_CALL_COMBINER_START(call_combiner, closure, error, reason)   \
  grpc_call_combiner_start((call_combiner), (closure), (error), __FILE__, \
                           __LINE__, (reason))
#define GRPC_CALL_COMBINER_STOP(call_combiner, reason) \
  grpc_call_combiner_stop((call_combiner), __FILE__, __LINE__, (reason))
/// Starts processing \a closure on \a call_combiner.
void grpc_call_combiner_start(grpc_call_combiner* call_combiner,
                              grpc_closure* closure, grpc_error* error,
                              const char* file, int line, const char* reason);
/// Yields the call combiner to the next closure in the queue, if any.
void grpc_call_combiner_stop(grpc_call_combiner* call_combiner,
                             const char* file, int line, const char* reason);
#else
#define GRPC_CALL_COMBINER_START(call_combiner, closure, error, reason) \
  grpc_call_combiner_start((call_combiner), (closure), (error), (reason))
#define GRPC_CALL_COMBINER_STOP(call_combiner, reason) \
  grpc_call_combiner_stop((call_combiner), (reason))
/// Starts processing \a closure on \a call_combiner.
void grpc_call_combiner_start(grpc_call_combiner* call_combiner,
                              grpc_closure* closure, grpc_error* error,
                              const char* reason);
/// Yields the call combiner to the next closure in the queue, if any.
void grpc_call_combiner_stop(grpc_call_combiner* call_combiner,
                             const char* reason);
#endif

/// Registers \a closure to be invoked by \a call_combiner when
/// grpc_call_combiner_cancel() is called.
///
/// Once a closure is registered, it will always be scheduled exactly
/// once; this allows the closure to hold references that will be freed
/// regardless of whether or not the call was cancelled.  If a cancellation
/// does occur, the closure will be scheduled with the cancellation error;
/// otherwise, it will be scheduled with GRPC_ERROR_NONE.
///
/// The closure will be scheduled in the following cases:
/// - If grpc_call_combiner_cancel() was called prior to registering the
///   closure, it will be scheduled immediately with the cancelation error.
/// - If grpc_call_combiner_cancel() is called after registering the
///   closure, the closure will be scheduled with the cancellation error.
/// - If grpc_call_combiner_set_notify_on_cancel() is called again to
///   register a new cancellation closure, the previous cancellation
///   closure will be scheduled with GRPC_ERROR_NONE.
///
/// If \a closure is NULL, then no closure will be invoked on
/// cancellation; this effectively unregisters the previously set closure.
/// However, most filters will not need to explicitly unregister their
/// callbacks, as this is done automatically when the call is destroyed.
void grpc_call_combiner_set_notify_on_cancel(grpc_call_combiner* call_combiner,
                                             grpc_closure* closure);

/// Indicates that the call has been cancelled.
void grpc_call_combiner_cancel(grpc_call_combiner* call_combiner,
                               grpc_error* error);

#endif /* GRPC_CORE_LIB_IOMGR_CALL_COMBINER_H */
