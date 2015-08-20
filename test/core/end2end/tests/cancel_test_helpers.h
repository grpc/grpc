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
