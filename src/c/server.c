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
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc_c/codegen/server.h>
#include <grpc_c/completion_queue.h>
#include "src/c/alloc.h"
#include "src/c/init_shutdown.h"
#include "src/c/server_context.h"

void GRPC_server_listen_host(GRPC_server *server, const char *host) {
  grpc_server_add_insecure_http2_port(server->core_server, host);
}

GRPC_server *GRPC_build_server(GRPC_build_server_options options) {
  GRPC_ensure_grpc_init();
  grpc_server *core_server = grpc_server_create(NULL, NULL);
  GRPC_server *server = GRPC_ALLOC_STRUCT(
      GRPC_server, {.core_server = core_server,
                    .event_queue = grpc_completion_queue_create(NULL)});
  GRPC_array_init(server->registered_queues);
  GRPC_array_init(server->registered_services);
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
  grpc_server_shutdown_and_notify(server->core_server, server->event_queue,
                                  NULL);
  // Wait for server to shutdown
  for (;;) {
    grpc_event ev = grpc_completion_queue_next(
        server->event_queue, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    if (ev.type == GRPC_OP_COMPLETE) break;
  }
  // Shutdown internal server queue
  grpc_completion_queue_shutdown(server->event_queue);
  // Shutdown all registered queues
  size_t i;
  for (i = 0; i < server->registered_queues.state.size; i++) {
    GRPC_completion_queue_shutdown(server->registered_queues.data[i]->cq);
  }
  // Wait for them to shutdown
  for (i = 0; i < server->registered_queues.state.size; i++) {
    GRPC_completion_queue_shutdown_wait(server->registered_queues.data[i]->cq);
  }
  for (;;) {
    grpc_event ev = grpc_completion_queue_next(
        server->event_queue, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    if (ev.type == GRPC_QUEUE_SHUTDOWN) break;
  }
}

void GRPC_server_destroy(GRPC_server *server) {
  size_t i;
  // release queues
  for (i = 0; i < server->registered_queues.state.size; i++) {
    GRPC_incoming_notification_queue_destroy(server->registered_queues.data[i]);
  }
  GRPC_array_deinit(server->registered_queues);

  // release registered methods
  for (i = 0; i < server->registered_services.state.size; i++) {
    GRPC_array_deinit(server->registered_services.data[i].registered_methods);
  }
  GRPC_array_deinit(server->registered_services);

  grpc_completion_queue_destroy(server->event_queue);

  grpc_server_destroy(server->core_server);
  gpr_free(server);
}

GRPC_registered_service *GRPC_server_add_service(
    GRPC_server *server, GRPC_service_declaration service_declaration,
    size_t num_methods) {
  // register every method in the service
  size_t i;
  GRPC_registered_service registered_service;
  GRPC_array_init(registered_service.registered_methods);
  for (i = 0; i < num_methods; i++) {
    GRPC_registered_method registered_method;
    registered_method.method = *service_declaration[i];
    grpc_server_register_method_payload_handling handling;
    // Let core read the payload for us only in unary or server streaming case
    if (registered_method.method.type == GRPC_NORMAL_RPC ||
        registered_method.method.type == GRPC_SERVER_STREAMING) {
      handling = GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER;
    } else {
      handling = GRPC_SRM_PAYLOAD_NONE;
    }
    // TODO(yifeit): per-method host
    registered_method.core_method_handle = grpc_server_register_method(
        server->core_server, registered_method.method.name, NULL, handling, 0);
    GRPC_array_push_back(registered_service.registered_methods,
                         registered_method);
  }
  GRPC_array_push_back(server->registered_services, registered_service);
  return &server->registered_services
              .data[server->registered_services.state.size - 1];
}

grpc_call_error GRPC_server_request_call(
    GRPC_registered_service *service, size_t method_index,
    GRPC_server_context *context,
    GRPC_incoming_notification_queue *incoming_queue,
    GRPC_completion_queue *processing_queue, void *tag) {
  void *core_method_handle =
      service->registered_methods.data[method_index].core_method_handle;
  GPR_ASSERT(core_method_handle != NULL);
  return grpc_server_request_registered_call(
      context->server->core_server, core_method_handle, &context->call,
      &context->deadline, &context->recv_metadata_array, &context->payload,
      processing_queue, incoming_queue->cq, tag);
}
