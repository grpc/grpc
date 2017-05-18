/*
 *
 * Copyright 2015-2016, Google Inc.
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

/** RPC-internal Census API's. These are designed to be generic enough that
 * they can (ultimately) be used in many different RPC systems (with differing
 * implementations). */

#ifndef GRPC_CENSUS_H
#define GRPC_CENSUS_H

#include <grpc/grpc.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Identify census features that can be enabled via census_initialize(). */
enum census_features {
  CENSUS_FEATURE_NONE = 0,    /** Do not enable census. */
  CENSUS_FEATURE_TRACING = 1, /** Enable census tracing. */
  CENSUS_FEATURE_STATS = 2,   /** Enable Census stats collection. */
  CENSUS_FEATURE_CPU = 4,     /** Enable Census CPU usage collection. */
  CENSUS_FEATURE_ALL =
      CENSUS_FEATURE_TRACING | CENSUS_FEATURE_STATS | CENSUS_FEATURE_CPU
};

/** Shutdown and startup census subsystem. The 'features' argument should be
 * the OR (|) of census_features values. If census fails to initialize, then
 * census_initialize() will return -1, otherwise the set of enabled features
 * (which may be smaller than that provided in the `features` argument, see
 * census_supported()) is returned. It is an error to call census_initialize()
 * more than once (without an intervening census_shutdown()). These functions
 * are not thread-safe. */
CENSUSAPI int census_initialize(int features);
CENSUSAPI void census_shutdown(void);

/** Return the features supported by the current census implementation (not all
 * features will be available on all platforms). */
CENSUSAPI int census_supported(void);

/** Return the census features currently enabled. */
CENSUSAPI int census_enabled(void);

/**
  A Census Context is a handle used by Census to represent the current tracing
  and stats collection information. Contexts should be propagated across RPC's
  (this is the responsibility of the local RPC system). A context is typically
  used as the first argument to most census functions. Conceptually, they
  should be thought of as specific to a single RPC/thread. The user visible
  context representation is that of a collection of key:value string pairs,
  each of which is termed a 'tag'; these form the basis against which Census
  metrics will be recorded. Keys are unique within a context. */
typedef struct census_context census_context;

/** A tag is a key:value pair. Both keys and values are nil-terminated strings,
   containing printable ASCII characters (decimal 32-126). Keys must be at
   least one character in length. Both keys and values can have at most
   CENSUS_MAX_TAG_KB_LEN characters (including the terminating nil). The
   maximum number of tags that can be propagated is
   CENSUS_MAX_PROPAGATED_TAGS. Users should also remember that some systems
   may have limits on, e.g., the number of bytes that can be transmitted as
   metadata, and that larger tags means more memory consumed and time in
   processing. */
typedef struct {
  const char *key;
  const char *value;
  uint8_t flags;
} census_tag;

/** Maximum length of a tag's key or value. */
#define CENSUS_MAX_TAG_KV_LEN 255
/** Maximum number of propagatable tags. */
#define CENSUS_MAX_PROPAGATED_TAGS 255

/** Tag flags. */
#define CENSUS_TAG_PROPAGATE 1 /** Tag should be propagated over RPC */
#define CENSUS_TAG_STATS 2    /** Tag will be used for statistics aggregation */
#define CENSUS_TAG_RESERVED 4 /** Reserved for internal use. */
/** Flag values 4,8,16,32,64,128 are reserved for future/internal use. Clients
   should not use or rely on their values. */

#define CENSUS_TAG_IS_PROPAGATED(flags) (flags & CENSUS_TAG_PROPAGATE)
#define CENSUS_TAG_IS_STATS(flags) (flags & CENSUS_TAG_STATS)

/** An instance of this structure is kept by every context, and records the
   basic information associated with the creation of that context. */
typedef struct {
  int n_propagated_tags; /** number of propagated tags */
  int n_local_tags;      /** number of non-propagated (local) tags */
  int n_deleted_tags;    /** number of tags that were deleted */
  int n_added_tags;      /** number of tags that were added */
  int n_modified_tags;   /** number of tags that were modified */
  int n_invalid_tags;    /** number of tags with bad keys or values (e.g.
                            longer than CENSUS_MAX_TAG_KV_LEN) */
  int n_ignored_tags;    /** number of tags ignored because of
                            CENSUS_MAX_PROPAGATED_TAGS limit. */
} census_context_status;

/** Create a new context, adding and removing tags from an existing context.
   This will copy all tags from the 'tags' input, so it is recommended
   to add as many tags in a single operation as is practical for the client.
   @param base Base context to build upon. Can be NULL.
   @param tags A set of tags to be added/changed/deleted. Tags with keys that
   are in 'tags', but not 'base', are added to the context. Keys that are in
   both 'tags' and 'base' will have their value/flags modified. Tags with keys
   in both, but with NULL values, will be deleted from the context. Tags with
   invalid (too long or short) keys or values will be ignored.
   If adding a tag will result in more than CENSUS_MAX_PROPAGATED_TAGS in either
   binary or non-binary tags, they will be ignored, as will deletions of
   tags that don't exist.
   @param ntags number of tags in 'tags'
   @param status If not NULL, will return a pointer to a census_context_status
   structure containing information about the new context and status of the
   tags used in its creation.
   @return A new, valid census_context.
*/
CENSUSAPI census_context *census_context_create(
    const census_context *base, const census_tag *tags, int ntags,
    census_context_status const **status);

/** Destroy a context. Once this function has been called, the context cannot
   be reused. */
CENSUSAPI void census_context_destroy(census_context *context);

/** Get a pointer to the original status from the context creation. */
CENSUSAPI const census_context_status *census_context_get_status(
    const census_context *context);

/** Structure used for iterating over the tags in a context. API clients should
   not use or reference internal fields - neither their contents or
   presence/absence are guaranteed. */
typedef struct {
  const census_context *context;
  int base;
  int index;
  char *kvm;
} census_context_iterator;

/** Initialize a census_tag_iterator. Must be called before first use. */
CENSUSAPI void census_context_initialize_iterator(
    const census_context *context, census_context_iterator *iterator);

/** Get the contents of the "next" tag in the context. If there are no more
   tags, returns 0 (and 'tag' contents will be unchanged), otherwise returns 1.
   */
CENSUSAPI int census_context_next_tag(census_context_iterator *iterator,
                                      census_tag *tag);

/** Get a context tag by key. Returns 0 if the key is not present. */
CENSUSAPI int census_context_get_tag(const census_context *context,
                                     const char *key, census_tag *tag);

/** Tag set encode/decode functionality. These functions are intended
   for use by RPC systems only, for purposes of transmitting/receiving contexts.
   */

/** Encode a context into a buffer.
   @param context context to be encoded
   @param buffer buffer into which the context will be encoded.
   @param buf_size number of available bytes in buffer.
   @return The number of buffer bytes consumed for the encoded context, or
           zero if the buffer was of insufficient size. */
CENSUSAPI size_t census_context_encode(const census_context *context,
                                       char *buffer, size_t buf_size);

/** Decode context buffer encoded with census_context_encode(). Returns NULL
   if there is an error in parsing either buffer. */
CENSUSAPI census_context *census_context_decode(const char *buffer,
                                                size_t size);

/** Distributed traces can have a number of options. */
enum census_trace_mask_values {
  CENSUS_TRACE_MASK_NONE = 0,      /** Default, empty flags */
  CENSUS_TRACE_MASK_IS_SAMPLED = 1 /** RPC tracing enabled for this context. */
};

/** Get the current trace mask associated with this context. The value returned
    will be the logical OR of census_trace_mask_values values. */
CENSUSAPI int census_trace_mask(const census_context *context);

/** Set the trace mask associated with a context. */
CENSUSAPI void census_set_trace_mask(int trace_mask);

/** The concept of "operation" is a fundamental concept for Census. In an RPC
   system, an operation typically represents a single RPC, or a significant
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
  /** Use gpr_timespec for default implementation. High performance
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
CENSUSAPI census_timestamp census_start_rpc_op_timestamp(void);

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
  const char *(*get_rpc_service_name)(int64_t id);
  const char *(*get_rpc_method_name)(int64_t id);
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
CENSUSAPI census_context *census_start_client_rpc_op(
    const census_context *context, int64_t rpc_name_id,
    const census_rpc_name_info *rpc_name_info, const char *peer, int trace_mask,
    const census_timestamp *start_time);

/**
  Add peer information to a context representing a client RPC operation.
*/
CENSUSAPI void census_set_rpc_client_peer(census_context *context,
                                          const char *peer);

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
CENSUSAPI census_context *census_start_server_rpc_op(
    const char *buffer, int64_t rpc_name_id,
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
   @param name Name within family to associate with traces/stats
   @param trace_mask An OR of census_trace_mask_values values. Only used if
                     context is NULL.

   @return A new census context.
 */
CENSUSAPI census_context *census_start_op(census_context *context,
                                          const char *family, const char *name,
                                          int trace_mask);

/**
  End an operation started by any of the census_start_*_op*() calls. The
  context used in this call will no longer be valid once this function
  completes.

  @param context Context associated with operation which is ending.
  @param status status associated with the operation. Not interpreted by
                census.
*/
CENSUSAPI void census_end_op(census_context *context, int status);

#define CENSUS_TRACE_RECORD_START_OP ((uint32_t)0)
#define CENSUS_TRACE_RECORD_END_OP ((uint32_t)1)

/** Insert a trace record into the trace stream. The record consists of an
    arbitrary size buffer, the size of which is provided in 'n'.
    @param context Trace context
    @param type User-defined type to associate with trace entry.
    @param buffer Pointer to buffer to use
    @param n Number of bytes in buffer
*/
CENSUSAPI void census_trace_print(census_context *context, uint32_t type,
                                  const char *buffer, size_t n);

/** Trace record. */
typedef struct {
  census_timestamp timestamp; /** Time of record creation */
  uint64_t trace_id;          /** Trace ID associated with record */
  uint64_t op_id;             /** Operation ID associated with record */
  uint32_t type;              /** Type (as used in census_trace_print() */
  const char *buffer;         /** Buffer (from census_trace_print() */
  size_t buf_size;            /** Number of bytes inside buffer */
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
CENSUSAPI int census_trace_scan_start(int consume);

/** Get a trace record. The data pointed to by the trace buffer is guaranteed
    stable until the next census_get_trace_record() call (if the consume
    argument to census_trace_scan_start was non-zero) or census_trace_scan_end()
    is called (otherwise).
  @param trace_record structure that will be filled in with oldest trace record.
  @returns -1 if an error occurred (e.g. no previous call to
           census_trace_scan_start()), 0 if there is no more trace data (and
           trace_record will not be modified) or 1 otherwise.
*/
CENSUSAPI int census_get_trace_record(census_trace_record *trace_record);

/** End a scan previously started by census_trace_scan_start() */
CENSUSAPI void census_trace_scan_end();

/** Core stats collection API's. The following concepts are used:
   * Resource: Users record measurements for a single resource. Examples
     include RPC latency, CPU seconds consumed, and bytes transmitted.
   * Aggregation: An aggregation of a set of measurements. Census supports the
       following aggregation types:
       * Distribution - statistical distribution information, used for
         recording average, standard deviation etc. Can include a histogram.
       * Interval - a count of events that happen in a rolling time window.
   * View: A view is a combination of a Resource, a set of tag keys and an
     Aggregation. When a measurement for a Resource matches the View tags, it is
     recorded (for each unique set of tag values) using the Aggregation type.
     Each resource can have an arbitrary number of views by which it will be
     broken down.

  Census uses protos to define each of the above, and output results. This
  ensures unification across the different language and runtime
  implementations. The proto definitions can be found in src/proto/census.
*/

/** Define a new resource. `resource_pb` should contain an encoded Resource
   protobuf, `resource_pb_size` being the size of the buffer. Returns a -ve
   value on error, or a positive (>= 0) resource id (for use in
   census_delete_resource() and census_record_values()). In order to be valid, a
   resource must have a name, and at least one numerator in its unit type. The
   resource name must be unique, and an error will be returned if it is not. */
CENSUSAPI int32_t census_define_resource(const uint8_t *resource_pb,
                                         size_t resource_pb_size);

/** Delete a resource created by census_define_resource(). */
CENSUSAPI void census_delete_resource(int32_t resource_id);

/** Determine the id of a resource, given its name. returns -1 if the resource
   does not exist. */
CENSUSAPI int32_t census_resource_id(const char *name);

/** A single value to be recorded comprises two parts: an ID for the particular
 * resource and the value to be recorded against it. */
typedef struct {
  int32_t resource_id;
  double value;
} census_value;

/** Record new usage values against the given context. */
CENSUSAPI void census_record_values(census_context *context,
                                    census_value *values, size_t nvalues);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CENSUS_H */
