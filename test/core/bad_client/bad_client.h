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

#ifndef GRPC_TEST_CORE_BAD_CLIENT_BAD_CLIENT_H
#define GRPC_TEST_CORE_BAD_CLIENT_BAD_CLIENT_H

#include <grpc/grpc.h>
#include "test/core/util/test_config.h"

#define GRPC_BAD_CLIENT_REGISTERED_METHOD "/registered/bar"
#define GRPC_BAD_CLIENT_REGISTERED_HOST "localhost"

typedef void (*grpc_bad_client_server_side_validator)(grpc_server *server,
                                                      grpc_completion_queue *cq,
                                                      void *registered_method);

typedef void (*grpc_bad_client_client_stream_validator)(
    grpc_slice_buffer *incoming);

#define GRPC_BAD_CLIENT_DISCONNECT 1

/* Test runner.

   Create a server, and send client_payload to it as bytes from a client.
   Execute server_validator in a separate thread to assert that the bytes are
   handled as expected. */
void grpc_run_bad_client_test(
    grpc_bad_client_server_side_validator server_validator,
    grpc_bad_client_client_stream_validator client_validator,
    const char *client_payload, size_t client_payload_length, uint32_t flags);

#define GRPC_RUN_BAD_CLIENT_TEST(server_validator, client_validator, payload, \
                                 flags)                                       \
  grpc_run_bad_client_test(server_validator, client_validator, payload,       \
                           sizeof(payload) - 1, flags)

#endif /* GRPC_TEST_CORE_BAD_CLIENT_BAD_CLIENT_H */
