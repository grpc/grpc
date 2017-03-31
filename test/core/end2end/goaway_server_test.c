/*
 *
 * Copyright 2016, Google Inc.
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

/* With the addition of a libuv endpoint, sockaddr.h now includes uv.h when
   using that endpoint. Because of various transitive includes in uv.h,
   including windows.h on Windows, uv.h must be included before other system
   headers. Therefore, sockaddr.h must always be included first */
#include "src/core/lib/iomgr/sockaddr.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <string.h>
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static void *tag(intptr_t i) { return (void *)i; }

static gpr_mu g_mu;
static int g_resolve_port = -1;
static void (*iomgr_resolve_address)(grpc_exec_ctx *exec_ctx, const char *addr,
                                     const char *default_port,
                                     grpc_pollset_set *interested_parties,
                                     grpc_closure *on_done,
                                     grpc_resolved_addresses **addresses);

static void set_resolve_port(int port) {
  gpr_mu_lock(&g_mu);
  g_resolve_port = port;
  gpr_mu_unlock(&g_mu);
}

static void my_resolve_address(grpc_exec_ctx *exec_ctx, const char *addr,
                               const char *default_port,
                               grpc_pollset_set *interested_parties,
                               grpc_closure *on_done,
                               grpc_resolved_addresses **addrs) {
  if (0 != strcmp(addr, "test")) {
    iomgr_resolve_address(exec_ctx, addr, default_port, interested_parties,
                          on_done, addrs);
    return;
  }

  grpc_error *error = GRPC_ERROR_NONE;
  gpr_mu_lock(&g_mu);
  if (g_resolve_port < 0) {
    gpr_mu_unlock(&g_mu);
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Forced Failure");
  } else {
    *addrs = gpr_malloc(sizeof(**addrs));
    (*addrs)->naddrs = 1;
    (*addrs)->addrs = gpr_malloc(sizeof(*(*addrs)->addrs));
    memset((*addrs)->addrs, 0, sizeof(*(*addrs)->addrs));
    struct sockaddr_in *sa = (struct sockaddr_in *)(*addrs)->addrs[0].addr;
    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = htonl(0x7f000001);
    sa->sin_port = htons((uint16_t)g_resolve_port);
    (*addrs)->addrs[0].len = sizeof(*sa);
    gpr_mu_unlock(&g_mu);
  }
  grpc_closure_sched(exec_ctx, on_done, error);
}

int main(int argc, char **argv) {
  grpc_completion_queue *cq;
  cq_verifier *cqv;
  grpc_op ops[6];
  grpc_op *op;

  grpc_test_init(argc, argv);

  gpr_mu_init(&g_mu);
  grpc_init();
  iomgr_resolve_address = grpc_resolve_address;
  grpc_resolve_address = my_resolve_address;

  int was_cancelled1;
  int was_cancelled2;

  grpc_metadata_array trailing_metadata_recv1;
  grpc_metadata_array request_metadata1;
  grpc_call_details request_details1;
  grpc_status_code status1;
  grpc_slice details1;
  grpc_metadata_array_init(&trailing_metadata_recv1);
  grpc_metadata_array_init(&request_metadata1);
  grpc_call_details_init(&request_details1);

  grpc_metadata_array trailing_metadata_recv2;
  grpc_metadata_array request_metadata2;
  grpc_call_details request_details2;
  grpc_status_code status2;
  grpc_slice details2;
  grpc_metadata_array_init(&trailing_metadata_recv2);
  grpc_metadata_array_init(&request_metadata2);
  grpc_call_details_init(&request_details2);

  cq = grpc_completion_queue_create(NULL);
  cqv = cq_verifier_create(cq);

  /* reserve two ports */
  int port1 = grpc_pick_unused_port_or_die();
  int port2 = grpc_pick_unused_port_or_die();

  char *addr;

  grpc_channel_args client_args;
  grpc_arg arg_array[1];
  arg_array[0].type = GRPC_ARG_INTEGER;
  arg_array[0].key = "grpc.testing.fixed_reconnect_backoff_ms";
  arg_array[0].value.integer = 1000;
  client_args.args = arg_array;
  client_args.num_args = 1;

  /* create a channel that picks first amongst the servers */
  grpc_channel *chan = grpc_insecure_channel_create("test", &client_args, NULL);
  /* and an initial call to them */
  grpc_slice host = grpc_slice_from_static_string("127.0.0.1");
  grpc_call *call1 =
      grpc_channel_create_call(chan, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), &host,
                               grpc_timeout_seconds_to_deadline(20), NULL);
  /* send initial metadata to probe connectivity */
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call1, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x101), NULL));
  /* and receive status to probe termination */
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv1;
  op->data.recv_status_on_client.status = &status1;
  op->data.recv_status_on_client.status_details = &details1;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call1, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x102), NULL));

  /* bring a server up on the first port */
  grpc_server *server1 = grpc_server_create(NULL, NULL);
  gpr_asprintf(&addr, "127.0.0.1:%d", port1);
  grpc_server_add_insecure_http2_port(server1, addr);
  grpc_server_register_completion_queue(server1, cq, NULL);
  gpr_free(addr);
  grpc_server_start(server1);

  /* request a call to the server */
  grpc_call *server_call1;
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_server_request_call(server1, &server_call1, &request_details1,
                                      &request_metadata1, cq, cq, tag(0x301)));

  set_resolve_port(port1);

  /* first call should now start */
  CQ_EXPECT_COMPLETION(cqv, tag(0x101), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(0x301), 1);
  cq_verify(cqv);

  GPR_ASSERT(GRPC_CHANNEL_READY ==
             grpc_channel_check_connectivity_state(chan, 0));
  grpc_channel_watch_connectivity_state(chan, GRPC_CHANNEL_READY,
                                        gpr_inf_future(GPR_CLOCK_REALTIME), cq,
                                        tag(0x9999));

  /* listen for close on the server call to probe for finishing */
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled1;
  op->flags = 0;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(server_call1, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x302), NULL));

  /* shutdown first server:
   * we should see a connectivity change and then nothing */
  set_resolve_port(-1);
  grpc_server_shutdown_and_notify(server1, cq, tag(0xdead1));
  CQ_EXPECT_COMPLETION(cqv, tag(0x9999), 1);
  cq_verify(cqv);
  cq_verify_empty(cqv);

  /* and a new call: should go through to server2 when we start it */
  grpc_call *call2 =
      grpc_channel_create_call(chan, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), &host,
                               grpc_timeout_seconds_to_deadline(20), NULL);
  /* send initial metadata to probe connectivity */
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call2, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x201), NULL));
  /* and receive status to probe termination */
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv2;
  op->data.recv_status_on_client.status = &status2;
  op->data.recv_status_on_client.status_details = &details2;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call2, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x202), NULL));

  /* and bring up second server */
  set_resolve_port(port2);
  grpc_server *server2 = grpc_server_create(NULL, NULL);
  gpr_asprintf(&addr, "127.0.0.1:%d", port2);
  grpc_server_add_insecure_http2_port(server2, addr);
  grpc_server_register_completion_queue(server2, cq, NULL);
  gpr_free(addr);
  grpc_server_start(server2);

  /* request a call to the server */
  grpc_call *server_call2;
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_server_request_call(server2, &server_call2, &request_details2,
                                      &request_metadata2, cq, cq, tag(0x401)));

  /* second call should now start */
  CQ_EXPECT_COMPLETION(cqv, tag(0x201), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(0x401), 1);
  cq_verify(cqv);

  /* listen for close on the server call to probe for finishing */
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled2;
  op->flags = 0;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(server_call2, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x402), NULL));

  /* shutdown second server: we should see nothing */
  grpc_server_shutdown_and_notify(server2, cq, tag(0xdead2));
  cq_verify_empty(cqv);

  grpc_call_cancel(call1, NULL);
  grpc_call_cancel(call2, NULL);

  /* now everything else should finish */
  CQ_EXPECT_COMPLETION(cqv, tag(0x102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(0x202), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(0x302), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(0x402), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(0xdead1), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(0xdead2), 1);
  cq_verify(cqv);

  grpc_call_destroy(call1);
  grpc_call_destroy(call2);
  grpc_call_destroy(server_call1);
  grpc_call_destroy(server_call2);
  grpc_server_destroy(server1);
  grpc_server_destroy(server2);
  grpc_channel_destroy(chan);

  grpc_metadata_array_destroy(&trailing_metadata_recv1);
  grpc_metadata_array_destroy(&request_metadata1);
  grpc_call_details_destroy(&request_details1);
  grpc_slice_unref(details1);
  grpc_metadata_array_destroy(&trailing_metadata_recv2);
  grpc_metadata_array_destroy(&request_metadata2);
  grpc_call_details_destroy(&request_details2);
  grpc_slice_unref(details2);

  cq_verifier_destroy(cqv);
  grpc_completion_queue_destroy(cq);

  grpc_shutdown();
  gpr_mu_destroy(&g_mu);

  return 0;
}
