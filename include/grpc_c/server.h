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

#ifndef GRPC_C_SERVER_H
#define GRPC_C_SERVER_H

#include <grpc_c/grpc_c.h>

typedef struct GRPC_build_server_options {
  int max_message_size;
} GRPC_build_server_options;

/**
 * Creates a server instance.
 */
GRPC_server *GRPC_build_server(GRPC_build_server_options options);

/**
 * Gets a new completion queue from the server used to monitor incoming
 * requests.
 * There is no need to manually free this queue.
 */
GRPC_incoming_notification_queue *GRPC_server_new_incoming_queue(
    GRPC_server *server);

/**
 * Instructs the server to listening on an address:port combination.
 */
void GRPC_server_listen_host(GRPC_server *server, const char *host);

/**
 * Start listening for requests. The server cannot be modified beyond this
 * point.
 */
void GRPC_server_start(GRPC_server *server);

/**
 * A blocking call. Shuts down the server.
 * After it returns, no new calls or connections will be admitted.
 * Existing calls will be allowed to complete.
 */
void GRPC_server_shutdown(GRPC_server *server);

/**
 * Frees the server and associated resources.
 */
void GRPC_server_destroy(GRPC_server *server);

#endif /* GRPC_C_SERVER_H */
