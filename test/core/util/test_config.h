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

#ifndef GRPC_TEST_CORE_UTIL_TEST_CONFIG_H
#define GRPC_TEST_CORE_UTIL_TEST_CONFIG_H

#include <grpc/support/time.h>

extern int64_t g_fixture_slowdown_factor;
extern int64_t g_poller_slowdown_factor;

/* Returns an appropriate scaling factor for timeouts. */
int64_t grpc_test_slowdown_factor();

/* Converts a given timeout (in seconds) to a deadline. */
gpr_timespec grpc_timeout_seconds_to_deadline(int64_t time_s);

/* Converts a given timeout (in milliseconds) to a deadline. */
gpr_timespec grpc_timeout_milliseconds_to_deadline(int64_t time_ms);

#if !defined(GRPC_TEST_CUSTOM_PICK_PORT) && !defined(GRPC_PORT_ISOLATED_RUNTIME)
#define GRPC_TEST_PICK_PORT
#endif

void grpc_test_init(int argc, char** argv);

#endif /* GRPC_TEST_CORE_UTIL_TEST_CONFIG_H */
