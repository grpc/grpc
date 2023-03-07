//
//
// Copyright 2019 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// close_fd_test tests the behavior of grpc core when the transport gets
// disconnected.
// The test creates an http2 transport over a socket pair and closes the
// client or server file descriptor to simulate connection breakage while
// an RPC call is in progress.
//
//
#include <stdint.h>

#include <initializer_list>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport_fwd.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET_TCP

#include <string.h>
#include <unistd.h>

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/server.h"
#include "test/core/util/test_config.h"

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

typedef struct test_ctx test_ctx;

struct test_ctx {
  // completion queue for call notifications on the server
  grpc_completion_queue* cq;
  // completion queue registered to server for shutdown events
  grpc_completion_queue* shutdown_cq;
  // client's completion queue
  grpc_completion_queue* client_cq;
  // completion queue bound to call on the server
  grpc_completion_queue* bound_cq;
  // Server responds to client calls
  grpc_server* server;
  // Client calls are sent over the channel
  grpc_channel* client;
  // encapsulates client, server endpoints
  grpc_endpoint_pair* ep;
};

static test_ctx g_ctx;

// chttp2 transport that is immediately available (used for testing
// connected_channel without a client_channel

static void server_setup_transport(grpc_transport* transport) {
  grpc_core::ExecCtx exec_ctx;
  grpc_endpoint_add_to_pollset(g_ctx.ep->server, grpc_cq_pollset(g_ctx.cq));
  grpc_core::Server* core_server = grpc_core::Server::FromC(g_ctx.server);
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "SetupTransport",
      core_server->SetupTransport(transport, nullptr,
                                  core_server->channel_args(), nullptr)));
}

static void client_setup_transport(grpc_transport* transport) {
  grpc_core::ExecCtx exec_ctx;
  grpc_endpoint_add_to_pollset(g_ctx.ep->client,
                               grpc_cq_pollset(g_ctx.client_cq));
  grpc_arg authority_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
      const_cast<char*>("test-authority"));
  grpc_channel_args* args =
      grpc_channel_args_copy_and_add(nullptr, &authority_arg, 1);
  // TODO (pjaikumar): use GRPC_CLIENT_CHANNEL instead of
  // GRPC_CLIENT_DIRECT_CHANNEL
  g_ctx.client = (*grpc_core::Channel::Create(
                      "socketpair-target", grpc_core::ChannelArgs::FromC(args),
                      GRPC_CLIENT_DIRECT_CHANNEL, transport))
                     ->c_ptr();
  grpc_channel_args_destroy(args);
}

static void init_client() {
  grpc_core::ExecCtx exec_ctx;
  grpc_transport* transport;
  transport = grpc_create_chttp2_transport(grpc_core::ChannelArgs(),
                                           g_ctx.ep->client, true);
  client_setup_transport(transport);
  GPR_ASSERT(g_ctx.client);
  grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
}

static void init_server() {
  grpc_core::ExecCtx exec_ctx;
  grpc_transport* transport;
  GPR_ASSERT(!g_ctx.server);
  g_ctx.server = grpc_server_create(nullptr, nullptr);
  grpc_server_register_completion_queue(g_ctx.server, g_ctx.cq, nullptr);
  grpc_server_start(g_ctx.server);
  transport = grpc_create_chttp2_transport(grpc_core::ChannelArgs(),
                                           g_ctx.ep->server, false);
  server_setup_transport(transport);
  grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
}

static void test_init() {
  grpc_endpoint_pair* sfd =
      static_cast<grpc_endpoint_pair*>(gpr_malloc(sizeof(grpc_endpoint_pair)));
  memset(&g_ctx, 0, sizeof(g_ctx));
  g_ctx.ep = sfd;
  g_ctx.cq = grpc_completion_queue_create_for_next(nullptr);
  g_ctx.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
  g_ctx.bound_cq = grpc_completion_queue_create_for_next(nullptr);
  g_ctx.client_cq = grpc_completion_queue_create_for_next(nullptr);

  // Create endpoints
  *sfd = grpc_iomgr_create_endpoint_pair("fixture", nullptr);
  // Create client, server and setup transport over endpoint pair
  init_server();
  init_client();
}

static void drain_cq(grpc_completion_queue* cq) {
  grpc_event event;
  do {
    event = grpc_completion_queue_next(cq, grpc_timeout_seconds_to_deadline(1),
                                       nullptr);
  } while (event.type != GRPC_QUEUE_SHUTDOWN);
}

static void drain_and_destroy_cq(grpc_completion_queue* cq) {
  grpc_completion_queue_shutdown(cq);
  drain_cq(cq);
  grpc_completion_queue_destroy(cq);
}

static void shutdown_server() {
  if (!g_ctx.server) return;
  grpc_server_shutdown_and_notify(g_ctx.server, g_ctx.shutdown_cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(g_ctx.shutdown_cq, tag(1000),
                                         grpc_timeout_seconds_to_deadline(1),
                                         nullptr)
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(g_ctx.server);
  g_ctx.server = nullptr;
}

static void shutdown_client() {
  if (!g_ctx.client) return;
  grpc_channel_destroy(g_ctx.client);
  g_ctx.client = nullptr;
}

static void end_test() {
  shutdown_server();
  shutdown_client();

  drain_and_destroy_cq(g_ctx.cq);
  drain_and_destroy_cq(g_ctx.client_cq);
  drain_and_destroy_cq(g_ctx.bound_cq);
  grpc_completion_queue_destroy(g_ctx.shutdown_cq);
  gpr_free(g_ctx.ep);
}

typedef enum fd_type { CLIENT_FD, SERVER_FD } fd_type;

static const char* fd_type_str(fd_type fdtype) {
  if (fdtype == CLIENT_FD) {
    return "client";
  } else if (fdtype == SERVER_FD) {
    return "server";
  } else {
    grpc_core::Crash(absl::StrFormat("Unexpected fd_type %d", fdtype));
  }
}

static void _test_close_before_server_recv(fd_type fdtype) {
  grpc_core::ExecCtx exec_ctx;
  grpc_call* call;
  grpc_call* server_call;
  grpc_event event;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_slice response_payload_slice =
      grpc_slice_from_copied_string("hello you");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer* response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  gpr_log(GPR_INFO, "Running test: test_close_%s_before_server_recv",
          fd_type_str(fdtype));
  test_init();

  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer* request_payload_recv = nullptr;
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_call_details call_details;
  grpc_status_code status = GRPC_STATUS__DO_NOT_USE;
  grpc_call_error error;
  grpc_slice details;

  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(1);
  call = grpc_channel_create_call(
      g_ctx.client, nullptr, GRPC_PROPAGATE_DEFAULTS, g_ctx.client_cq,
      grpc_slice_from_static_string("/foo"), nullptr, deadline, nullptr);
  GPR_ASSERT(call);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(call, ops, static_cast<size_t>(op - ops),
                                tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error = grpc_server_request_call(g_ctx.server, &server_call, &call_details,
                                   &request_metadata_recv, g_ctx.bound_cq,
                                   g_ctx.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  event = grpc_completion_queue_next(
      g_ctx.cq, grpc_timeout_milliseconds_to_deadline(100), nullptr);
  GPR_ASSERT(event.success == 1);
  GPR_ASSERT(event.tag == tag(101));
  GPR_ASSERT(event.type == GRPC_OP_COMPLETE);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;

  grpc_endpoint_pair* sfd = g_ctx.ep;
  int fd;
  if (fdtype == SERVER_FD) {
    fd = sfd->server->vtable->get_fd(sfd->server);
  } else {
    GPR_ASSERT(fdtype == CLIENT_FD);
    fd = sfd->client->vtable->get_fd(sfd->client);
  }
  // Connection is closed before the server receives the client's message.
  close(fd);

  error = grpc_call_start_batch(server_call, ops, static_cast<size_t>(op - ops),
                                tag(102), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  event = grpc_completion_queue_next(
      g_ctx.bound_cq, grpc_timeout_milliseconds_to_deadline(100), nullptr);

  // Batch operation completes on the server side.
  // event.success will be true if the op completes successfully.
  // event.success will be false if the op completes with an error. This can
  // happen due to a race with closing the fd resulting in pending writes
  // failing due to stream closure.
  //
  GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(event.tag == tag(102));

  event = grpc_completion_queue_next(
      g_ctx.client_cq, grpc_timeout_milliseconds_to_deadline(100), nullptr);
  // When the client fd is closed, the server gets EPIPE.
  // When server fd is closed, server gets EBADF.
  // In both cases server sends GRPC_STATUS_UNAVALABLE to the client. However,
  // the client may not receive this grpc_status as it's socket is being closed.
  // If the client didn't get grpc_status from the server it will time out
  // waiting on the completion queue. So there 2 2 possibilities:
  // 1. client times out waiting for server's response
  // 2. client receives GRPC_STATUS_UNAVAILABLE from server
  //
  if (event.type == GRPC_QUEUE_TIMEOUT) {
    GPR_ASSERT(event.success == 0);
    // status is not initialized
    GPR_ASSERT(status == GRPC_STATUS__DO_NOT_USE);
  } else {
    GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(event.success == 1);
    GPR_ASSERT(event.tag == tag(1));
    GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);
  }

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(call);
  grpc_call_unref(server_call);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(response_payload);
  grpc_byte_buffer_destroy(request_payload_recv);
  grpc_byte_buffer_destroy(response_payload_recv);

  end_test();
}

static void test_close_before_server_recv() {
  // Close client side of the connection before server receives message from
  // client
  _test_close_before_server_recv(CLIENT_FD);
  // Close server side of the connection before server receives message from
  // client
  _test_close_before_server_recv(SERVER_FD);
}

static void _test_close_before_server_send(fd_type fdtype) {
  grpc_core::ExecCtx exec_ctx;
  grpc_call* call;
  grpc_call* server_call;
  grpc_event event;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_slice response_payload_slice =
      grpc_slice_from_copied_string("hello you");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer* response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  gpr_log(GPR_INFO, "Running test: test_close_%s_before_server_send",
          fd_type_str(fdtype));
  test_init();

  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer* request_payload_recv = nullptr;
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_call_details call_details;
  grpc_status_code status = GRPC_STATUS__DO_NOT_USE;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(1);
  call = grpc_channel_create_call(
      g_ctx.client, nullptr, GRPC_PROPAGATE_DEFAULTS, g_ctx.client_cq,
      grpc_slice_from_static_string("/foo"), nullptr, deadline, nullptr);
  GPR_ASSERT(call);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(call, ops, static_cast<size_t>(op - ops),
                                tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error = grpc_server_request_call(g_ctx.server, &server_call, &call_details,
                                   &request_metadata_recv, g_ctx.bound_cq,
                                   g_ctx.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  event = grpc_completion_queue_next(
      g_ctx.cq, grpc_timeout_milliseconds_to_deadline(100), nullptr);
  GPR_ASSERT(event.success == 1);
  GPR_ASSERT(event.tag == tag(101));
  GPR_ASSERT(event.type == GRPC_OP_COMPLETE);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(server_call, ops, static_cast<size_t>(op - ops),
                                tag(102), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  event = grpc_completion_queue_next(
      g_ctx.bound_cq, grpc_timeout_milliseconds_to_deadline(100), nullptr);
  GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(event.success == 1);
  GPR_ASSERT(event.tag == tag(102));

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;

  grpc_endpoint_pair* sfd = g_ctx.ep;
  int fd;
  if (fdtype == SERVER_FD) {
    fd = sfd->server->vtable->get_fd(sfd->server);
  } else {
    GPR_ASSERT(fdtype == CLIENT_FD);
    fd = sfd->client->vtable->get_fd(sfd->client);
  }

  // Connection is closed before the server sends message and status to the
  // client.
  close(fd);
  error = grpc_call_start_batch(server_call, ops, static_cast<size_t>(op - ops),
                                tag(103), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  // Batch operation succeeds on the server side
  event = grpc_completion_queue_next(
      g_ctx.bound_cq, grpc_timeout_milliseconds_to_deadline(100), nullptr);
  GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(event.success == 1);
  GPR_ASSERT(event.tag == tag(103));

  event = grpc_completion_queue_next(
      g_ctx.client_cq, grpc_timeout_milliseconds_to_deadline(100), nullptr);
  // In both cases server sends GRPC_STATUS_UNAVALABLE to the client. However,
  // the client may not receive this grpc_status as it's socket is being closed.
  // If the client didn't get grpc_status from the server it will time out
  // waiting on the completion queue
  //
  if (event.type == GRPC_OP_COMPLETE) {
    GPR_ASSERT(event.success == 1);
    GPR_ASSERT(event.tag == tag(1));
    GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);
  } else {
    GPR_ASSERT(event.type == GRPC_QUEUE_TIMEOUT);
    GPR_ASSERT(event.success == 0);
    // status is not initialized
    GPR_ASSERT(status == GRPC_STATUS__DO_NOT_USE);
  }
  GPR_ASSERT(was_cancelled == 0);

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(call);
  grpc_call_unref(server_call);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(response_payload);
  grpc_byte_buffer_destroy(request_payload_recv);
  grpc_byte_buffer_destroy(response_payload_recv);

  end_test();
}

static void test_close_before_server_send() {
  // Close client side of the connection before server sends message to client
  //
  _test_close_before_server_send(CLIENT_FD);
  // Close server side of the connection before server sends message to client
  //
  _test_close_before_server_send(SERVER_FD);
}

static void _test_close_before_client_send(fd_type fdtype) {
  grpc_core::ExecCtx exec_ctx;
  grpc_call* call;
  grpc_event event;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_slice response_payload_slice =
      grpc_slice_from_copied_string("hello you");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer* response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  gpr_log(GPR_INFO, "Running test: test_close_%s_before_client_send",
          fd_type_str(fdtype));
  test_init();

  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer* request_payload_recv = nullptr;
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;

  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(1);
  call = grpc_channel_create_call(
      g_ctx.client, nullptr, GRPC_PROPAGATE_DEFAULTS, g_ctx.client_cq,
      grpc_slice_from_static_string("/foo"), nullptr, deadline, nullptr);
  GPR_ASSERT(call);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;

  grpc_endpoint_pair* sfd = g_ctx.ep;
  int fd;
  if (fdtype == SERVER_FD) {
    fd = sfd->server->vtable->get_fd(sfd->server);
  } else {
    GPR_ASSERT(fdtype == CLIENT_FD);
    fd = sfd->client->vtable->get_fd(sfd->client);
  }
  // Connection is closed before the client sends a batch to the server
  close(fd);

  error = grpc_call_start_batch(call, ops, static_cast<size_t>(op - ops),
                                tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  // Status unavailable is returned to the client when client or server fd is
  // closed
  event = grpc_completion_queue_next(
      g_ctx.client_cq, grpc_timeout_milliseconds_to_deadline(100), nullptr);
  GPR_ASSERT(event.success == 1);
  GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(event.tag == tag(1));
  GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);

  // No event is received on the server
  event = grpc_completion_queue_next(
      g_ctx.cq, grpc_timeout_milliseconds_to_deadline(100), nullptr);
  GPR_ASSERT(event.success == 0);
  GPR_ASSERT(event.type == GRPC_QUEUE_TIMEOUT);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(call);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(response_payload);
  grpc_byte_buffer_destroy(request_payload_recv);
  grpc_byte_buffer_destroy(response_payload_recv);

  end_test();
}
static void test_close_before_client_send() {
  // Close client side of the connection before client sends message to server
  //
  _test_close_before_client_send(CLIENT_FD);
  // Close server side of the connection before client sends message to server
  //
  _test_close_before_client_send(SERVER_FD);
}

static void _test_close_before_call_create(fd_type fdtype) {
  grpc_core::ExecCtx exec_ctx;
  grpc_call* call;
  grpc_event event;
  test_init();

  gpr_timespec deadline = grpc_timeout_milliseconds_to_deadline(100);

  grpc_endpoint_pair* sfd = g_ctx.ep;
  int fd;
  if (fdtype == SERVER_FD) {
    fd = sfd->server->vtable->get_fd(sfd->server);
  } else {
    GPR_ASSERT(fdtype == CLIENT_FD);
    fd = sfd->client->vtable->get_fd(sfd->client);
  }
  // Connection is closed before the client creates a call
  close(fd);

  call = grpc_channel_create_call(
      g_ctx.client, nullptr, GRPC_PROPAGATE_DEFAULTS, g_ctx.client_cq,
      grpc_slice_from_static_string("/foo"), nullptr, deadline, nullptr);
  GPR_ASSERT(call);

  // Client and server time out waiting on their completion queues and nothing
  // is sent or received
  event = grpc_completion_queue_next(
      g_ctx.client_cq, grpc_timeout_milliseconds_to_deadline(100), nullptr);
  GPR_ASSERT(event.type == GRPC_QUEUE_TIMEOUT);
  GPR_ASSERT(event.success == 0);

  event = grpc_completion_queue_next(
      g_ctx.cq, grpc_timeout_milliseconds_to_deadline(100), nullptr);
  GPR_ASSERT(event.type == GRPC_QUEUE_TIMEOUT);
  GPR_ASSERT(event.success == 0);

  grpc_call_unref(call);
  end_test();
}

static void test_close_before_call_create() {
  // Close client side of the connection before client creates a call
  _test_close_before_call_create(CLIENT_FD);
  // Close server side of the connection before client creates a call
  _test_close_before_call_create(SERVER_FD);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  // Init grpc
  grpc_init();
  int iterations = 10;

  for (int i = 0; i < iterations; ++i) {
    test_close_before_call_create();
    test_close_before_client_send();
    test_close_before_server_recv();
    test_close_before_server_send();
  }

  grpc_shutdown();

  return 0;
}

#else  // GRPC_POSIX_SOCKET_TCP

int main(int argc, char** argv) { return 1; }

#endif  // GRPC_POSIX_SOCKET_TCP
