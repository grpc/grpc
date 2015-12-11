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

/* Identify census features that can be enabled via census_initialize(). */
enum census_features {
  CENSUS_FEATURE_NONE = 0,    /* Do not enable census. */
  CENSUS_FEATURE_TRACING = 1, /* Enable census tracing. */
  CENSUS_FEATURE_STATS = 2,   /* Enable Census stats collection. */
  CENSUS_FEATURE_CPU = 4,     /* Enable Census CPU usage collection. */
  CENSUS_FEATURE_ALL =
      CENSUS_FEATURE_TRACING | CENSUS_FEATURE_STATS | CENSUS_FEATURE_CPU
};

/** Shutdown and startup census subsystem. The 'features' argument should be
 * the OR (|) of census_features values. If census fails to initialize, then
 * census_initialize() will return a non-zero value. It is an error to call
 * census_initialize() more than once (without an intervening
 * census_shutdown()). */
int census_initialize(int features);
void census_shutdown(void);

/** Return the features supported by the current census implementation (not all
 * features will be available on all platforms). */
int census_supported(void);

/** Return the census features currently enabled. */
int census_enabled(void);

/**
  Context is a handle used by census to represent the current tracing and
  tagging information. Contexts should be propagated across RPC's. Contexts
  are created by any of the census_start_*_op() functions. A context is
  typically used as argument to most census functions. Conceptually, contexts
  should be thought of as specific to single RPC/thread. The context can be
  serialized for passing across the wire, via census_context_serialize().
*/
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

/* Distributed traces can have a number of options. */
enum census_trace_mask_values {
  CENSUS_TRACE_MASK_NONE = 0,      /* Default, empty flags */
  CENSUS_TRACE_MASK_IS_SAMPLED = 1 /* RPC tracing enabled for this context. */
};

/** Get the current trace mask associated with this context. The value returned
    will be the logical or of census_trace_mask_values values. */
int census_trace_mask(const census_context *context);

/** Set the trace mask associated with a context. */
void census_set_trace_mask(int trace_mask);

/* The concept of "operation" is a fundamental concept for Census. In an RPC
   system, and operation typcially represents a single RPC, or a significant
   sub-part thereof (e.g. a single logical "read" RPC to a distributed storage
   system might do several other actions in parallel, from looking up metadata
   indices to making requests of other services - each of these could be a
   sub-operation with the larger RPC operation). Census uses operations for the
   following:

   CPU accounting: If enabled, census will measure the thread CPU time
   consumed between operation start and end times.

   Active operations: Census will maintain information on all currently
   active operations.

   Distributed tracing: Each operation serves as a logical trace span.

   Stats collection: Stats are broken down by operation (e.g. latency
   breakdown for each unique RPC path).

   The following functions serve to delineate the start and stop points for
   each logical operation. */

/**
  This structure represents a timestamp as used by census to record the time
  at which an operation begins.
*/
typedef struct {
  /* Use gpr_timespec for default implementation. High performance
   * implementations should use a cycle-counter based timestamp. */
  gpr_timespec ts;
} census_timestamp;

/**
  Mark the beginning of an RPC operation. The information required to call the
  functions to record the start of RPC operations (both client and server) may
  not be callable at the true start time of the operation, due to information
  not being available (e.g. the census context data will not be available in a
  server RPC until at least initial metadata has been processed). To ensure
  correct CPU accounting and latency recording, RPC systems can call this
  function to get the timestamp of operation beginning. This can later be used
  as an argument to census_start_{client,server}_rpc_op(). NB: for correct
  CPU accounting, the system must guarantee that the same thread is used
  for all request processing after this function is called.

  @return A timestamp representing the operation start time.
*/
census_timestamp census_start_rpc_op_timestamp(void);

/**
  Represent functions to map RPC name ID to service/method names. Census
  breaks down all RPC stats by service and method names. We leave the
  definition and format of these to the RPC system. For efficiency purposes,
  we encode these as a single 64 bit identifier, and allow the RPC system to
  provide a structure for functions that can convert these to service and
  method strings.

  TODO(aveitch): Instead of providing this as an argument to the rpc_start_op()
  functions, maybe it should be set once at census initialization.
*/
typedef struct {
  const char *(*get_rpc_service_name)(gpr_int64 id);
  const char *(*get_rpc_method_name)(gpr_int64 id);
} census_rpc_name_info;

/**
   Start a client rpc operation. This function should be called as early in the
   client RPC path as possible. This function will create a new context. If
   the context argument is non-null, then the new context will inherit all
   its properties, with the following changes:
   - create a new operation ID for the new context, marking it as a child of
     the previous operation.
   - use the new RPC path and peer information for tracing and stats
     collection purposes, rather than those from the original context

   If the context argument is NULL, then a new root context is created. This
   is particularly important for tracing purposes (the trace spans generated
   will be unassociated with any other trace spans, except those
   downstream). The trace_mask will be used for tracing operations associated
   with the new context.

   In some RPC systems (e.g. where load balancing is used), peer information
   may not be available at the time the operation starts. In this case, use a
   NULL value for peer, and set it later using the
   census_set_rpc_client_peer() function.

   @param context The parent context. Can be NULL.
   @param rpc_name_id The rpc name identifier to be associated with this RPC.
   @param rpc_name_info Used to decode rpc_name_id.
   @param peer RPC peer. If not available at the time, NULL can be used,
               and a later census_set_rpc_client_peer() call made.
   @param trace_mask An OR of census_trace_mask_values values. Only used in
                     the creation of a new root context (context == NULL).
   @param start_time A timestamp returned from census_start_rpc_op_timestamp().
                     Can be NULL. Used to set the true time the operation
                     begins.

   @return A new census context.
 */
census_context *census_start_client_rpc_op(
    const census_context *context, gpr_int64 rpc_name_id,
    const census_rpc_name_info *rpc_name_info, const char *peer, int trace_mask,
    const census_timestamp *start_time);

/**
  Add peer information to a context representing a client RPC operation.
*/
void census_set_rpc_client_peer(census_context *context, const char *peer);

/**
   Start a server RPC operation. Returns a new context to be used in future
   census calls. If buffer is non-NULL, then the buffer contents should
   represent the client context, as generated by census_context_serialize().
   If buffer is NULL, a new root context is created.

   @param buffer Buffer containing bytes output from census_context_serialize().
   @param rpc_name_id The rpc name identifier to be associated with this RPC.
   @param rpc_name_info Used to decode rpc_name_id.
   @param peer RPC peer.
   @param trace_mask An OR of census_trace_mask_values values. Only used in
                     the creation of a new root context (buffer == NULL).
   @param start_time A timestamp returned from census_start_rpc_op_timestamp().
                     Can be NULL. Used to set the true time the operation
                     begins.

   @return A new census context.
 */
census_context *census_start_server_rpc_op(
    const char *buffer, gpr_int64 rpc_name_id,
    const census_rpc_name_info *rpc_name_info, const char *peer, int trace_mask,
    census_timestamp *start_time);

/**
   Start a new, non-RPC operation. In general, this function works very
   similarly to census_start_client_rpc_op, with the primary difference being
   the replacement of host/path information with the more generic family/name
   tags. If the context argument is non-null, then the new context will
   inherit all its properties, with the following changes:
   - create a new operation ID for the new context, marking it as a child of
     the previous operation.
   - use the family and name information for tracing and stats collection
     purposes, rather than those from the original context

   If the context argument is NULL, then a new root context is created. This
   is particularly important for tracing purposes (the trace spans generated
   will be unassociated with any other trace spans, except those
   downstream). The trace_mask will be used for tracing
   operations associated with the new context.

   @param context The base context. Can be NULL.
   @param family Family name to associate with the trace
   @param name Name within family to associated with traces/stats
   @param trace_mask An OR of census_trace_mask_values values. Only used if
                     context is NULL.

   @return A new census context.
 */
census_context *census_start_op(census_context *context, const char *family,
                                const char *name, int trace_mask);

/**
  End an operation started by any of the census_start_*_op*() calls. The
  context used in this call will no longer be valid once this function
  completes.

  @param context Context associated with operation which is ending.
  @param status status associated with the operation. Not interpreted by
                census.
*/
void census_end_op(census_context *context, int status);

#define CENSUS_TRACE_RECORD_START_OP ((gpr_uint32)0)
#define CENSUS_TRACE_RECORD_END_OP ((gpr_uint32)1)

/** Insert a trace record into the trace stream. The record consists of an
    arbitrary size buffer, the size of which is provided in 'n'.
    @param context Trace context
    @param type User-defined type to associate with trace entry.
    @param buffer Pointer to buffer to use
    @param n Number of bytes in buffer
*/
void census_trace_print(census_context *context, gpr_uint32 type,
                        const char *buffer, size_t n);

/** Trace record. */
typedef struct {
  census_timestamp timestamp; /* Time of record creation */
  gpr_uint64 trace_id;        /* Trace ID associated with record */
  gpr_uint64 op_id;           /* Operation ID associated with record */
  gpr_uint32 type;            /* Type (as used in census_trace_print() */
  const char *buffer;         /* Buffer (from census_trace_print() */
  size_t buf_size;            /* Number of bytes inside buffer */
} census_trace_record;

/** Start a scan of existing trace records. While a scan is ongoing, addition
    of new trace records will be blocked if the underlying trace buffers
    fill up, so trace processing systems should endeavor to complete
    reading as soon as possible.
  @param consume if non-zero, indicates that reading records also "consumes"
         the previously read record - i.e. releases space in the trace log
         while scanning is ongoing.
  @returns 0 on success, non-zero on failure (e.g. if a scan is already ongoing)
*/
int census_trace_scan_start(int consume);

/** Get a trace record. The data pointed to by the trace buffer is guaranteed
    stable until the next census_get_trace_record() call (if the consume
    argument to census_trace_scan_start was non-zero) or census_trace_scan_end()
    is called (otherwise).
  @param trace_record structure that will be filled in with oldest trace record.
  @returns -1 if an error occurred (e.g. no previous call to
           census_trace_scan_start()), 0 if there is no more trace data (and
           trace_record will not be modified) or 1 otherwise.
*/
int census_get_trace_record(census_trace_record *trace_record);

/** End a scan previously started by census_trace_scan_start() */
void census_trace_scan_end();

/* Max number of characters in tag key */
#define CENSUS_MAX_TAG_KEY_LENGTH 20
/* Max number of tag value characters */
#define CENSUS_MAX_TAG_VALUE_LENGTH 50

/* A Census tag set is a collection of key:value string pairs; these form the
   basis against which Census metrics will be recorded. Keys are unique within
   a tag set. All contexts have an associated tag set. */
typedef struct census_tag_set census_tag_set;

/* Returns a pointer to a newly created, empty tag set. If size_hint > 0,
   indicates that the tag set is intended to hold approximately that number
   of tags. */
census_tag_set *census_tag_set_create(size_t size_hint);

/* Add a new tag key/value to an existing tag set; if the tag key already exists
   in the tag set, then its value is overwritten with the new one. Can also be
   used to delete a tag, by specifying a NULL value. If key is NULL, returns
   the number of tags in the tag set.
   Return values:
   -1: invalid length key or value
   non-negative value: the number of tags in the tag set. */
int census_tag_set_add(census_tag_set *tags, const char *key,
                       const char *value);

/* Destroys a tag set. This function must be called to prevent memory leaks.
   Once called, the tag set cannot be used again. */
void census_tag_set_destroy(census_tag_set *tags);

/* Get a contexts tag set. */
census_tag_set *census_context_tag_set(census_context *context);

/* A read-only representation of a tag for use by census clients. */
typedef struct {
  size_t key_len;    /* Number of bytes in tag key. */
  const char *key;   /* A pointer to the tag key. May not be null-terminated. */
  size_t value_len;  /* Number of bytes in tag value. */
  const char *value; /* Pointer to the tag value. May not be null-terminated. */
} census_tag_const;

/* Used to iterate through a tag sets contents. */
typedef struct census_tag_set_iterator census_tag_set_iterator;

/* Open a tag set for iteration. The tag set must not be modified while
   iteration is ongoing. Returns an iterator for use in following functions. */
census_tag_set_iterator *census_tag_set_open(census_tag_set *tags);

/* Get the next tag in the tag set, by writing into the 'tag' argument. Returns
   1 if there is a "next" tag, 0 if there are no more tags. */
int census_tag_set_next(census_tag_set_iterator *it, census_tag_const *tag);

/* Close an iterator opened by census_tag_set_open(). The iterator will be
   invalidated, and should not be used once close is called. */
void census_tag_set_close(census_tag_set_iterator *it);

/* Core stats collection API's. The following concepts are used:
   * Aggregation: A collection of values. Census supports the following
       aggregation types:
         Sum - a single summation type. Typically used for keeping (e.g.)
           counts of events.
         Distribution - statistical distribution information, used for
           recording average, standard deviation etc.
         Histogram - a histogram of measurements falling in defined bucket
           boundaries.
         Window - a count of events that happen in reolling time window.
     New aggregation types can be added by the user, if desired (see
     census_register_aggregation()).
   * Metric: Each measurement is for a single metric. Examples include RPC
     latency, CPU seconds consumed, and bytes transmitted.
   * View: A view is a combination of a metric, a tag set (in which the tag
     values are regular expressions) and a set of aggregations. When a
     measurement for a metric matches the view tags, it is recorded (for each
     unique set of tags) against each aggregation. Each metric can have an
     arbitrary number of views by which it will be broken down.
*/

/* A single value to be recorded comprises two parts: an ID for the particular
 * metric and the value to be recorded against it. */
typedef struct {
  gpr_uint32 metric_id;
  double value;
} census_value;

/* Record new usage values against the given context. */
void census_record_values(census_context *context, census_value *values,
                          size_t nvalues);

/** Type representing a particular aggregation */
typedef struct census_aggregation_ops census_aggregation_ops;

/* Predefined aggregation types, for use with census_view_create(). */
extern census_aggregation_ops census_agg_sum;
extern census_aggregation_ops census_agg_distribution;
extern census_aggregation_ops census_agg_histogram;
extern census_aggregation_ops census_agg_window;

/** Information needed to instantiate a new aggregation. Used in view
    construction via census_define_view(). */
typedef struct {
  const census_aggregation_ops *ops;
  const void *
      create_arg; /* Argument to be used for aggregation initialization. */
} census_aggregation;

/** A census view type. Opaque. */
typedef struct census_view census_view;

/** Create a new view.
  @param metric_id Metric with which this view is associated.
  @param tags tags that define the view
  @param aggregations aggregations to associate with the view
  @param naggregations number of aggregations

  @return A new census view
*/
census_view *census_view_create(gpr_uint32 metric_id,
                                const census_tag_set *tags,
                                const census_aggregation *aggregations,
                                size_t naggregations);

/** Destroy a previously created view. */
void census_view_delete(census_view *view);

/** Metric ID associated with a view */
size_t census_view_metric(const census_view *view);

/** Number of aggregations associated with view. */
size_t census_view_naggregations(const census_view *view);

/** Get tags associated with view. */
const census_tag_set *census_view_tags(const census_view *view);

/** Get aggregation descriptors associated with a view. */
const census_aggregation *census_view_aggregrations(const census_view *view);

/** Holds all the aggregation data for a particular view instantiation. Forms
  part of the data returned by census_view_data(). */
typedef struct {
  const census_tag_set *tags; /* Tags for this set of aggregations. */
  const void **data; /* One data set for every aggregation in the view. */
} census_view_aggregation_data;

/** Census view data as returned by census_view_get_data(). */
typedef struct {
  size_t n_tag_sets; /* Number of unique tag sets that matched view. */
  const census_view_aggregation_data *data; /* n_tag_sets entries */
} census_view_data;

/** Get data from aggregations associated with a view.
  @param view View from which to get data.
  @return Full set of data for all aggregations for the view.
*/
const census_view_data *census_view_get_data(const census_view *view);

/** Reset all view data to zero for the specified view */
void census_view_reset(census_view *view);

#ifdef __cplusplus
}
#endif

#endif /* CENSUS_CENSUS_H */
