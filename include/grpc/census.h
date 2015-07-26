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

/* RPC-internal Census API's. These are designed to be generic enough that
 * they can (ultimately) be used in many different RPC systems (with differing
 * implementations). */

#ifndef CENSUS_CENSUS_H
#define CENSUS_CENSUS_H

#include <grpc/grpc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Identify census functionality that can be enabled via census_initialize(). */
enum census_functions {
  CENSUS_NONE = 0,       /* Do not enable census. */
  CENSUS_TRACING = 1,    /* Enable distributed tracing. */
  CENSUS_STATS = 2,      /* Enable stats collection. */
  CENSUS_CPU = 4,        /* Enable CPU usage collection. */
  CENSUS_ACTIVE_OPS = 8, /* Trace Active operations. */
  CENSUS_ALL = CENSUS_TRACING | CENSUS_STATS | CENSUS_CPU | CENSUS_ACTIVE_OPS
};

/* Shutdown and startup census subsystem. The 'functions' argument should be
 * the OR (|) of census_functions values. If census fails to initialize, then
 * census_initialize() will return a non-zero value. It is an error to call
 * census_initialize() more than once (without an intervening
 * census_shutdown()). */
int census_initialize(int functions);
void census_shutdown();

/* If any census feature has been initialized, this funtion will return a
 * non-zero value. */
int census_available();

/* Internally, Census relies on a context, which should be propagated across
 * RPC's. From the RPC subsystems viewpoint, this is an opaque data structure.
 * A context must be used as the first argument to all other census
 * functions. Conceptually, contexts should be thought of as specific to
 * single RPC/thread. The context can be serialized for passing across the
 * wire. */
typedef struct census_context census_context;

/* This function is called by the RPC subsystem whenever it needs to get a
 * serialized form of the current census context (presumably to pass across
 * the wire). Arguments:
 * 'buffer': pointer to memory into which serialized context will be placed
 * 'buf_size': size of 'buffer'
 *
 * Returns: the number of bytes used in buffer if successful, or 0 if the
 * buffer is of insufficient size.
 *
 * TODO(aveitch): determine how best to communicate required/max buffer size
 * so caller doesn't have to guess. */
size_t census_context_serialize(const census_context *context, char *buffer,
                                size_t buf_size);

/* Create a new census context, possibly from a serialized buffer. If 'buffer'
 * is non-NULL, it is assumed that it is a buffer encoded by
 * census_context_serialize(). If `buffer` is NULL, a new, empty context is
 * created. The decoded/new contest is returned in 'context'.
 *
 * Returns 0 if no errors, non-zero if buffer is incorrectly formatted, in
 * which case a new empty context will be returned. */
int census_context_deserialize(const char *buffer, census_context **context);

/* The given context is destroyed. Once destroyed, using the context in
 * future census calls will result in undefined behavior. */
void census_context_destroy(census_context *context);

/* Distributed traces can have a number of options. */
enum census_trace_mask_values {
  CENSUS_TRACE_MASK_NONE = 0,      /* Default, empty flags */
  CENSUS_TRACE_MASK_IS_SAMPLED = 1 /* RPC tracing enabled for this context. */
};

/** Get the current trace mask associated with this context. The value returned
    will be the logical or of census_trace_mask_values values. */
int census_trace_mask(const census_context *context);

/* The concept of "operation" is a fundamental concept for Census. An
   operation is a logical representation of a action in a RPC-using system. It
   is most typically used to represent a single RPC, or a significant sub-part
   thereof (e.g. a single logical "read" RPC to a distributed storage system
   might do several other actions in parallel, from looking up metadata
   indices to making requests of other services - each of these could be a
   sub-operation with the larger RPC operation. Census uses operations for the
   following:

   CPU accounting: If enabled, census will measure the thread CPU time
   consumed between operation start and end times.

   Active operations: Census will maintain information on all currently
   active operations.

   Distributed tracing: Each operation serves as a logical trace span.

   Stats collection: Stats are broken down operation (e.g. latency
   breakdown for each service/method combination).

   The following functions serve to delineate the start and stop points for
   each logical operation. */
/**
   Start a client rpc operation. This function will create a new context. If
   the context argument is non-null, then the new context will inherit all
   its properties, with the following changes:
   - create a new operation ID for the new context, marking it as a child of
     the previous operation.
   - use the new RPC service/method/peer information for tracing and stats
     collection purposes, rather than those from the original context
   - if trace_mask is non-zero, update the trace mask entries associated with
     the original context.

   If the context argument is NULL, then a new root context is created.  This
   is particularly important for tracing purposes (the trace spans generated
   will be unassociated with any other trace spans, except those
   downstream). Whatever it's value, the trace_mask will be used for tracing
   operations associated with the new context.

   @param context The base context. Can be NULL.
   @param service RPC service name. On some systems, may include other
   parts of RPC identification (e.g. host on gRPC systems).
   @param method RPC method name
   @param peer RPC peer
   @param trace_mask An or of census_trace_mask_values values
   @param start_time If NULL, the time of function call is used as the
   start time for the operation. If non-NULL, then the time should be in the
   past, when the operation was deemed to have started. This is used when
   the other information used as arguments is not yet available.

   @return A new census context.
 */
census_context *census_start_client_rpc_op(census_context *context,
                                           const char *service,
                                           const char *method, const char *peer,
                                           int trace_mask,
                                           gpr_timespec *start_time);

/**
   Indicate the start of a server rpc operation, updating the current
   context (which should have been created from census_context_deserialize()
   (as passed from the client side of the RPC operation) or census_start_op().
   - if trace_mask is non-zero, update the trace mask entries associated with
     the original context.

   @param context The base context. Cannot be NULL.
   @param service RPC service name. On some systems, may include other
   parts of RPC identification (e.g. host on gRPC systems).
   @param method RPC method name
   @param peer RPC peer
   @param trace_mask An or of census_trace_mask_values values
   @param start_time If NULL, the time of function call is used as the
   start time for the operation. If non-NULL, then the time should be in the
   past, when the operation was deemed to have started. This is used when
   the other information used as arguments is not yet available.
 */
void census_start_server_rpc_op(census_context *context, const char *service,
                                const char *method, const char *peer,
                                int trace_mask, gpr_timespec *start_time);

/**
   Start a new, non-RPC census operation. In general, this function works very
   similarly to census_start_client_rpc_op, with the primary differennce being
   the abscence of peer information, and the replacement of service and method
   names with the more general family/name. If the context argument is
   non-null, then the new context will inherit all its properties, with the
   following changes:
   - create a new operation ID for the new context, marking it as a child of
     the previous operation.
   - use the family and name information for tracing and stats collection
     purposes, rather than those from the original context
   - if trace_mask is non-zero, update the trace mask entries associated with
     the original context.

   If the context argument is NULL, then a new root context is created.  This
   is particularly important for tracing purposes (the trace spans generated
   will be unassociated with any other trace spans, except those
   downstream). Whatever it's value, the trace_mask will be used for tracing
   operations associated with the new context.

   @param context The base context. Can be NULL.
   @param family Family name to associate with the trace
   @param name Name within family to associated with traces/stats
   @param trace_mask An or of census_trace_mask_values values
   @param start_time If NULL, the time of function call is used as the
   start time for the operation. If non-NULL, then the time should be in the
   past, when the operation was deemed to have started. This is used when
   the other information used as arguments is not yet available.

   @return A new census context.
 */
census_context *census_start_op(census_context *context, const char *family,
                                const char *name, int trace_mask,
                                gpr_timespec *start_time);

/** End a tracing operation. Must be matched with an earlier
 * census_start_*_op*() call. */
void census_trace_end_op(census_context *context, int status);

/** Insert a trace record into the trace stream. The record consists of an
 * arbitrary size buffer, the size of which is provided in 'n'. */
void census_trace_print(census_context *context, const char *buffer, size_t n);

/** Retrieve active ops as a proto. Note that since we don't have proto
    manipulation available in the grpc core yet, arguments etc. are left
    unspecified for now. */
void census_get_active_ops_as_proto(/* pointer to proto */);

/** Retrieve all active trace records as a proto. Note that since we don't
    have proto manipulation available in the grpc core yet, arguments etc. are
    left unspecified for now. This function will clear existing trace
    records. */
void census_get_trace_as_proto(/* pointer to proto */);

/* A census statistic to be recorded comprises two parts: an ID for the
 * particular statistic and the value to be recorded against it. */
typedef struct {
  int id;
  double value;
} census_stat;

/* Record new stats against the given context. */
void census_record_stat(census_context *context, census_stat *stats,
                        size_t nstats);

#ifdef __cplusplus
}
#endif

#endif /* CENSUS_CENSUS_H */
