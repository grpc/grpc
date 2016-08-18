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

#ifndef GRPC_C_INTERNAL_SERVER_H
#define GRPC_C_INTERNAL_SERVER_H

#include <grpc/grpc.h>
#include <grpc_c/codegen/method.h>
#include <grpc_c/grpc_c.h>
#include <grpc_c/server.h>
#include "src/c/array.h"
#include "src/c/server_incoming_queue.h"

typedef struct GRPC_registered_method GRPC_registered_method;

struct GRPC_registered_method {
  GRPC_method method;
  // An opaque structure used by core to identify this method
  void *core_method_handle;
};

struct GRPC_registered_service {
  GRPC_server *server;
  // Index of myself in the server-side service array
  size_t index;

  GRPC_array(GRPC_registered_method) registered_methods;
};

struct GRPC_server {
  grpc_server *core_server;
  GRPC_array(char *) listen_hosts;
  GRPC_array(GRPC_incoming_notification_queue *) registered_queues;
  GRPC_array(GRPC_registered_service) registered_services;

  // used to monitor server events
  grpc_completion_queue *event_queue;

  // async

  // TODO(yifeit): synchronous server state
};

grpc_call_error GRPC_server_request_call(
    GRPC_registered_service *service, size_t method_index,
    GRPC_server_context *context,
    GRPC_incoming_notification_queue *incoming_queue,
    GRPC_completion_queue *processing_queue, void *tag);

#endif  // GRPC_C_INTERNAL_SERVER_H
