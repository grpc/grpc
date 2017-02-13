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

#ifndef GRPC_TEST_CORE_UTIL_TEST_CONFIG_H
#define GRPC_TEST_CORE_UTIL_TEST_CONFIG_H

#include <grpc/support/time.h>

#ifdef __cplusplus
extern "C" {
#endif /*  __cplusplus */

extern int64_t g_fixture_slowdown_factor;
extern int64_t g_poller_slowdown_factor;

/* Returns an appropriate scaling factor for timeouts. */
int64_t grpc_test_slowdown_factor();

/* Converts a given timeout (in seconds) to a deadline. */
gpr_timespec grpc_timeout_seconds_to_deadline(int64_t time_s);

/* Converts a given timeout (in milliseconds) to a deadline. */
gpr_timespec grpc_timeout_milliseconds_to_deadline(int64_t time_ms);

#ifndef GRPC_TEST_CUSTOM_PICK_PORT
#define GRPC_TEST_PICK_PORT
#endif

void grpc_test_init(int argc, char **argv);

#ifdef __cplusplus
}
#endif /*  __cplusplus */

#endif /* GRPC_TEST_CORE_UTIL_TEST_CONFIG_H */
