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
#include "src/core/transport/chttp2_transport.h"

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

static void done_write(void *arg, grpc_endpoint_cb_status status) {
  thd_args *a = arg;
  gpr_event_set(&a->done_write, (void *)1);
}

static grpc_transport_setup_result server_setup_transport(
    void *ts, grpc_transport *transport, grpc_mdctx *mdctx) {
  thd_args *a = ts;
  static grpc_channel_filter const *extra_filters[] = {
      &grpc_http_server_filter};
  return grpc_server_setup_transport(a->server, transport, extra_filters,
                                     GPR_ARRAY_SIZE(extra_filters), mdctx);
}

void grpc_run_bad_client_test(const char *name, const char *client_payload,
                              size_t client_payload_length,
                              grpc_bad_client_server_side_validator validator) {
  grpc_endpoint_pair sfd;
  thd_args a;
  gpr_thd_id id;
  gpr_slice slice =
      gpr_slice_from_copied_buffer(client_payload, client_payload_length);

  /* Add a debug log */
  gpr_log(GPR_INFO, "TEST: %s", name);

  /* Init grpc */
  grpc_init();

  /* Create endpoints */
  sfd = grpc_iomgr_create_endpoint_pair(65536);

  /* Create server, completion events */
  a.server = grpc_server_create_from_filters(NULL, 0, NULL);
  a.cq = grpc_completion_queue_create();
  gpr_event_init(&a.done_thd);
  gpr_event_init(&a.done_write);
  a.validator = validator;
  grpc_server_register_completion_queue(a.server, a.cq);
  grpc_server_start(a.server);
  grpc_create_chttp2_transport(server_setup_transport, &a, NULL, sfd.server,
                               NULL, 0, grpc_mdctx_create(), 0);

  /* Bind everything into the same pollset */
  grpc_endpoint_add_to_pollset(sfd.client, grpc_cq_pollset(a.cq));
  grpc_endpoint_add_to_pollset(sfd.server, grpc_cq_pollset(a.cq));

  /* Check a ground truth */
  GPR_ASSERT(grpc_server_has_open_connections(a.server));

  /* Start validator */
  gpr_thd_new(&id, thd_func, &a, NULL);

  /* Write data */
  switch (grpc_endpoint_write(sfd.client, &slice, 1, done_write, &a)) {
    case GRPC_ENDPOINT_WRITE_DONE:
      done_write(&a, 1);
      break;
    case GRPC_ENDPOINT_WRITE_PENDING:
      break;
    case GRPC_ENDPOINT_WRITE_ERROR:
      done_write(&a, 0);
      break;
  }

  /* Await completion */
  GPR_ASSERT(
      gpr_event_wait(&a.done_write, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5)));
  GPR_ASSERT(gpr_event_wait(&a.done_thd, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5)));

  /* Shutdown */
  grpc_endpoint_destroy(sfd.client);
  grpc_server_destroy(a.server);
  grpc_completion_queue_destroy(a.cq);

  grpc_shutdown();
}
