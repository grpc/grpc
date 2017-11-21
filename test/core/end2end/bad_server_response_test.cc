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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>

#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/core/util/test_tcp_server.h"

#define HTTP1_RESP                           \
  "HTTP/1.0 400 Bad Request\n"               \
  "Content-Type: text/html; charset=UTF-8\n" \
  "Content-Length: 0\n"                      \
  "Date: Tue, 07 Jun 2016 17:43:20 GMT\n\n"

#define HTTP2_RESP(STATUS_CODE)          \
  "\x00\x00\x00\x04\x00\x00\x00\x00\x00" \
  "\x00\x00>\x01\x04\x00\x00\x00\x01"    \
  "\x10\x0e"                             \
  "content-length\x01"                   \
  "0"                                    \
  "\x10\x0c"                             \
  "content-type\x10"                     \
  "application/grpc"                     \
  "\x10\x07:status\x03" #STATUS_CODE

#define UNPARSEABLE_RESP "Bad Request\n"

#define HTTP2_DETAIL_MSG(STATUS_CODE) \
  "Received http2 header with status: " #STATUS_CODE

#define HTTP1_DETAIL_MSG "Trying to connect an http1.x server"

/* TODO(zyc) Check the content of incomming data instead of using this length */
/* The 'bad' server will start sending responses after reading this amount of
 * data from the client. */
#define SERVER_INCOMING_DATA_LENGTH_LOWER_THRESHOLD (size_t)200

struct rpc_state {
  char* target;
  grpc_completion_queue* cq;
  grpc_channel* channel;
  grpc_call* call;
  size_t incoming_data_length;
  grpc_slice_buffer temp_incoming_buffer;
  grpc_slice_buffer outgoing_buffer;
  grpc_endpoint* tcp;
  gpr_atm done_atm;
  bool write_done;
  const char* response_payload;
  size_t response_payload_length;
};

static int server_port;
static struct rpc_state state;
static grpc_closure on_read;
static grpc_closure on_write;

static void* tag(intptr_t t) { return (void*)t; }

static void done_write(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {
  GPR_ASSERT(error == GRPC_ERROR_NONE);

  gpr_atm_rel_store(&state.done_atm, 1);
}

static void handle_write(grpc_exec_ctx* exec_ctx) {
  grpc_slice slice = grpc_slice_from_copied_buffer(
      state.response_payload, state.response_payload_length);

  grpc_slice_buffer_reset_and_unref(&state.outgoing_buffer);
  grpc_slice_buffer_add(&state.outgoing_buffer, slice);
  grpc_endpoint_write(exec_ctx, state.tcp, &state.outgoing_buffer, &on_write);
}

static void handle_read(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  state.incoming_data_length += state.temp_incoming_buffer.length;

  size_t i;
  for (i = 0; i < state.temp_incoming_buffer.count; i++) {
    char* dump = grpc_dump_slice(state.temp_incoming_buffer.slices[i],
                                 GPR_DUMP_HEX | GPR_DUMP_ASCII);
    gpr_log(GPR_DEBUG, "Server received: %s", dump);
    gpr_free(dump);
  }

  gpr_log(GPR_DEBUG, "got %" PRIuPTR " bytes, expected %" PRIuPTR " bytes",
          state.incoming_data_length,
          SERVER_INCOMING_DATA_LENGTH_LOWER_THRESHOLD);
  if (state.incoming_data_length >=
      SERVER_INCOMING_DATA_LENGTH_LOWER_THRESHOLD) {
    handle_write(exec_ctx);
  } else {
    grpc_endpoint_read(exec_ctx, state.tcp, &state.temp_incoming_buffer,
                       &on_read);
  }
}

static void on_connect(grpc_exec_ctx* exec_ctx, void* arg, grpc_endpoint* tcp,
                       grpc_pollset* accepting_pollset,
                       grpc_tcp_server_acceptor* acceptor) {
  gpr_free(acceptor);
  test_tcp_server* server = (test_tcp_server*)arg;
  GRPC_CLOSURE_INIT(&on_read, handle_read, nullptr, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&on_write, done_write, nullptr, grpc_schedule_on_exec_ctx);
  grpc_slice_buffer_init(&state.temp_incoming_buffer);
  grpc_slice_buffer_init(&state.outgoing_buffer);
  state.tcp = tcp;
  state.incoming_data_length = 0;
  grpc_endpoint_add_to_pollset(exec_ctx, tcp, server->pollset);
  grpc_endpoint_read(exec_ctx, tcp, &state.temp_incoming_buffer, &on_read);
}

static gpr_timespec n_sec_deadline(int seconds) {
  return gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                      gpr_time_from_seconds(seconds, GPR_TIMESPAN));
}

static void start_rpc(int target_port, grpc_status_code expected_status,
                      const char* expected_detail) {
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_status_code status;
  grpc_call_error error;
  cq_verifier* cqv;
  grpc_slice details;

  state.cq = grpc_completion_queue_create_for_next(nullptr);
  cqv = cq_verifier_create(state.cq);
  gpr_join_host_port(&state.target, "127.0.0.1", target_port);
  state.channel = grpc_insecure_channel_create(state.target, nullptr, nullptr);
  grpc_slice host = grpc_slice_from_static_string("localhost");
  state.call = grpc_channel_create_call(
      state.channel, nullptr, GRPC_PROPAGATE_DEFAULTS, state.cq,
      grpc_slice_from_static_string("/Service/Method"), &host,
      gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
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
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(state.call, ops, (size_t)(op - ops), tag(1),
                                nullptr);

  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == expected_status);
  if (expected_detail != nullptr) {
    GPR_ASSERT(-1 != grpc_slice_slice(details, grpc_slice_from_static_string(
                                                   expected_detail)));
  }

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_slice_unref(details);
  cq_verifier_destroy(cqv);
}

static void cleanup_rpc(grpc_exec_ctx* exec_ctx) {
  grpc_event ev;
  grpc_slice_buffer_destroy_internal(exec_ctx, &state.temp_incoming_buffer);
  grpc_slice_buffer_destroy_internal(exec_ctx, &state.outgoing_buffer);
  grpc_call_unref(state.call);
  grpc_completion_queue_shutdown(state.cq);
  do {
    ev = grpc_completion_queue_next(state.cq, n_sec_deadline(1), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(state.cq);
  grpc_channel_destroy(state.channel);
  gpr_free(state.target);
}

typedef struct {
  test_tcp_server* server;
  gpr_event* signal_when_done;
} poll_args;

static void actually_poll_server(void* arg) {
  poll_args* pa = (poll_args*)arg;
  gpr_timespec deadline = n_sec_deadline(10);
  while (true) {
    bool done = gpr_atm_acq_load(&state.done_atm) != 0;
    gpr_timespec time_left =
        gpr_time_sub(deadline, gpr_now(GPR_CLOCK_REALTIME));
    gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRId64 ".%09d", done,
            time_left.tv_sec, time_left.tv_nsec);
    if (done || gpr_time_cmp(time_left, gpr_time_0(GPR_TIMESPAN)) < 0) {
      break;
    }
    test_tcp_server_poll(pa->server, 1);
  }
  gpr_event_set(pa->signal_when_done, (void*)1);
  gpr_free(pa);
}

static void poll_server_until_read_done(test_tcp_server* server,
                                        gpr_event* signal_when_done) {
  gpr_atm_rel_store(&state.done_atm, 0);
  state.write_done = 0;
  gpr_thd_id id;
  poll_args* pa = (poll_args*)gpr_malloc(sizeof(*pa));
  pa->server = server;
  pa->signal_when_done = signal_when_done;
  gpr_thd_new(&id, actually_poll_server, pa, nullptr);
}

static void run_test(const char* response_payload,
                     size_t response_payload_length,
                     grpc_status_code expected_status,
                     const char* expected_detail) {
  test_tcp_server test_server;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_event ev;

  grpc_init();
  gpr_event_init(&ev);
  server_port = grpc_pick_unused_port_or_die();
  test_tcp_server_init(&test_server, on_connect, &test_server);
  test_tcp_server_start(&test_server, server_port);
  state.response_payload = response_payload;
  state.response_payload_length = response_payload_length;

  /* poll server until sending out the response */
  poll_server_until_read_done(&test_server, &ev);
  start_rpc(server_port, expected_status, expected_detail);
  gpr_event_wait(&ev, gpr_inf_future(GPR_CLOCK_REALTIME));

  /* clean up */
  grpc_endpoint_shutdown(&exec_ctx, state.tcp,
                         GRPC_ERROR_CREATE_FROM_STATIC_STRING("Test Shutdown"));
  grpc_endpoint_destroy(&exec_ctx, state.tcp);
  cleanup_rpc(&exec_ctx);
  grpc_exec_ctx_finish(&exec_ctx);
  test_tcp_server_destroy(&test_server);

  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);

  /* status defined in hpack static table */
  run_test(HTTP2_RESP(204), sizeof(HTTP2_RESP(204)) - 1, GRPC_STATUS_CANCELLED,
           HTTP2_DETAIL_MSG(204));

  run_test(HTTP2_RESP(206), sizeof(HTTP2_RESP(206)) - 1, GRPC_STATUS_CANCELLED,
           HTTP2_DETAIL_MSG(206));

  run_test(HTTP2_RESP(304), sizeof(HTTP2_RESP(304)) - 1, GRPC_STATUS_CANCELLED,
           HTTP2_DETAIL_MSG(304));

  run_test(HTTP2_RESP(400), sizeof(HTTP2_RESP(400)) - 1, GRPC_STATUS_CANCELLED,
           HTTP2_DETAIL_MSG(400));

  run_test(HTTP2_RESP(404), sizeof(HTTP2_RESP(404)) - 1, GRPC_STATUS_CANCELLED,
           HTTP2_DETAIL_MSG(404));

  run_test(HTTP2_RESP(500), sizeof(HTTP2_RESP(500)) - 1, GRPC_STATUS_CANCELLED,
           HTTP2_DETAIL_MSG(500));

  /* status not defined in hpack static table */
  run_test(HTTP2_RESP(401), sizeof(HTTP2_RESP(401)) - 1, GRPC_STATUS_CANCELLED,
           HTTP2_DETAIL_MSG(401));

  run_test(HTTP2_RESP(403), sizeof(HTTP2_RESP(403)) - 1, GRPC_STATUS_CANCELLED,
           HTTP2_DETAIL_MSG(403));

  run_test(HTTP2_RESP(502), sizeof(HTTP2_RESP(502)) - 1, GRPC_STATUS_CANCELLED,
           HTTP2_DETAIL_MSG(502));

  /* unparseable response */
  run_test(UNPARSEABLE_RESP, sizeof(UNPARSEABLE_RESP) - 1, GRPC_STATUS_UNKNOWN,
           nullptr);

  /* http1 response */
  run_test(HTTP1_RESP, sizeof(HTTP1_RESP) - 1, GRPC_STATUS_UNAVAILABLE,
           HTTP1_DETAIL_MSG);

  return 0;
}
