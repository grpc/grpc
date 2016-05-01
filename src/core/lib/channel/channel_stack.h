/*
 *
 * Copyright 2015, Google Inc.
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

#ifndef GRPC_CORE_LIB_CHANNEL_CHANNEL_STACK_H
#define GRPC_CORE_LIB_CHANNEL_CHANNEL_STACK_H

/* A channel filter defines how operations on a channel are implemented.
   Channel filters are chained together to create full channels, and if those
   chains are linear, then channel stacks provide a mechanism to minimize
   allocations for that chain.
   Call stacks are created by channel stacks and represent the per-call data
   for that stack. */

#include <stddef.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/transport/transport.h"

typedef struct grpc_channel_element grpc_channel_element;
typedef struct grpc_call_element grpc_call_element;

typedef struct grpc_channel_stack grpc_channel_stack;
typedef struct grpc_call_stack grpc_call_stack;

typedef struct {
  grpc_channel_stack *channel_stack;
  const grpc_channel_args *channel_args;
  int is_first;
  int is_last;
} grpc_channel_element_args;

typedef struct {
  grpc_call_stack *call_stack;
  const void *server_transport_data;
  grpc_call_context_element *context;
} grpc_call_element_args;

/* Channel filters specify:
   1. the amount of memory needed in the channel & call (via the sizeof_XXX
      members)
   2. functions to initialize and destroy channel & call data
      (init_XXX, destroy_XXX)
   3. functions to implement call operations and channel operations (call_op,
      channel_op)
   4. a name, which is useful when debugging

   Members are laid out in approximate frequency of use order. */
typedef struct {
  /* Called to eg. send/receive data on a call.
     See grpc_call_next_op on how to call the next element in the stack */
  void (*start_transport_stream_op)(grpc_exec_ctx *exec_ctx,
                                    grpc_call_element *elem,
                                    grpc_transport_stream_op *op);
  /* Called to handle channel level operations - e.g. new calls, or transport
     closure.
     See grpc_channel_next_op on how to call the next element in the stack */
  void (*start_transport_op)(grpc_exec_ctx *exec_ctx,
                             grpc_channel_element *elem, grpc_transport_op *op);

  /* sizeof(per call data) */
  size_t sizeof_call_data;
  /* Initialize per call data.
     elem is initialized at the start of the call, and elem->call_data is what
     needs initializing.
     The filter does not need to do any chaining.
     server_transport_data is an opaque pointer. If it is NULL, this call is
     on a client; if it is non-NULL, then it points to memory owned by the
     transport and is on the server. Most filters want to ignore this
     argument. */
  void (*init_call_elem)(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                         grpc_call_element_args *args);
  void (*set_pollset)(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                      grpc_pollset *pollset);
  /* Destroy per call data.
     The filter does not need to do any chaining.
     The bottom filter of a stack will be passed a non-NULL pointer to
     \a and_free_memory that should be passed to gpr_free when destruction
     is complete. */
  void (*destroy_call_elem)(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                            void *and_free_memory);

  /* sizeof(per channel data) */
  size_t sizeof_channel_data;
  /* Initialize per-channel data.
     elem is initialized at the start of the call, and elem->channel_data is
     what needs initializing.
     is_first, is_last designate this elements position in the stack, and are
     useful for asserting correct configuration by upper layer code.
     The filter does not need to do any chaining */
  void (*init_channel_elem)(grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
                            grpc_channel_element_args *args);
  /* Destroy per channel data.
     The filter does not need to do any chaining */
  void (*destroy_channel_elem)(grpc_exec_ctx *exec_ctx,
                               grpc_channel_element *elem);

  /* Implement grpc_call_get_peer() */
  char *(*get_peer)(grpc_exec_ctx *exec_ctx, grpc_call_element *elem);

  /* The name of this filter */
  const char *name;
} grpc_channel_filter;

/* A channel_element tracks its filter and the filter requested memory within
   a channel allocation */
struct grpc_channel_element {
  const grpc_channel_filter *filter;
  void *channel_data;
};

/* A call_element tracks its filter, the filter requested memory within
   a channel allocation, and the filter requested memory within a call
   allocation */
struct grpc_call_element {
  const grpc_channel_filter *filter;
  void *channel_data;
  void *call_data;
};

/* A channel stack tracks a set of related filters for one channel, and
   guarantees they live within a single malloc() allocation */
struct grpc_channel_stack {
  grpc_stream_refcount refcount;
  size_t count;
  /* Memory required for a call stack (computed at channel stack
     initialization) */
  size_t call_stack_size;
};

/* A call stack tracks a set of related filters for one call, and guarantees
   they live within a single malloc() allocation */
struct grpc_call_stack {
  /* shared refcount for this channel stack.
     MUST be the first element: the underlying code calls destroy
     with the address of the refcount, but higher layers prefer to think
     about the address of the call stack itself. */
  grpc_stream_refcount refcount;
  size_t count;
};

/* Get a channel element given a channel stack and its index */
grpc_channel_element *grpc_channel_stack_element(grpc_channel_stack *stack,
                                                 size_t i);
/* Get the last channel element in a channel stack */
grpc_channel_element *grpc_channel_stack_last_element(
    grpc_channel_stack *stack);
/* Get a call stack element given a call stack and an index */
grpc_call_element *grpc_call_stack_element(grpc_call_stack *stack, size_t i);

/* Determine memory required for a channel stack containing a set of filters */
size_t grpc_channel_stack_size(const grpc_channel_filter **filters,
                               size_t filter_count);
/* Initialize a channel stack given some filters */
void grpc_channel_stack_init(grpc_exec_ctx *exec_ctx, int initial_refs,
                             grpc_iomgr_cb_func destroy, void *destroy_arg,
                             const grpc_channel_filter **filters,
                             size_t filter_count, const grpc_channel_args *args,
                             const char *name, grpc_channel_stack *stack);
/* Destroy a channel stack */
void grpc_channel_stack_destroy(grpc_exec_ctx *exec_ctx,
                                grpc_channel_stack *stack);

/* Initialize a call stack given a channel stack. transport_server_data is
   expected to be NULL on a client, or an opaque transport owned pointer on the
   server. */
void grpc_call_stack_init(grpc_exec_ctx *exec_ctx,
                          grpc_channel_stack *channel_stack, int initial_refs,
                          grpc_iomgr_cb_func destroy, void *destroy_arg,
                          grpc_call_context_element *context,
                          const void *transport_server_data,
                          grpc_call_stack *call_stack);
/* Set a pollset for a call stack: must occur before the first op is started */
void grpc_call_stack_set_pollset(grpc_exec_ctx *exec_ctx,
                                 grpc_call_stack *call_stack,
                                 grpc_pollset *pollset);

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
#define GRPC_CALL_STACK_REF(call_stack, reason) \
  grpc_stream_ref(&(call_stack)->refcount, reason)
#define GRPC_CALL_STACK_UNREF(exec_ctx, call_stack, reason) \
  grpc_stream_unref(exec_ctx, &(call_stack)->refcount, reason)
#define GRPC_CHANNEL_STACK_REF(channel_stack, reason) \
  grpc_stream_ref(&(channel_stack)->refcount, reason)
#define GRPC_CHANNEL_STACK_UNREF(exec_ctx, channel_stack, reason) \
  grpc_stream_unref(exec_ctx, &(channel_stack)->refcount, reason)
#else
#define GRPC_CALL_STACK_REF(call_stack, reason) \
  grpc_stream_ref(&(call_stack)->refcount)
#define GRPC_CALL_STACK_UNREF(exec_ctx, call_stack, reason) \
  grpc_stream_unref(exec_ctx, &(call_stack)->refcount)
#define GRPC_CHANNEL_STACK_REF(channel_stack, reason) \
  grpc_stream_ref(&(channel_stack)->refcount)
#define GRPC_CHANNEL_STACK_UNREF(exec_ctx, channel_stack, reason) \
  grpc_stream_unref(exec_ctx, &(channel_stack)->refcount)
#endif

/* Destroy a call stack */
void grpc_call_stack_destroy(grpc_exec_ctx *exec_ctx, grpc_call_stack *stack,
                             void *and_free_memory);

/* Ignore set pollset - used by filters to implement the set_pollset method
   if they don't care about pollsets at all. Does nothing. */
void grpc_call_stack_ignore_set_pollset(grpc_exec_ctx *exec_ctx,
                                        grpc_call_element *elem,
                                        grpc_pollset *pollset);
/* Call the next operation in a call stack */
void grpc_call_next_op(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                       grpc_transport_stream_op *op);
/* Call the next operation (depending on call directionality) in a channel
   stack */
void grpc_channel_next_op(grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
                          grpc_transport_op *op);
/* Pass through a request to get_peer to the next child element */
char *grpc_call_next_get_peer(grpc_exec_ctx *exec_ctx, grpc_call_element *elem);

/* Given the top element of a channel stack, get the channel stack itself */
grpc_channel_stack *grpc_channel_stack_from_top_element(
    grpc_channel_element *elem);
/* Given the top element of a call stack, get the call stack itself */
grpc_call_stack *grpc_call_stack_from_top_element(grpc_call_element *elem);

void grpc_call_log_op(char *file, int line, gpr_log_severity severity,
                      grpc_call_element *elem, grpc_transport_stream_op *op);

void grpc_call_element_send_cancel(grpc_exec_ctx *exec_ctx,
                                   grpc_call_element *cur_elem);

extern int grpc_trace_channel;

#define GRPC_CALL_LOG_OP(sev, elem, op) \
  if (grpc_trace_channel) grpc_call_log_op(sev, elem, op)

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNEL_STACK_H */
