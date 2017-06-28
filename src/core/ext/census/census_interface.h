/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
