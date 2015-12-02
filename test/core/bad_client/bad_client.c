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

#include "test/core/bad_client/bad_client.h"

#include "src/core/channel/channel_stack.h"
#include "src/core/channel/http_server_filter.h"
#include "src/core/iomgr/endpoint_pair.h"
#include "src/core/surface/completion_queue.h"
#include "src/core/surface/server.h"
#include "src/core/support/string.h"
#include "src/core/transport/chttp2_transport.h"

#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>

typedef struct {
  grpc_server *server;
  grpc_completion_queue *cq;
  grpc_bad_client_server_side_validator validator;
  gpr_event done_thd;
  gpr_event done_write;
} thd_args;

static void thd_func(void *arg) {
  thd_args *a = arg;
  a->validator(a->server, a->cq);
  gpr_event_set(&a->done_thd, (void *)1);
}

static void done_write(grpc_exec_ctx *exec_ctx, void *arg, int success) {
  thd_args *a = arg;
  gpr_event_set(&a->done_write, (void *)1);
}

static void server_setup_transport(void *ts, grpc_transport *transport) {
  thd_args *a = ts;
  static grpc_channel_filter const *extra_filters[] = {
      &grpc_http_server_filter};
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_server_setup_transport(&exec_ctx, a->server, transport, extra_filters,
                              GPR_ARRAY_SIZE(extra_filters),
                              grpc_server_get_channel_args(a->server));
  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_run_bad_client_test(grpc_bad_client_server_side_validator validator,
                              const char *client_payload,
                              size_t client_payload_length, gpr_uint32 flags) {
  grpc_endpoint_pair sfd;
  thd_args a;
  gpr_thd_id id;
  char *hex;
  grpc_transport *transport;
  gpr_slice slice =
      gpr_slice_from_copied_buffer(client_payload, client_payload_length);
  gpr_slice_buffer outgoing;
  grpc_closure done_write_closure;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  hex = gpr_dump(client_payload, client_payload_length,
                 GPR_DUMP_HEX | GPR_DUMP_ASCII);

  /* Add a debug log */
  gpr_log(GPR_INFO, "TEST: %s", hex);

  gpr_free(hex);

  /* Init grpc */
  grpc_init();

  /* Create endpoints */
  sfd = grpc_iomgr_create_endpoint_pair("fixture", 65536);

  /* Create server, completion events */
  a.server = grpc_server_create_from_filters(NULL, 0, NULL);
  a.cq = grpc_completion_queue_create(NULL);
  gpr_event_init(&a.done_thd);
  gpr_event_init(&a.done_write);
  a.validator = validator;
  grpc_server_register_completion_queue(a.server, a.cq, NULL);
  grpc_server_start(a.server);
  transport = grpc_create_chttp2_transport(&exec_ctx, NULL, sfd.server, 0);
  server_setup_transport(&a, transport);
  grpc_chttp2_transport_start_reading(&exec_ctx, transport, NULL, 0);
  grpc_exec_ctx_finish(&exec_ctx);

  /* Bind everything into the same pollset */
  grpc_endpoint_add_to_pollset(&exec_ctx, sfd.client, grpc_cq_pollset(a.cq));
  grpc_endpoint_add_to_pollset(&exec_ctx, sfd.server, grpc_cq_pollset(a.cq));

  /* Check a ground truth */
  GPR_ASSERT(grpc_server_has_open_connections(a.server));

  /* Start validator */
  gpr_thd_new(&id, thd_func, &a, NULL);

  gpr_slice_buffer_init(&outgoing);
  gpr_slice_buffer_add(&outgoing, slice);
  grpc_closure_init(&done_write_closure, done_write, &a);

  /* Write data */
  grpc_endpoint_write(&exec_ctx, sfd.client, &outgoing, &done_write_closure);
  grpc_exec_ctx_finish(&exec_ctx);

  /* Await completion */
  GPR_ASSERT(
      gpr_event_wait(&a.done_write, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5)));

  if (flags & GRPC_BAD_CLIENT_DISCONNECT) {
    grpc_endpoint_shutdown(&exec_ctx, sfd.client);
    grpc_endpoint_destroy(&exec_ctx, sfd.client);
    grpc_exec_ctx_finish(&exec_ctx);
    sfd.client = NULL;
  }

  GPR_ASSERT(gpr_event_wait(&a.done_thd, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5)));

  /* Shutdown */
  if (sfd.client) {
    grpc_endpoint_shutdown(&exec_ctx, sfd.client);
    grpc_endpoint_destroy(&exec_ctx, sfd.client);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_server_shutdown_and_notify(a.server, a.cq, NULL);
  GPR_ASSERT(grpc_completion_queue_pluck(a.cq, NULL,
                                         GRPC_TIMEOUT_SECONDS_TO_DEADLINE(1),
                                         NULL).type == GRPC_OP_COMPLETE);
  grpc_server_destroy(a.server);
  grpc_completion_queue_destroy(a.cq);
  gpr_slice_buffer_destroy(&outgoing);

  grpc_exec_ctx_finish(&exec_ctx);
  grpc_shutdown();
}
