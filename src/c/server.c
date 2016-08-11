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

#include "src/c/server.h"
#include <grpc_c/completion_queue.h>
#include <grpc_c/server.h>
#include "src/c/alloc.h"
#include "src/c/init_shutdown.h"

GRPC_server *GRPC_build_server(GRPC_build_server_options options) {
  GRPC_ensure_grpc_init();
  grpc_server *core_server = grpc_server_create(NULL, NULL);
  GRPC_server *server = GRPC_ALLOC_STRUCT(
      GRPC_server, {.core_server = core_server,
                    .internal_queue = grpc_completion_queue_create(NULL)});
  GRPC_array_init(server->registered_queues);
  return server;
}

GRPC_incoming_notification_queue *GRPC_server_new_incoming_queue(
    GRPC_server *server) {
  GRPC_incoming_notification_queue *queue =
      GRPC_incoming_notification_queue_create();
  grpc_server_register_completion_queue(server->core_server, queue->cq, NULL);
  // Stores the completion queue for destruction
  GRPC_array_push_back(server->registered_queues, queue);
  return queue;
}

void GRPC_server_start(GRPC_server *server) {
  grpc_server_start(server->core_server);
}

void GRPC_server_shutdown(GRPC_server *server) {
  grpc_server_shutdown_and_notify(server->core_server, server->internal_queue,
                                  NULL);
  // Wait for server to shutdown
  for (;;) {
    grpc_event ev = grpc_completion_queue_next(
        server->internal_queue, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    if (ev.type == GRPC_OP_COMPLETE) break;
  }
  // Shutdown all registered queues
  int i;
  for (i = 0; i < server->registered_queues.state.size; i++) {
    GRPC_completion_queue_shutdown(server->registered_queues.data[i]->cq);
  }
  // Wait for them to shutdown
  for (i = 0; i < server->registered_queues.state.size; i++) {
    GRPC_completion_queue_shutdown_wait(server->registered_queues.data[i]->cq);
  }
  for (;;) {
    grpc_event ev = grpc_completion_queue_next(
        server->internal_queue, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    if (ev.type == GRPC_QUEUE_SHUTDOWN) break;
  }
}

void GRPC_server_destroy(GRPC_server *server) {
  int i;
  for (i = 0; i < server->registered_queues.state.size; i++) {
    GRPC_incoming_notification_queue_destroy(server->registered_queues.data[i]);
  }
  GRPC_array_deinit(server->registered_queues);

  grpc_completion_queue_destroy(server->internal_queue);

  grpc_server_destroy(server->core_server);
}
