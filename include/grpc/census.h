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
         Scalar - a single scalar value. Typically used for keeping (e.g.)
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
void census_record(census_context *context, census_value *values,
                   size_t nvalues);

/** Structure used to describe an aggregation type. */
typedef struct {
  /* Create a new aggregation. The pointer returned can be used in future calls
     to free(), record(), data() and reset(). */
  void *(*create)(const void *create_arg);
  /* Destroy an aggregation created by create() */
  void (*free)(void *aggregation);
  /* Record a new value against aggregation. */
  void (*record)(void *aggregation, double value);
  /* Return current aggregation data. The caller must cast this object into
     the correct type for the aggregation result. The object returned can be
     freed by using free_data(). */
  const void *(*data)(const void *aggregation);
  /* free data returned by data() */
  void (*free_data)(const void *data);
  /* Reset an aggregation to default (zero) values. */
  void (*reset)(void *aggregation);
  /* Merge 'from' aggregation into 'to'. Both aggregations must be compatible */
  void (*merge)(void *to, const void *from);
  /* Fill buffer with printable string version of aggregation contents. For
     debugging only. Returns the number of bytes added to buffer (a value == n
     implies the buffer was of insufficient size). */
  size_t (*print)(const void *aggregation, char *buffer, size_t n);
} census_aggregation;

/* Predefined aggregation types. */
extern census_aggregation census_agg_scalar;
extern census_aggregation census_agg_distribution;
extern census_aggregation census_agg_histogram;
extern census_aggregation census_agg_window;

/** Information needed to instantiate a new aggregation. Used in view
    construction via census_define_view(). */
typedef struct {
  const census_aggregation *aggregation;
  const void
      *create_arg; /* Argument to be used for aggregation initialization. */
} census_aggregation_descriptor;

/** A census view type. Opaque. */
typedef struct census_view census_view;

/** Create a new view.
  @param metric_id Metric with which this view is associated.
  @param tags tags that define the view
  @param aggregations aggregations to associate with the view
  @param naggregations number of aggregations

  @return A new census view
*/
census_view *census_view_create(
    gpr_uint32 metric_id, const census_tag_set *tags,
    const census_aggregation_descriptor *aggregations, size_t naggregations);

/** Destroy a previously created view. */
void census_view_delete(census_view *view);

/** Metric ID associated with a view */
size_t census_view_metric(const census_view *view);

/** Number of aggregations associated with view. */
size_t census_view_naggregations(const census_view *view);

/** Get tags associated with view. */
const census_tag_set *census_view_tags(const census_view *view);

/** Get aggregation descriptors associated with a view. */
const census_aggregation_descriptor *census_view_aggregrations(
    const census_view *view);

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
