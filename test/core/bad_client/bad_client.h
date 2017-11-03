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

#ifndef GRPC_TEST_CORE_BAD_CLIENT_BAD_CLIENT_H
#define GRPC_TEST_CORE_BAD_CLIENT_BAD_CLIENT_H

#include <grpc/grpc.h>

#include <stdbool.h>

#include "test/core/util/test_config.h"

#define GRPC_BAD_CLIENT_REGISTERED_METHOD "/registered/bar"
#define GRPC_BAD_CLIENT_REGISTERED_HOST "localhost"

typedef void (*grpc_bad_client_server_side_validator)(grpc_server* server,
                                                      grpc_completion_queue* cq,
                                                      void* registered_method);

// Returns false if we need to read more data.
typedef bool (*grpc_bad_client_client_stream_validator)(
    grpc_slice_buffer* incoming);

#define GRPC_BAD_CLIENT_DISCONNECT 1
#define GRPC_BAD_CLIENT_LARGE_REQUEST 2

/* Test runner.

   Create a server, and send client_payload to it as bytes from a client.
   Execute server_validator in a separate thread to assert that the bytes are
   handled as expected. */
void grpc_run_bad_client_test(
    grpc_bad_client_server_side_validator server_validator,
    grpc_bad_client_client_stream_validator client_validator,
    const char* client_payload, size_t client_payload_length, uint32_t flags);

#define GRPC_RUN_BAD_CLIENT_TEST(server_validator, client_validator, payload, \
                                 flags)                                       \
  grpc_run_bad_client_test(server_validator, client_validator, payload,       \
                           sizeof(payload) - 1, flags)

#endif /* GRPC_TEST_CORE_BAD_CLIENT_BAD_CLIENT_H */
