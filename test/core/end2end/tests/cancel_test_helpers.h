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

#ifndef GRPC_TEST_CORE_END2END_TESTS_CANCEL_TEST_HELPERS_H
#define GRPC_TEST_CORE_END2END_TESTS_CANCEL_TEST_HELPERS_H

typedef struct {
  const char *name;
  grpc_call_error (*initiate_cancel)(grpc_call *call, void *reserved);
  grpc_status_code expect_status;
  const char *expect_details;
} cancellation_mode;

static grpc_call_error wait_for_deadline(grpc_call *call, void *reserved) {
  (void)reserved;
  return GRPC_CALL_OK;
}

static const cancellation_mode cancellation_modes[] = {
    {"cancel", grpc_call_cancel, GRPC_STATUS_CANCELLED, "Cancelled"},
    {"deadline", wait_for_deadline, GRPC_STATUS_DEADLINE_EXCEEDED,
     "Deadline Exceeded"},
};

#endif /* GRPC_TEST_CORE_END2END_TESTS_CANCEL_TEST_HELPERS_H */
