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

#ifndef GRPC_CORE_EXT_CENSUS_CENSUS_INTERFACE_H
#define GRPC_CORE_EXT_CENSUS_CENSUS_INTERFACE_H

#include <grpc/support/port_platform.h>

/* Maximum length of an individual census trace annotation. */
#define CENSUS_MAX_ANNOTATION_LENGTH 200

/* Structure of a census op id. Define as structure because 64bit integer is not
   available on every platform for C89. */
typedef struct census_op_id {
  uint32_t upper;
  uint32_t lower;
} census_op_id;

typedef struct census_rpc_stats census_rpc_stats;

/* Initializes Census library. No-op if Census is already initialized. */
void census_init(void);

/* Shutdown Census Library. */
void census_shutdown(void);

/* Annotates grpc method name on a census_op_id. The method name has the format
   of <full quantified rpc service name>/<rpc function name>. Returns 0 iff
   op_id and method_name are all valid. op_id is valid after its creation and
   before calling census_tracing_end_op().

   TODO(hongyu): Figure out valid characters set for service name and command
   name and document requirements here.*/
int census_add_method_tag(census_op_id op_id, const char *method_name);

/* Annotates tracing information to a specific op_id.
   Up to CENSUS_MAX_ANNOTATION_LENGTH bytes are recorded. */
void census_tracing_print(census_op_id op_id, const char *annotation);

/* Starts tracing for an RPC. Returns a locally unique census_op_id */
census_op_id census_tracing_start_op(void);

/* Ends tracing. Calling this function will invalidate the input op_id. */
void census_tracing_end_op(census_op_id op_id);

#endif /* GRPC_CORE_EXT_CENSUS_CENSUS_INTERFACE_H */
