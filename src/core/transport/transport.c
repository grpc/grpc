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

#include "src/core/transport/transport.h"
#include "src/core/transport/transport_impl.h"

size_t grpc_transport_stream_size(grpc_transport *transport) {
  return transport->vtable->sizeof_stream;
}

void grpc_transport_goaway(grpc_transport *transport, grpc_status_code status,
                           gpr_slice message) {
  transport->vtable->goaway(transport, status, message);
}

void grpc_transport_close(grpc_transport *transport) {
  transport->vtable->close(transport);
}

void grpc_transport_destroy(grpc_transport *transport) {
  transport->vtable->destroy(transport);
}

int grpc_transport_init_stream(grpc_transport *transport, grpc_stream *stream,
                               const void *server_data,
                               grpc_transport_op *initial_op) {
  return transport->vtable->init_stream(transport, stream, server_data,
                                        initial_op);
}

void grpc_transport_perform_op(grpc_transport *transport, grpc_stream *stream,
                               grpc_transport_op *op) {
  transport->vtable->perform_op(transport, stream, op);
}

void grpc_transport_add_to_pollset(grpc_transport *transport,
                                   grpc_pollset *pollset) {
  transport->vtable->add_to_pollset(transport, pollset);
}

void grpc_transport_destroy_stream(grpc_transport *transport,
                                   grpc_stream *stream) {
  transport->vtable->destroy_stream(transport, stream);
}

void grpc_transport_ping(grpc_transport *transport, void (*cb)(void *user_data),
                         void *user_data) {
  transport->vtable->ping(transport, cb, user_data);
}

void grpc_transport_setup_cancel(grpc_transport_setup *setup) {
  setup->vtable->cancel(setup);
}

void grpc_transport_setup_initiate(grpc_transport_setup *setup) {
  setup->vtable->initiate(setup);
}

void grpc_transport_op_finish_with_failure(grpc_transport_op *op) {
  if (op->send_ops) {
    op->on_done_send(op->send_user_data, 0);
  }
  if (op->recv_ops) {
    op->on_done_recv(op->recv_user_data, 0);
  }
}

void grpc_transport_op_add_cancellation(grpc_transport_op *op,
                                        grpc_status_code status,
                                        grpc_mdstr *message) {
  if (op->cancel_with_status == GRPC_STATUS_OK) {
    op->cancel_with_status = status;
    op->cancel_message = message;
  } else if (message) {
    grpc_mdstr_unref(message);
  }
}
