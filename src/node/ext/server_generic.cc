/*
 *
 * Copyright 2017, Google Inc.
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

#ifndef GRPC_UV

#include "server.h"

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"
#include "grpc/support/time.h"

namespace grpc {
namespace node {

Server::Server(grpc_server *server) : wrapped_server(server) {
  shutdown_queue = grpc_completion_queue_create(NULL);
  grpc_server_register_non_listening_completion_queue(server, shutdown_queue,
                                                      NULL);
}

Server::~Server() {
  this->ShutdownServer();
  grpc_completion_queue_shutdown(this->shutdown_queue);
  grpc_completion_queue_destroy(this->shutdown_queue);
}

void Server::ShutdownServer() {
  if (this->wrapped_server != NULL) {
    grpc_server_shutdown_and_notify(this->wrapped_server, this->shutdown_queue,
                                    NULL);
    grpc_server_cancel_all_calls(this->wrapped_server);
    grpc_completion_queue_pluck(this->shutdown_queue, NULL,
                                gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    grpc_server_destroy(this->wrapped_server);
    this->wrapped_server = NULL;
  }
}

}  // namespace grpc
}  // namespace node

#endif /* GRPC_UV */
