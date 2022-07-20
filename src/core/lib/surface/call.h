/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SURFACE_CALL_H
#define GRPC_CORE_LIB_SURFACE_CALL_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/impl/codegen/compression_types.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"

typedef void (*grpc_ioreq_completion_func)(grpc_call* call, int success,
                                           void* user_data);

typedef struct grpc_call_create_args {
  grpc_core::RefCountedPtr<grpc_core::Channel> channel;
  grpc_core::Server* server;

  grpc_call* parent;
  uint32_t propagation_mask;

  grpc_completion_queue* cq;
  /* if not NULL, it'll be used in lieu of cq */
  grpc_pollset_set* pollset_set_alternative;

  const void* server_transport_data;

  absl::optional<grpc_core::Slice> path;
  absl::optional<grpc_core::Slice> authority;

  grpc_core::Timestamp send_deadline;
} grpc_call_create_args;

/* Create a new call based on \a args.
   Regardless of success or failure, always returns a valid new call into *call
   */
grpc_error_handle grpc_call_create(grpc_call_create_args* args,
                                   grpc_call** call);

void grpc_call_set_completion_queue(grpc_call* call, grpc_completion_queue* cq);

grpc_core::Arena* grpc_call_get_arena(grpc_call* call);

grpc_call_stack* grpc_call_get_call_stack(grpc_call* call);

grpc_call_error grpc_call_start_batch_and_execute(grpc_call* call,
                                                  const grpc_op* ops,
                                                  size_t nops,
                                                  grpc_closure* closure);

/* gRPC core internal version of grpc_call_cancel that does not create
 * exec_ctx. */
void grpc_call_cancel_internal(grpc_call* call);

/* Given the top call_element, get the call object. */
grpc_call* grpc_call_from_top_element(grpc_call_element* surface_element);

void grpc_call_log_batch(const char* file, int line, gpr_log_severity severity,
                         const grpc_op* ops, size_t nops);

/* Set a context pointer.
   No thread safety guarantees are made wrt this value. */
/* TODO(#9731): add exec_ctx to destroy */
void grpc_call_context_set(grpc_call* call, grpc_context_index elem,
                           void* value, void (*destroy)(void* value));
/* Get a context pointer. */
void* grpc_call_context_get(grpc_call* call, grpc_context_index elem);

#define GRPC_CALL_LOG_BATCH(sev, ops, nops)        \
  do {                                             \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_api_trace)) { \
      grpc_call_log_batch(sev, ops, nops);         \
    }                                              \
  } while (0)

uint8_t grpc_call_is_client(grpc_call* call);

/* Get the estimated memory size for a call BESIDES the call stack. Combined
 * with the size of the call stack, it helps estimate the arena size for the
 * initial call. */
size_t grpc_call_get_initial_size_estimate();

/* Return an appropriate compression algorithm for the requested compression \a
 * level in the context of \a call. */
grpc_compression_algorithm grpc_call_compression_for_level(
    grpc_call* call, grpc_compression_level level);

/* Did this client call receive a trailers-only response */
/* TODO(markdroth): This is currently available only to the C++ API.
                    Move to surface API if requested by other languages. */
bool grpc_call_is_trailers_only(const grpc_call* call);

// Returns the authority for the call, as seen on the server side.
absl::string_view grpc_call_server_authority(const grpc_call* call);

extern grpc_core::TraceFlag grpc_call_error_trace;
extern grpc_core::TraceFlag grpc_compression_trace;

#endif /* GRPC_CORE_LIB_SURFACE_CALL_H */
