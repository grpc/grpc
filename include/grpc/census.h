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

/* A census statistic to be recorded comprises two parts: an ID for the
 * particular statistic and the value to be recorded against it. */
typedef struct {
  int id;
  double value;
} census_stat;

/* Record new stats against the given context. */
void census_record_stat(census_context *context, census_stat *stats,
                        size_t nstats);

/* Stats Configuration - Census clients can use these functions and structures
   to extend and define what stats get recorded for what measurements. */

/** Stats types supported by census. */
typedef enum {
  CENSUS_STAT_SCALAR = 0,       /* Simple scalar */
  CENSUS_STAT_DISTRIBUTION = 1, /* count, average, variance */
  CENSUS_STAT_HISTOGRAM = 2,    /* Histogram. */
  CENSUS_STAT_WINDOW = 3,       /* Count over a time window. */
  CENSUS_STAT_NSTATS = 4        /* Total number of stats types. */
} census_stat_type;

/*
  Each stats type differs in how it is initialized, how it is represented, and
  the results it provides. The following structures allow us to use a generic
  type for each of those.

  Types referenced (one for each stat type in census_stat_type):
*/

typedef struct census_stat_scalar_create_arg census_stat_scalar_create_arg;
typedef struct census_stat_distribution_create_arg
    census_stat_distribution_create_arg;
typedef struct census_stat_histogram_create_arg
    census_stat_histogram_create_arg;
typedef struct census_stat_window_create_arg census_stat_window_create_arg;

/**
  Type for representing information to construct a new instance of a given
  stats type (e.g. histogram bucket boundaries).
*/
typedef struct {
  census_stat_type stat_type; /* The "real" type of the stat. */
  union {
    const census_stat_scalar_create_arg *scalar_arg;
    const census_stat_distribution_create_arg *distribution_arg;
    const census_stat_histogram_create_arg *histogram_arg;
    const census_stat_window_create_arg *window_arg;
  }
} census_stat_create_arg;

/**
  Type for representing a single stats result. */
typedef struct {
  const census_tag_set *view; /* Unique tags associated with this result. */
  census_stat_type stat_type;
  union {
    const census_stat_scalar_result *scalar_result;
    const census_stat_distribution_result *distribution_result;
    const census_stat_histogram_result *histogram_result;
    const census_stat_window_result *window_result;
  }
} census_stat_result;

/**
  Generic type for representing a stat "object".
*/
typdef struct {
  census_stat_type stat_type;
  union {
    census_stat_scalar *scalar;
    census_stat_distribution *distribution;
    census_stat_histogram *histogram;
    census_stat_window *window;
  }
} census_stat;

/**
  Structure holding function pointers and associated information needed to
  manipulate a statstics "object". Every stats type must provide an instance
  of this structure. */
typedef struct {
  /* Create a new statistic. The pointer returned can be used in future calls
     to clone_stat(), destroy_stat(), record_stat() and get_stats(). */
  (census_stat *) (*create_stat)(const census_stat_create_arg *create_arg);
  /* Create a new statistic, using an existing one as base. */
  (census_stat *) (*clone_stat)(const census_stat *stat);
  /* destroy a stats object created by {create,clone}_stat(). */
  (void) (*destroy_stat)(census_stat *stat);
  /* Record a new value against a given statistics object instance. */
  (void) (*record_stat)(census_stat *stat, double value);
  /* Return current state of a stat. The object returned can be freed by
     using destroy_stats_result(). */
  (const census_stat_result *) (*get_stat)(const census_stat *stat);
  /* destroy a stats result object, as returned from get_stat(). */
  (void) (*destroy_stats_result)(census_stat_result *result);
  /* Reset a stats values. */
  (void) (*reset_stat)(census_stat *stat);
} census_stat;

gpr_int32 census_define_view(const census_tag_set *view);

gpr_int32 census_define_stat(gpr_int32 metric_id, gpr_int32 view_id,
                             const census_stat *stat,
                             const census_stat_create_arg *create_arg);

census_stat_result *census_get_stat(gpr_int32 stat_id, gpr_int32 *nstats);

#ifdef __cplusplus
}
#endif

#endif /* CENSUS_CENSUS_H */
