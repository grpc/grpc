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

#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/impl/codegen/byte_buffer_reader.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

#include <grpc++/impl/codegen/config.h>
extern "C" {
#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/support/tmpfile.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/fake_resolver.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
}

#include "src/proto/grpc/lb/v1/load_balancer.pb.h"

#define NUM_BACKENDS 4
#define PAYLOAD "hello you"

// TODO(dgq): Other scenarios in need of testing:
// - Send an empty serverlist update and verify that the client request blocks
//   until a new serverlist with actual contents is available.
// - Send identical serverlist update
// - Send a serverlist with faulty ip:port addresses (port > 2^16, etc).
// - Test reception of invalid serverlist
// - Test pinging
// - Test against a non-LB server.
// - Random LB server closing the stream unexpectedly.
// - Test using DNS-resolvable names (localhost?)
// - Test handling of creation of faulty RR instance by having the LB return a
//   serverlist with non-existent backends after having initially returned a
//   valid one.
//
// Findings from end to end testing to be covered here:
// - Handling of LB servers restart, including reconnection after backing-off
//   retries.
// - Destruction of load balanced channel (and therefore of grpclb instance)
//   while:
//   1) the internal LB call is still active. This should work by virtue
//   of the weak reference the LB call holds. The call should be terminated as
//   part of the grpclb shutdown process.
//   2) the retry timer is active. Again, the weak reference it holds should
//   prevent a premature call to \a glb_destroy.
// - Restart of backend servers with no changes to serverlist. This exercises
//   the RR handover mechanism.

namespace grpc {
namespace {

typedef struct client_fixture {
  grpc_channel *client;
  char *server_uri;
  grpc_completion_queue *cq;
} client_fixture;

typedef struct server_fixture {
  grpc_server *server;
  grpc_call *server_call;
  grpc_completion_queue *cq;
  char *servers_hostport;
  const char *balancer_name;
  int port;
  const char *lb_token_prefix;
  gpr_thd_id tid;
  int num_calls_serviced;
} server_fixture;

typedef struct test_fixture {
  server_fixture lb_server;
  server_fixture lb_backends[NUM_BACKENDS];
  client_fixture client;
  int lb_server_update_delay_ms;
} test_fixture;

static void *tag(intptr_t t) { return (void *)t; }

static grpc_slice build_response_payload_slice(
    const char *host, int *ports, size_t nports,
    int64_t expiration_interval_secs, int32_t expiration_interval_nanos,
    const char *token_prefix) {
  // server_list {
  //   servers {
  //     ip_address: <in_addr/6 bytes of an IP>
  //     port: <16 bit uint>
  //     load_balance_token: "token..."
  //   }
  //   ...
  // }
  grpc::lb::v1::LoadBalanceResponse response;
  auto *serverlist = response.mutable_server_list();

  if (expiration_interval_secs > 0 || expiration_interval_nanos > 0) {
    auto *expiration_interval = serverlist->mutable_expiration_interval();
    if (expiration_interval_secs > 0) {
      expiration_interval->set_seconds(expiration_interval_secs);
    }
    if (expiration_interval_nanos > 0) {
      expiration_interval->set_nanos(expiration_interval_nanos);
    }
  }
  for (size_t i = 0; i < nports; i++) {
    auto *server = serverlist->add_servers();
    // TODO(dgq): test ipv6
    struct in_addr ip4;
    GPR_ASSERT(inet_pton(AF_INET, host, &ip4) == 1);
    server->set_ip_address(
        string(reinterpret_cast<const char *>(&ip4), sizeof(ip4)));
    server->set_port(ports[i]);
    // Missing tokens are acceptable. Test that path.
    if (strlen(token_prefix) > 0) {
      string token_data = token_prefix + std::to_string(ports[i]);
      server->set_load_balance_token(token_data);
    }
  }
  const string &enc_resp = response.SerializeAsString();
  return grpc_slice_from_copied_buffer(enc_resp.data(), enc_resp.size());
}

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, grpc_timeout_seconds_to_deadline(5),
                                    NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void sleep_ms(int delay_ms) {
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_millis(delay_ms, GPR_TIMESPAN)));
}

static void start_lb_server(server_fixture *sf, int *ports, size_t nports,
                            int update_delay_ms) {
  grpc_call *s;
  cq_verifier *cqv = cq_verifier_create(sf->cq);
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_call_error error;
  int was_cancelled = 2;
  grpc_byte_buffer *request_payload_recv;
  grpc_byte_buffer *response_payload;

  memset(ops, 0, sizeof(ops));
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  error = grpc_server_request_call(sf->server, &s, &call_details,
                                   &request_metadata_recv, sf->cq, sf->cq,
                                   tag(200));
  GPR_ASSERT(GRPC_CALL_OK == error);
  gpr_log(GPR_INFO, "LB Server[%s](%s) up", sf->servers_hostport,
          sf->balancer_name);
  CQ_EXPECT_COMPLETION(cqv, tag(200), 1);
  cq_verify(cqv);
  gpr_log(GPR_INFO, "LB Server[%s](%s) after tag 200", sf->servers_hostport,
          sf->balancer_name);

  // make sure we've received the initial metadata from the grpclb request.
  GPR_ASSERT(request_metadata_recv.count > 0);
  GPR_ASSERT(request_metadata_recv.metadata != NULL);

  // receive request for backends
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(202), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(202), 1);
  cq_verify(cqv);
  gpr_log(GPR_INFO, "LB Server[%s](%s) after RECV_MSG", sf->servers_hostport,
          sf->balancer_name);

  // validate initial request.
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, request_payload_recv);
  grpc_slice request_payload_slice = grpc_byte_buffer_reader_readall(&bbr);
  grpc::lb::v1::LoadBalanceRequest request;
  request.ParseFromArray(GRPC_SLICE_START_PTR(request_payload_slice),
                         GRPC_SLICE_LENGTH(request_payload_slice));
  GPR_ASSERT(request.has_initial_request());
  GPR_ASSERT(request.initial_request().name() == sf->servers_hostport);
  grpc_slice_unref(request_payload_slice);
  grpc_byte_buffer_reader_destroy(&bbr);
  grpc_byte_buffer_destroy(request_payload_recv);

  grpc_slice response_payload_slice;
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(201), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  gpr_log(GPR_INFO, "LB Server[%s](%s) after tag 201", sf->servers_hostport,
          sf->balancer_name);

  for (int i = 0; i < 2; i++) {
    if (i == 0) {
      // First half of the ports.
      response_payload_slice = build_response_payload_slice(
          "127.0.0.1", ports, nports / 2, -1, -1, sf->lb_token_prefix);
    } else {
      // Second half of the ports.
      sleep_ms(update_delay_ms);
      response_payload_slice = build_response_payload_slice(
          "127.0.0.1", ports + (nports / 2), (nports + 1) / 2 /* ceil */, -1,
          -1, "" /* this half doesn't get to receive an LB token */);
    }

    response_payload = grpc_raw_byte_buffer_create(&response_payload_slice, 1);
    op = ops;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = response_payload;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(203), NULL);
    GPR_ASSERT(GRPC_CALL_OK == error);
    CQ_EXPECT_COMPLETION(cqv, tag(203), 1);
    cq_verify(cqv);
    gpr_log(GPR_INFO, "LB Server[%s](%s) after SEND_MESSAGE, iter %d",
            sf->servers_hostport, sf->balancer_name, i);

    grpc_byte_buffer_destroy(response_payload);
    grpc_slice_unref(response_payload_slice);
  }
  gpr_log(GPR_INFO, "LB Server[%s](%s) shutting down", sf->servers_hostport,
          sf->balancer_name);

  op = ops;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(204), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(201), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(204), 1);
  cq_verify(cqv);
  gpr_log(GPR_INFO, "LB Server[%s](%s) after tag 204. All done. LB server out",
          sf->servers_hostport, sf->balancer_name);

  grpc_call_destroy(s);

  cq_verifier_destroy(cqv);

  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
}

static void start_backend_server(server_fixture *sf) {
  grpc_call *s;
  cq_verifier *cqv;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_call_error error;
  int was_cancelled;
  grpc_byte_buffer *request_payload_recv;
  grpc_byte_buffer *response_payload;
  grpc_event ev;

  while (true) {
    memset(ops, 0, sizeof(ops));
    cqv = cq_verifier_create(sf->cq);
    was_cancelled = 2;
    grpc_metadata_array_init(&request_metadata_recv);
    grpc_call_details_init(&call_details);

    error = grpc_server_request_call(sf->server, &s, &call_details,
                                     &request_metadata_recv, sf->cq, sf->cq,
                                     tag(100));
    GPR_ASSERT(GRPC_CALL_OK == error);
    gpr_log(GPR_INFO, "Server[%s] up", sf->servers_hostport);
    ev = grpc_completion_queue_next(sf->cq,
                                    grpc_timeout_seconds_to_deadline(60), NULL);
    if (!ev.success) {
      gpr_log(GPR_INFO, "Server[%s] being torn down", sf->servers_hostport);
      cq_verifier_destroy(cqv);
      grpc_metadata_array_destroy(&request_metadata_recv);
      grpc_call_details_destroy(&call_details);
      return;
    }
    GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    const string expected_token =
        strlen(sf->lb_token_prefix) == 0 ? "" : sf->lb_token_prefix +
                                                    std::to_string(sf->port);
    GPR_ASSERT(contains_metadata(&request_metadata_recv, "lb-token",
                                 expected_token.c_str()));

    gpr_log(GPR_INFO, "Server[%s] after tag 100", sf->servers_hostport);

    op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
    op->data.recv_close_on_server.cancelled = &was_cancelled;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(101), NULL);
    GPR_ASSERT(GRPC_CALL_OK == error);
    gpr_log(GPR_INFO, "Server[%s] after tag 101", sf->servers_hostport);

    bool exit = false;
    grpc_slice response_payload_slice = grpc_slice_from_copied_string(PAYLOAD);
    while (!exit) {
      op = ops;
      op->op = GRPC_OP_RECV_MESSAGE;
      op->data.recv_message.recv_message = &request_payload_recv;
      op->flags = 0;
      op->reserved = NULL;
      op++;
      error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
      GPR_ASSERT(GRPC_CALL_OK == error);
      ev = grpc_completion_queue_next(
          sf->cq, grpc_timeout_seconds_to_deadline(3), NULL);
      if (ev.type == GRPC_OP_COMPLETE && ev.success) {
        GPR_ASSERT(ev.tag = tag(102));
        if (request_payload_recv == NULL) {
          exit = true;
          gpr_log(GPR_INFO,
                  "Server[%s] recv \"close\" from client, exiting. Call #%d",
                  sf->servers_hostport, sf->num_calls_serviced);
        }
      } else {
        gpr_log(GPR_INFO, "Server[%s] forced to shutdown. Call #%d",
                sf->servers_hostport, sf->num_calls_serviced);
        exit = true;
      }
      gpr_log(GPR_INFO, "Server[%s] after tag 102. Call #%d",
              sf->servers_hostport, sf->num_calls_serviced);

      if (!exit) {
        response_payload =
            grpc_raw_byte_buffer_create(&response_payload_slice, 1);
        op = ops;
        op->op = GRPC_OP_SEND_MESSAGE;
        op->data.send_message.send_message = response_payload;
        op->flags = 0;
        op->reserved = NULL;
        op++;
        error =
            grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(103), NULL);
        GPR_ASSERT(GRPC_CALL_OK == error);
        ev = grpc_completion_queue_next(
            sf->cq, grpc_timeout_seconds_to_deadline(3), NULL);
        if (ev.type == GRPC_OP_COMPLETE && ev.success) {
          GPR_ASSERT(ev.tag = tag(103));
        } else {
          gpr_log(GPR_INFO, "Server[%s] forced to shutdown. Call #%d",
                  sf->servers_hostport, sf->num_calls_serviced);
          exit = true;
        }
        gpr_log(GPR_INFO, "Server[%s] after tag 103. Call #%d",
                sf->servers_hostport, sf->num_calls_serviced);
        grpc_byte_buffer_destroy(response_payload);
      }

      grpc_byte_buffer_destroy(request_payload_recv);
    }
    ++sf->num_calls_serviced;

    gpr_log(GPR_INFO, "Server[%s] OUT OF THE LOOP", sf->servers_hostport);
    grpc_slice_unref(response_payload_slice);

    op = ops;
    op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
    op->data.send_status_from_server.trailing_metadata_count = 0;
    op->data.send_status_from_server.status = GRPC_STATUS_OK;
    grpc_slice status_details =
        grpc_slice_from_static_string("Backend server out a-ok");
    op->data.send_status_from_server.status_details = &status_details;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(104), NULL);
    GPR_ASSERT(GRPC_CALL_OK == error);

    CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
    CQ_EXPECT_COMPLETION(cqv, tag(104), 1);
    cq_verify(cqv);
    gpr_log(GPR_INFO, "Server[%s] DONE. After servicing %d calls",
            sf->servers_hostport, sf->num_calls_serviced);

    grpc_call_destroy(s);
    cq_verifier_destroy(cqv);
    grpc_metadata_array_destroy(&request_metadata_recv);
    grpc_call_details_destroy(&call_details);
  }
}

static void perform_request(client_fixture *cf) {
  grpc_call *c;
  cq_verifier *cqv = cq_verifier_create(cf->cq);
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  grpc_byte_buffer *request_payload;
  grpc_byte_buffer *response_payload_recv;
  int i;

  memset(ops, 0, sizeof(ops));
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");

  grpc_slice host = grpc_slice_from_static_string("foo.test.google.fr:1234");
  c = grpc_channel_create_call(cf->client, NULL, GRPC_PROPAGATE_DEFAULTS,
                               cf->cq, grpc_slice_from_static_string("/foo"),
                               &host, grpc_timeout_seconds_to_deadline(5),
                               NULL);
  gpr_log(GPR_INFO, "Call 0x%" PRIxPTR " created", (intptr_t)c);
  GPR_ASSERT(c);
  char *peer;

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);

  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  for (i = 0; i < 4; i++) {
    request_payload = grpc_raw_byte_buffer_create(&request_payload_slice, 1);

    op = ops;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = request_payload;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload_recv;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(2), NULL);
    GPR_ASSERT(GRPC_CALL_OK == error);

    CQ_EXPECT_COMPLETION(cqv, tag(2), 1);
    cq_verify(cqv);
    gpr_log(GPR_INFO, "Client after sending msg %d / 4", i + 1);
    GPR_ASSERT(byte_buffer_eq_string(response_payload_recv, PAYLOAD));

    grpc_byte_buffer_destroy(request_payload);
    grpc_byte_buffer_destroy(response_payload_recv);
  }

  grpc_slice_unref(request_payload_slice);

  op = ops;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(3), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(3), 1);
  cq_verify(cqv);
  peer = grpc_call_get_peer(c);
  gpr_log(GPR_INFO, "Client DONE WITH SERVER %s ", peer);

  grpc_call_destroy(c);

  cq_verify_empty_timeout(cqv, 1 /* seconds */);
  cq_verifier_destroy(cqv);

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_slice_unref(details);
  gpr_log(GPR_INFO, "Client call (peer %s) DESTROYED.", peer);
  gpr_free(peer);
}

#define BALANCERS_NAME "lb.name"
static void setup_client(const server_fixture *lb_server,
                         const server_fixture *backends, client_fixture *cf) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  char *lb_uri;
  // The grpclb LB policy will be automatically selected by virtue of
  // the fact that the returned addresses are balancer addresses.
  gpr_asprintf(&lb_uri, "test:///%s?lb_enabled=1&balancer_names=%s",
               lb_server->servers_hostport, lb_server->balancer_name);

  grpc_arg expected_target_arg;
  expected_target_arg.type = GRPC_ARG_STRING;
  expected_target_arg.key =
      const_cast<char *>(GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS);

  char *expected_target_names = NULL;
  const char *backends_name = lb_server->servers_hostport;
  gpr_asprintf(&expected_target_names, "%s;%s", backends_name, BALANCERS_NAME);

  expected_target_arg.value.string = const_cast<char *>(expected_target_names);
  grpc_channel_args *args =
      grpc_channel_args_copy_and_add(NULL, &expected_target_arg, 1);
  gpr_free(expected_target_names);

  cf->cq = grpc_completion_queue_create(NULL);
  cf->server_uri = lb_uri;
  grpc_channel_credentials *fake_creds =
      grpc_fake_transport_security_credentials_create();
  cf->client =
      grpc_secure_channel_create(fake_creds, cf->server_uri, args, NULL);
  grpc_channel_credentials_unref(&exec_ctx, fake_creds);
  grpc_channel_args_destroy(&exec_ctx, args);
}

static void teardown_client(client_fixture *cf) {
  grpc_completion_queue_shutdown(cf->cq);
  drain_cq(cf->cq);
  grpc_completion_queue_destroy(cf->cq);
  cf->cq = NULL;
  grpc_channel_destroy(cf->client);
  cf->client = NULL;
  gpr_free(cf->server_uri);
}

static void setup_server(const char *host, server_fixture *sf) {
  int assigned_port;

  sf->cq = grpc_completion_queue_create(NULL);
  const char *colon_idx = strchr(host, ':');
  if (colon_idx) {
    const char *port_str = colon_idx + 1;
    sf->port = atoi(port_str);
    sf->servers_hostport = gpr_strdup(host);
  } else {
    sf->port = grpc_pick_unused_port_or_die();
    gpr_join_host_port(&sf->servers_hostport, host, sf->port);
  }

  grpc_server_credentials *server_creds =
      grpc_fake_transport_security_server_credentials_create();

  sf->server = grpc_server_create(NULL, NULL);
  grpc_server_register_completion_queue(sf->server, sf->cq, NULL);
  GPR_ASSERT((assigned_port = grpc_server_add_secure_http2_port(
                  sf->server, sf->servers_hostport, server_creds)) > 0);
  grpc_server_credentials_release(server_creds);
  GPR_ASSERT(sf->port == assigned_port);
  grpc_server_start(sf->server);
}

static void teardown_server(server_fixture *sf) {
  if (!sf->server) return;

  gpr_log(GPR_INFO, "Server[%s] shutting down", sf->servers_hostport);
  grpc_server_shutdown_and_notify(sf->server, sf->cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(
                 sf->cq, tag(1000), grpc_timeout_seconds_to_deadline(5), NULL)
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(sf->server);
  gpr_thd_join(sf->tid);

  sf->server = NULL;
  grpc_completion_queue_shutdown(sf->cq);
  drain_cq(sf->cq);
  grpc_completion_queue_destroy(sf->cq);

  gpr_log(GPR_INFO, "Server[%s] bye bye", sf->servers_hostport);
  gpr_free(sf->servers_hostport);
}

static void fork_backend_server(void *arg) {
  server_fixture *sf = static_cast<server_fixture *>(arg);
  start_backend_server(sf);
}

static void fork_lb_server(void *arg) {
  test_fixture *tf = static_cast<test_fixture *>(arg);
  int ports[NUM_BACKENDS];
  for (int i = 0; i < NUM_BACKENDS; i++) {
    ports[i] = tf->lb_backends[i].port;
  }
  start_lb_server(&tf->lb_server, ports, NUM_BACKENDS,
                  tf->lb_server_update_delay_ms);
}

#define LB_TOKEN_PREFIX "token"
static test_fixture setup_test_fixture(int lb_server_update_delay_ms) {
  test_fixture tf;
  memset(&tf, 0, sizeof(tf));
  tf.lb_server_update_delay_ms = lb_server_update_delay_ms;

  gpr_thd_options options = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&options);

  for (int i = 0; i < NUM_BACKENDS; ++i) {
    // Only the first half of the servers expect an LB token.
    if (i < NUM_BACKENDS / 2) {
      tf.lb_backends[i].lb_token_prefix = LB_TOKEN_PREFIX;
    } else {
      tf.lb_backends[i].lb_token_prefix = "";
    }
    setup_server("127.0.0.1", &tf.lb_backends[i]);
    gpr_thd_new(&tf.lb_backends[i].tid, fork_backend_server, &tf.lb_backends[i],
                &options);
  }

  tf.lb_server.lb_token_prefix = LB_TOKEN_PREFIX;
  tf.lb_server.balancer_name = BALANCERS_NAME;
  setup_server("127.0.0.1", &tf.lb_server);
  gpr_thd_new(&tf.lb_server.tid, fork_lb_server, &tf.lb_server, &options);
  setup_client(&tf.lb_server, tf.lb_backends, &tf.client);
  return tf;
}

static void teardown_test_fixture(test_fixture *tf) {
  teardown_client(&tf->client);
  for (int i = 0; i < NUM_BACKENDS; ++i) {
    teardown_server(&tf->lb_backends[i]);
  }
  teardown_server(&tf->lb_server);
}

// The LB server will send two updates: batch 1 and batch 2. Each batch contains
// two addresses, both of a valid and running backend server. Batch 1 is readily
// available and provided as soon as the client establishes the streaming call.
// Batch 2 is sent after a delay of \a lb_server_update_delay_ms milliseconds.
static test_fixture test_update(int lb_server_update_delay_ms) {
  gpr_log(GPR_INFO, "start %s(%d)", __func__, lb_server_update_delay_ms);
  test_fixture tf = setup_test_fixture(lb_server_update_delay_ms);
  perform_request(
      &tf.client);  // "consumes" 1st backend server of 1st serverlist
  perform_request(
      &tf.client);  // "consumes" 2nd backend server of 1st serverlist

  perform_request(
      &tf.client);  // "consumes" 1st backend server of 2nd serverlist
  perform_request(
      &tf.client);  // "consumes" 2nd backend server of 2nd serverlist

  teardown_test_fixture(&tf);
  gpr_log(GPR_INFO, "end %s(%d)", __func__, lb_server_update_delay_ms);
  return tf;
}

TEST(GrpclbTest, Updates) {
  grpc::test_fixture tf_result;
  // Clients take at least one second to complete a call (the last part of the
  // call sleeps for 1 second while verifying the client's completion queue is
  // empty), more if the system is under load. Therefore:
  //
  // If the LB server waits 800ms before sending an update, it will arrive
  // before the first client request finishes, skipping the second server from
  // batch 1. All subsequent picks will come from the second half of the
  // backends, those coming in the LB update.
  tf_result = grpc::test_update(800);
  GPR_ASSERT(tf_result.lb_backends[0].num_calls_serviced +
                 tf_result.lb_backends[1].num_calls_serviced ==
             1);
  GPR_ASSERT(tf_result.lb_backends[2].num_calls_serviced +
                 tf_result.lb_backends[3].num_calls_serviced >
             0);
  int num_serviced_calls = 0;
  for (int i = 0; i < 4; i++) {
    num_serviced_calls += tf_result.lb_backends[i].num_calls_serviced;
  }
  GPR_ASSERT(num_serviced_calls == 4);

  // If the LB server waits 2500ms, the update arrives after two calls and three
  // picks. The third pick will be the 1st server of the 1st update (RR policy
  // going around). The fourth and final pick will come from the second LB
  // update. In any case, the total number of serviced calls must again be equal
  // to four across all the backends.
  tf_result = grpc::test_update(2500);
  GPR_ASSERT(tf_result.lb_backends[0].num_calls_serviced +
                 tf_result.lb_backends[1].num_calls_serviced >=
             2);
  GPR_ASSERT(tf_result.lb_backends[2].num_calls_serviced +
                 tf_result.lb_backends[3].num_calls_serviced >
             0);
  num_serviced_calls = 0;
  for (int i = 0; i < 4; i++) {
    num_serviced_calls += tf_result.lb_backends[i].num_calls_serviced;
  }
  GPR_ASSERT(num_serviced_calls == 4);
}

TEST(GrpclbTest, InvalidAddressInServerlist) {}

}  // namespace
}  // namespace grpc

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_test_init(argc, argv);
  grpc_fake_resolver_init();
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
