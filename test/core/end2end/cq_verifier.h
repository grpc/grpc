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

#ifndef GRPC_TEST_CORE_END2END_CQ_VERIFIER_H
#define GRPC_TEST_CORE_END2END_CQ_VERIFIER_H

#include <stdbool.h>

#include <grpc/grpc.h>
#include "test/core/util/test_config.h"

/* A cq_verifier can verify that expected events arrive in a timely fashion
   on a single completion queue */

typedef struct cq_verifier cq_verifier;

/* construct/destroy a cq_verifier */
cq_verifier* cq_verifier_create(grpc_completion_queue* cq);
void cq_verifier_destroy(cq_verifier* v);

/* ensure all expected events (and only those events) are present on the
   bound completion queue within \a timeout_sec */
void cq_verify(cq_verifier* v, int timeout_sec = 10);

/* ensure that the completion queue is empty */
void cq_verify_empty(cq_verifier* v);

/* ensure that the completion queue is empty, waiting up to \a timeout secs. */
void cq_verify_empty_timeout(cq_verifier* v, int timeout_sec);

/* Various expectation matchers
   Any functions taking ... expect a NULL terminated list of key/value pairs
   (each pair using two parameter slots) of metadata that MUST be present in
   the event. */
void cq_expect_completion(cq_verifier* v, const char* file, int line, void* tag,
                          bool success);
/* If the \a tag is seen, \a seen is set to true. */
void cq_maybe_expect_completion(cq_verifier* v, const char* file, int line,
                                void* tag, bool success, bool* seen);
void cq_expect_completion_any_status(cq_verifier* v, const char* file, int line,
                                     void* tag);
#define CQ_EXPECT_COMPLETION(v, tag, success) \
  cq_expect_completion(v, __FILE__, __LINE__, tag, success)
#define CQ_MAYBE_EXPECT_COMPLETION(v, tag, success, seen) \
  cq_maybe_expect_completion(v, __FILE__, __LINE__, tag, success, seen)
#define CQ_EXPECT_COMPLETION_ANY_STATUS(v, tag) \
  cq_expect_completion_any_status(v, __FILE__, __LINE__, tag)

int byte_buffer_eq_slice(grpc_byte_buffer* bb, grpc_slice b);
int byte_buffer_eq_string(grpc_byte_buffer* bb, const char* str);
int contains_metadata(grpc_metadata_array* array, const char* key,
                      const char* value);
int contains_metadata_slices(grpc_metadata_array* array, grpc_slice key,
                             grpc_slice value);

#endif /* GRPC_TEST_CORE_END2END_CQ_VERIFIER_H */
