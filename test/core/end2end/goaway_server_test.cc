/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* With the addition of a libuv endpoint, sockaddr.h now includes uv.h when
   using that endpoint. Because of various transitive includes in uv.h,
   including windows.h on Windows, uv.h must be included before other system
   headers. Therefore, sockaddr.h must always be included first */
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <string.h>
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

extern grpc_address_resolver_vtable* grpc_resolve_address_impl;
static grpc_address_resolver_vtable* default_resolver;

static void* tag(intptr_t i) { return (void*)i; }

static gpr_mu g_mu;
static int g_resolve_port = -1;

static grpc_ares_request* (*iomgr_dns_lookup_ares)(
    const char* dns_server, const char* addr, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_lb_addresses** addresses, bool check_grpclb,
    char** service_config_json);

static void set_resolve_port(int port) {
  gpr_mu_lock(&g_mu);
  g_resolve_port = port;
  gpr_mu_unlock(&g_mu);
}

static void my_resolve_address(const char* addr, const char* default_port,
                               grpc_pollset_set* interested_parties,
                               grpc_closure* on_done,
                               grpc_resolved_addresses** addrs) {
  if (0 != strcmp(addr, "test")) {
    default_resolver->resolve_address(addr, default_port, interested_parties,
                                      on_done, addrs);
    return;
  }

  grpc_error* error = GRPC_ERROR_NONE;
  gpr_mu_lock(&g_mu);
  if (g_resolve_port < 0) {
    gpr_mu_unlock(&g_mu);
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Forced Failure");
  } else {
    *addrs = static_cast<grpc_resolved_addresses*>(gpr_malloc(sizeof(**addrs)));
    (*addrs)->naddrs = 1;
    (*addrs)->addrs = static_cast<grpc_resolved_address*>(
        gpr_malloc(sizeof(*(*addrs)->addrs)));
    memset((*addrs)->addrs, 0, sizeof(*(*addrs)->addrs));
    grpc_sockaddr_in* sa =
        reinterpret_cast<grpc_sockaddr_in*>((*addrs)->addrs[0].addr);
    sa->sin_family = GRPC_AF_INET;
    sa->sin_addr.s_addr = 0x100007f;
    sa->sin_port = grpc_htons(static_cast<uint16_t>(g_resolve_port));
    (*addrs)->addrs[0].len = static_cast<socklen_t>(sizeof(*sa));
    gpr_mu_unlock(&g_mu);
  }
  GRPC_CLOSURE_SCHED(on_done, error);
}

static grpc_error* my_blocking_resolve_address(
    const char* name, const char* default_port,
    grpc_resolved_addresses** addresses) {
  return default_resolver->blocking_resolve_address(name, default_port,
                                                    addresses);
}

static grpc_address_resolver_vtable test_resolver = {
    my_resolve_address, my_blocking_resolve_address};

static grpc_ares_request* my_dns_lookup_ares(
    const char* dns_server, const char* addr, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_lb_addresses** lb_addrs, bool check_grpclb,
    char** service_config_json) {
  if (0 != strcmp(addr, "test")) {
    return iomgr_dns_lookup_ares(dns_server, addr, default_port,
                                 interested_parties, on_done, lb_addrs,
                                 check_grpclb, service_config_json);
  }

  grpc_error* error = GRPC_ERROR_NONE;
  gpr_mu_lock(&g_mu);
  if (g_resolve_port < 0) {
    gpr_mu_unlock(&g_mu);
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Forced Failure");
  } else {
    *lb_addrs = grpc_lb_addresses_create(1, nullptr);
    grpc_sockaddr_in* sa =
        static_cast<grpc_sockaddr_in*>(gpr_zalloc(sizeof(grpc_sockaddr_in)));
    sa->sin_family = GRPC_AF_INET;
    sa->sin_addr.s_addr = 0x100007f;
    sa->sin_port = grpc_htons(static_cast<uint16_t>(g_resolve_port));
    grpc_lb_addresses_set_address(*lb_addrs, 0, sa, sizeof(*sa), false, nullptr,
                                  nullptr);
    gpr_free(sa);
    gpr_mu_unlock(&g_mu);
  }
  GRPC_CLOSURE_SCHED(on_done, error);
  return nullptr;
}

int main(int argc, char** argv) {
  grpc_completion_queue* cq;
  cq_verifier* cqv;
  grpc_op ops[6];
  grpc_op* op;

  grpc_test_init(argc, argv);

  gpr_mu_init(&g_mu);
  grpc_init();
  default_resolver = grpc_resolve_address_impl;
  grpc_set_resolver_impl(&test_resolver);
  iomgr_dns_lookup_ares = grpc_dns_lookup_ares;
  grpc_dns_lookup_ares = my_dns_lookup_ares;

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

  cq = grpc_completion_queue_create_for_next(nullptr);
  cqv = cq_verifier_create(cq);

  /* reserve two ports */
  int port1 = grpc_pick_unused_port_or_die();
  int port2 = grpc_pick_unused_port_or_die();

  char* addr;

  grpc_channel_args client_args;
  grpc_arg arg_array[1];
  arg_array[0].type = GRPC_ARG_INTEGER;
  arg_array[0].key =
      const_cast<char*>("grpc.testing.fixed_reconnect_backoff_ms");
  arg_array[0].value.integer = 1000;
  client_args.args = arg_array;
  client_args.num_args = 1;

  /* create a channel that picks first amongst the servers */
  grpc_channel* chan =
      grpc_insecure_channel_create("test", &client_args, nullptr);
  /* and an initial call to them */
  grpc_slice host = grpc_slice_from_static_string("127.0.0.1");
  grpc_call* call1 =
      grpc_channel_create_call(chan, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), &host,
                               grpc_timeout_seconds_to_deadline(20), nullptr);
  /* send initial metadata to probe connectivity */
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call1, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x101), nullptr));
  /* and receive status to probe termination */
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv1;
  op->data.recv_status_on_client.status = &status1;
  op->data.recv_status_on_client.status_details = &details1;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call1, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x102), nullptr));

  /* bring a server up on the first port */
  grpc_server* server1 = grpc_server_create(nullptr, nullptr);
  gpr_asprintf(&addr, "127.0.0.1:%d", port1);
  grpc_server_add_insecure_http2_port(server1, addr);
  grpc_server_register_completion_queue(server1, cq, nullptr);
  gpr_free(addr);
  grpc_server_start(server1);

  /* request a call to the server */
  grpc_call* server_call1;
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
                                                   tag(0x302), nullptr));

  /* shutdown first server:
   * we should see a connectivity change and then nothing */
  set_resolve_port(-1);
  grpc_server_shutdown_and_notify(server1, cq, tag(0xdead1));
  CQ_EXPECT_COMPLETION(cqv, tag(0x9999), 1);
  cq_verify(cqv);
  cq_verify_empty(cqv);

  /* and a new call: should go through to server2 when we start it */
  grpc_call* call2 =
      grpc_channel_create_call(chan, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), &host,
                               grpc_timeout_seconds_to_deadline(20), nullptr);
  /* send initial metadata to probe connectivity */
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call2, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x201), nullptr));
  /* and receive status to probe termination */
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv2;
  op->data.recv_status_on_client.status = &status2;
  op->data.recv_status_on_client.status_details = &details2;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call2, ops,
                                                   (size_t)(op - ops),
                                                   tag(0x202), nullptr));

  /* and bring up second server */
  set_resolve_port(port2);
  grpc_server* server2 = grpc_server_create(nullptr, nullptr);
  gpr_asprintf(&addr, "127.0.0.1:%d", port2);
  grpc_server_add_insecure_http2_port(server2, addr);
  grpc_server_register_completion_queue(server2, cq, nullptr);
  gpr_free(addr);
  grpc_server_start(server2);

  /* request a call to the server */
  grpc_call* server_call2;
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
                                                   tag(0x402), nullptr));

  /* shutdown second server: we should see nothing */
  grpc_server_shutdown_and_notify(server2, cq, tag(0xdead2));
  cq_verify_empty(cqv);

  grpc_call_cancel(call1, nullptr);
  grpc_call_cancel(call2, nullptr);

  /* now everything else should finish */
  CQ_EXPECT_COMPLETION(cqv, tag(0x102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(0x202), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(0x302), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(0x402), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(0xdead1), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(0xdead2), 1);
  cq_verify(cqv);

  grpc_call_unref(call1);
  grpc_call_unref(call2);
  grpc_call_unref(server_call1);
  grpc_call_unref(server_call2);
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
