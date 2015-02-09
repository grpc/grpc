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

#ifndef __GRPC_CENSUS_H__
#define __GRPC_CENSUS_H__

#ifdef __cplusplus
extern "C" {
#endif

/* A Census tag set is a collection of key:value string pairs; these form the
   basis against which Census resource measures will be recorded. */
typedef struct census_tag_set census_tag_set;

/* Add a new tag key/value to an existing tag set; if the tag key already exists
   in the tag set, then its value is overwritten with the new one. */
void census_tag_set_add(census_tag_set *tags, const char *key,
                        const char *value);

/* Empty an existing tag set; This *must* be called if any tags have been
   added to the tag set in order to avoid memory leaks. */
void census_tag_set_reset(census_tag_set *tags);

/* Census context contains tracking information on current tracing and
   resource measurement facilities. */
typedef struct census_context census_context;

/* Mark the logical start of a new Census operation, and resturns a new
   context. For tracing, this will generate a new span. The child context will
   be initialized using the contents of 'parent' and 'tags' (both of which can
   be NULL; if 'parent' is NULL, this is intended to be the root of a new
   trace). census_op_end() should be called when the operation completes.
*/
census_context *census_op_start(const census_context *parent,
                                const census_tag_set *tags);

/* Mark the end of a census context usage. The 'context' argument will be
   invalidated, and should not be used again. */
void census_end_op(census_context *context);

/* Insert a trace annotation. The string 's; is inserted into the trace
   record. */
void census_trace(const census_context *context, const char *s);

/* Record a metric (with given 'name' and 'value') against a 'context'. */
void census_record_metric(census_context *context, const char *name,
                          double value);

/* Serialize a census to a string buffer. This is intended for use by some RPC
   systems. The size of the buffer is passed as 'n'. The return value is the
   number of bytes consumed by the serialized context, or 0 if the buffer
   provided was too small. */
size_t census_context_serialize(const census_context *context, char *buffer,
                                size_t n);

/* Deserialize into 'context' a buffer previously constructed from
 * census_context_serialize(). Existing contents of the context are
 * overwritten. */
void census_context_deserialize(census_context *context, const char *buffer);

#ifdef __cplusplus
}
#endif

#endif /* __GRPC_CENSUS_H__ */
