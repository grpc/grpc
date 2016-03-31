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

#ifndef GRPC_CORE_EXT_CENSUS_CENSUS_TRACING_H
#define GRPC_CORE_EXT_CENSUS_CENSUS_TRACING_H

#include <grpc/support/time.h>
#include "src/core/ext/census/census_rpc_stats.h"

/* WARNING: The data structures and APIs provided by this file are for GRPC
   library's internal use ONLY. They might be changed in backward-incompatible
   ways and are not subject to any deprecation policy.
   They are not recommended for external use.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* Struct for a trace annotation. */
typedef struct census_trace_annotation {
  gpr_timespec ts;                            /* timestamp of the annotation */
  char txt[CENSUS_MAX_ANNOTATION_LENGTH + 1]; /* actual txt annotation */
  struct census_trace_annotation *next;
} census_trace_annotation;

typedef struct census_trace_obj {
  census_op_id id;
  gpr_timespec ts;
  census_rpc_stats rpc_stats;
  char *method;
  census_trace_annotation *annotations;
} census_trace_obj;

/* Deletes trace object. */
void census_trace_obj_destroy(census_trace_obj *obj);

/* Initializes trace store. This function is thread safe. */
void census_tracing_init(void);

/* Shutsdown trace store. This function is thread safe. */
void census_tracing_shutdown(void);

/* Gets trace obj corresponding to the input op_id. Returns NULL if trace store
   is not initialized or trace obj is not found. Requires trace store being
   locked before calling this function. */
census_trace_obj *census_get_trace_obj_locked(census_op_id op_id);

/* The following two functions acquire and release the trace store global lock.
   They are for census internal use only. */
void census_internal_lock_trace_store(void);
void census_internal_unlock_trace_store(void);

/* Gets method name associated with the input trace object. */
const char *census_get_trace_method_name(const census_trace_obj *trace);

/* Returns an array of pointers to trace objects of currently active operations
   and fills in number of active operations. Returns NULL if there are no active
   operations.
   Caller owns the returned objects. */
census_trace_obj **census_get_active_ops(int *num_active_ops);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_CENSUS_CENSUS_TRACING_H */
