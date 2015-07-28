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
  CENSUS_NONE = 0,    /* Do not enable census. */
  CENSUS_TRACING = 1, /* Enable census tracing. */
  CENSUS_STATS = 2,   /* Enable Census stats collection. */
  CENSUS_CPU = 4,     /* Enable Census CPU usage collection. */
  CENSUS_ALL = CENSUS_TRACING | CENSUS_STATS | CENSUS_CPU
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
   positive values: the number of tags in the tag set. */
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

#ifdef __cplusplus
}
#endif

#endif /* CENSUS_CENSUS_H */
