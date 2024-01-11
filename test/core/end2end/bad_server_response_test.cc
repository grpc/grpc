//
//
// Copyright 2016 gRPC authors.
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
//

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/event_engine/shim.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/core/util/test_tcp_server.h"

#define HTTP1_RESP_400                       \
  "HTTP/1.0 400 Bad Request\n"               \
  "Content-Type: text/html; charset=UTF-8\n" \
  "Content-Length: 0\n"                      \
  "Date: Tue, 07 Jun 2016 17:43:20 GMT\n\n"

#define HTTP2_SETTINGS_FRAME "\x00\x00\x00\x04\x00\x00\x00\x00\x00"

#define HTTP2_RESP(STATUS_CODE)       \
  "\x00\x00>\x01\x04\x00\x00\x00\x01" \
  "\x10\x0e"                          \
  "content-length\x01"                \
  "0"                                 \
  "\x10\x0c"                          \
  "content-type\x10"                  \
  "application/grpc"                  \
  "\x10\x07:status\x03" #STATUS_CODE

#define UNPARSEABLE_RESP "Bad Request\n"

#define HTTP2_DETAIL_MSG(STATUS_CODE) \
  "Received http2 header with status: " #STATUS_CODE

// TODO(zyc) Check the content of incoming data instead of using this length
// The 'bad' server will start sending responses after reading this amount of
// data from the client.
#define SERVER_INCOMING_DATA_LENGTH_LOWER_THRESHOLD (size_t)200

struct rpc_state {
  std::string target;
  grpc_completion_queue* cq;
  grpc_channel* channel;
  grpc_call* call;
  size_t incoming_data_length;
  grpc_slice_buffer temp_incoming_buffer;
  grpc_slice_buffer outgoing_buffer;
  grpc_endpoint* tcp;
  gpr_atm done_atm;
  bool http2_response;
  bool send_settings;
  const char* response_payload;
  size_t response_payload_length;
  bool connection_attempt_made;
  std::unique_ptr<grpc_core::Notification> on_connect_done;
};

static int server_port;
static struct rpc_state state;
static grpc_closure on_read;
static grpc_closure on_writing_settings_frame;
static grpc_closure on_write;

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

static void done_write(void* /*arg*/, grpc_error_handle error) {
  GPR_ASSERT(error.ok());
  gpr_atm_rel_store(&state.done_atm, 1);
}

static void done_writing_settings_frame(void* /* arg */,
                                        grpc_error_handle error) {
  GPR_ASSERT(error.ok());
  grpc_endpoint_read(state.tcp, &state.temp_incoming_buffer, &on_read,
                     /*urgent=*/false, /*min_progress_size=*/1);
}

static void handle_write() {
  grpc_slice slice = grpc_slice_from_copied_buffer(
      state.response_payload, state.response_payload_length);

  grpc_slice_buffer_reset_and_unref(&state.outgoing_buffer);
  grpc_slice_buffer_add(&state.outgoing_buffer, slice);
  grpc_endpoint_write(state.tcp, &state.outgoing_buffer, &on_write, nullptr,
                      /*max_frame_size=*/INT_MAX);
}

static void handle_read(void* /*arg*/, grpc_error_handle error) {
  if (!error.ok()) {
    gpr_log(GPR_ERROR, "handle_read error: %s",
            grpc_core::StatusToString(error).c_str());
    return;
  }
  state.incoming_data_length += state.temp_incoming_buffer.length;

  size_t i;
  for (i = 0; i < state.temp_incoming_buffer.count; i++) {
    char* dump = grpc_dump_slice(state.temp_incoming_buffer.slices[i],
                                 GPR_DUMP_HEX | GPR_DUMP_ASCII);
    gpr_log(GPR_DEBUG, "Server received: %s", dump);
    gpr_free(dump);
  }

  gpr_log(GPR_DEBUG,
          "got %" PRIuPTR " bytes, expected %" PRIuPTR
          " bytes or a non-HTTP2 response to be sent",
          state.incoming_data_length,
          SERVER_INCOMING_DATA_LENGTH_LOWER_THRESHOLD);
  if (state.incoming_data_length >=
          SERVER_INCOMING_DATA_LENGTH_LOWER_THRESHOLD ||
      !state.http2_response) {
    handle_write();
  } else {
    grpc_endpoint_read(state.tcp, &state.temp_incoming_buffer, &on_read,
                       /*urgent=*/false, /*min_progress_size=*/1);
  }
}

static void on_connect(void* arg, grpc_endpoint* tcp,
                       grpc_pollset* /*accepting_pollset*/,
                       grpc_tcp_server_acceptor* acceptor) {
  gpr_free(acceptor);
  test_tcp_server* server = static_cast<test_tcp_server*>(arg);
  GRPC_CLOSURE_INIT(&on_read, handle_read, nullptr, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&on_writing_settings_frame, done_writing_settings_frame,
                    nullptr, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&on_write, done_write, nullptr, grpc_schedule_on_exec_ctx);
  grpc_slice_buffer_init(&state.temp_incoming_buffer);
  grpc_slice_buffer_init(&state.outgoing_buffer);
  state.connection_attempt_made = true;
  state.tcp = tcp;
  state.incoming_data_length = 0;
  grpc_endpoint_add_to_pollset(tcp, server->pollset[0]);
  if (state.send_settings) {
    // Send settings frame from server
    grpc_slice slice = grpc_slice_from_static_buffer(
        HTTP2_SETTINGS_FRAME, sizeof(HTTP2_SETTINGS_FRAME) - 1);
    grpc_slice_buffer_add(&state.outgoing_buffer, slice);
    grpc_endpoint_write(state.tcp, &state.outgoing_buffer,
                        &on_writing_settings_frame, nullptr,
                        /*max_frame_size=*/INT_MAX);
  } else {
    grpc_endpoint_read(state.tcp, &state.temp_incoming_buffer, &on_read,
                       /*urgent=*/false, /*min_progress_size=*/1);
  }
  state.on_connect_done->Notify();
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
  grpc_slice details;

  state.cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_core::CqVerifier cqv(state.cq);
  state.target = grpc_core::JoinHostPort("127.0.0.1", target_port);

  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  state.channel = grpc_channel_create(state.target.c_str(), creds, nullptr);
  grpc_channel_credentials_release(creds);
  grpc_slice host = grpc_slice_from_static_string("localhost");
  // The default connect deadline is 20 seconds, so reduce the RPC deadline to 1
  // second. This helps us verify - a) If the server responded with a non-HTTP2
  // response, the connect fails immediately resulting in
  // GRPC_STATUS_UNAVAILABLE instead of GRPC_STATUS_DEADLINE_EXCEEDED. b) If the
  // server does not send a HTTP2 SETTINGs frame, the RPC fails with a
  // DEADLINE_EXCEEDED.
  state.call = grpc_channel_create_call(
      state.channel, nullptr, GRPC_PROPAGATE_DEFAULTS, state.cq,
      grpc_slice_from_static_string("/Service/Method"), &host,
      n_sec_deadline(5), nullptr);

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
  error = grpc_call_start_batch(state.call, ops, static_cast<size_t>(op - ops),
                                tag(1), nullptr);

  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(tag(1), true);
  cqv.Verify();

  GPR_ASSERT(status == expected_status);
  if (expected_detail != nullptr) {
    GPR_ASSERT(-1 != grpc_slice_slice(details, grpc_slice_from_static_string(
                                                   expected_detail)));
  }

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_slice_unref(details);
}

static void cleanup_rpc() {
  grpc_event ev;
  grpc_slice_buffer_destroy(&state.temp_incoming_buffer);
  grpc_slice_buffer_destroy(&state.outgoing_buffer);
  grpc_call_unref(state.call);
  grpc_completion_queue_shutdown(state.cq);
  do {
    ev = grpc_completion_queue_next(state.cq, n_sec_deadline(1), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(state.cq);
  grpc_channel_destroy(state.channel);
  state.target.clear();
}

typedef struct {
  test_tcp_server* server;
  gpr_event* signal_when_done;
} poll_args;

static void actually_poll_server(void* arg) {
  poll_args* pa = static_cast<poll_args*>(arg);
  gpr_timespec deadline = n_sec_deadline(5);
  while (true) {
    bool done = gpr_atm_acq_load(&state.done_atm) != 0;
    gpr_timespec time_left =
        gpr_time_sub(deadline, gpr_now(GPR_CLOCK_REALTIME));
    gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRId64 ".%09d", done,
            time_left.tv_sec, time_left.tv_nsec);
    if (done || gpr_time_cmp(time_left, gpr_time_0(GPR_TIMESPAN)) < 0) {
      break;
    }
    int milliseconds = 1000;
    if (grpc_event_engine::experimental::UseEventEngineListener()) {
      milliseconds = 10;
    }
    test_tcp_server_poll(pa->server, milliseconds);
  }
  gpr_event_set(pa->signal_when_done, reinterpret_cast<void*>(1));
  gpr_free(pa);
}

static grpc_core::Thread* poll_server_until_read_done(
    test_tcp_server* server, gpr_event* signal_when_done) {
  gpr_atm_rel_store(&state.done_atm, 0);
  state.connection_attempt_made = false;
  poll_args* pa = static_cast<poll_args*>(gpr_malloc(sizeof(*pa)));
  pa->server = server;
  pa->signal_when_done = signal_when_done;
  auto* th =
      new grpc_core::Thread("grpc_poll_server", actually_poll_server, pa);
  th->Start();
  return th;
}

static void run_test(bool http2_response, bool send_settings,
                     const char* response_payload,
                     size_t response_payload_length,
                     grpc_status_code expected_status,
                     const char* expected_detail) {
  test_tcp_server test_server;
  grpc_core::ExecCtx exec_ctx;
  gpr_event ev;

  grpc_init();
  gpr_event_init(&ev);
  server_port = grpc_pick_unused_port_or_die();
  test_tcp_server_init(&test_server, on_connect, &test_server);
  test_tcp_server_start(&test_server, server_port);
  state.on_connect_done = std::make_unique<grpc_core::Notification>();
  state.http2_response = http2_response;
  state.send_settings = send_settings;
  state.response_payload = response_payload;
  state.response_payload_length = response_payload_length;

  // poll server until sending out the response
  std::unique_ptr<grpc_core::Thread> thdptr(
      poll_server_until_read_done(&test_server, &ev));
  start_rpc(server_port, expected_status, expected_detail);
  gpr_event_wait(&ev, gpr_inf_future(GPR_CLOCK_REALTIME));
  thdptr->Join();
  state.on_connect_done->WaitForNotification();
  // Proof that the server accepted the TCP connection.
  GPR_ASSERT(state.connection_attempt_made == true);
  // clean up
  grpc_endpoint_shutdown(state.tcp, GRPC_ERROR_CREATE("Test Shutdown"));
  grpc_endpoint_destroy(state.tcp);
  cleanup_rpc();
  grpc_core::ExecCtx::Get()->Flush();
  test_tcp_server_destroy(&test_server);

  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  // status defined in hpack static table
  run_test(true, true, HTTP2_RESP(204), sizeof(HTTP2_RESP(204)) - 1,
           GRPC_STATUS_UNKNOWN, HTTP2_DETAIL_MSG(204));
  run_test(true, true, HTTP2_RESP(206), sizeof(HTTP2_RESP(206)) - 1,
           GRPC_STATUS_UNKNOWN, HTTP2_DETAIL_MSG(206));
  run_test(true, true, HTTP2_RESP(304), sizeof(HTTP2_RESP(304)) - 1,
           GRPC_STATUS_UNKNOWN, HTTP2_DETAIL_MSG(304));
  run_test(true, true, HTTP2_RESP(400), sizeof(HTTP2_RESP(400)) - 1,
           GRPC_STATUS_INTERNAL, HTTP2_DETAIL_MSG(400));
  run_test(true, true, HTTP2_RESP(404), sizeof(HTTP2_RESP(404)) - 1,
           GRPC_STATUS_UNIMPLEMENTED, HTTP2_DETAIL_MSG(404));
  run_test(true, true, HTTP2_RESP(500), sizeof(HTTP2_RESP(500)) - 1,
           GRPC_STATUS_UNKNOWN, HTTP2_DETAIL_MSG(500));

  // status not defined in hpack static table
  run_test(true, true, HTTP2_RESP(401), sizeof(HTTP2_RESP(401)) - 1,
           GRPC_STATUS_UNAUTHENTICATED, HTTP2_DETAIL_MSG(401));
  run_test(true, true, HTTP2_RESP(403), sizeof(HTTP2_RESP(403)) - 1,
           GRPC_STATUS_PERMISSION_DENIED, HTTP2_DETAIL_MSG(403));
  run_test(true, true, HTTP2_RESP(429), sizeof(HTTP2_RESP(429)) - 1,
           GRPC_STATUS_UNAVAILABLE, HTTP2_DETAIL_MSG(429));
  run_test(true, true, HTTP2_RESP(499), sizeof(HTTP2_RESP(499)) - 1,
           GRPC_STATUS_UNKNOWN, HTTP2_DETAIL_MSG(499));
  run_test(true, true, HTTP2_RESP(502), sizeof(HTTP2_RESP(502)) - 1,
           GRPC_STATUS_UNAVAILABLE, HTTP2_DETAIL_MSG(502));
  run_test(true, true, HTTP2_RESP(503), sizeof(HTTP2_RESP(503)) - 1,
           GRPC_STATUS_UNAVAILABLE, HTTP2_DETAIL_MSG(503));
  run_test(true, true, HTTP2_RESP(504), sizeof(HTTP2_RESP(504)) - 1,
           GRPC_STATUS_UNAVAILABLE, HTTP2_DETAIL_MSG(504));
  // unparseable response. RPC should fail immediately due to a connect
  // failure.
  //
  run_test(false, false, UNPARSEABLE_RESP, sizeof(UNPARSEABLE_RESP) - 1,
           GRPC_STATUS_UNAVAILABLE, nullptr);

  // http1 response. RPC should fail immediately due to a connect failure.
  run_test(false, false, HTTP1_RESP_400, sizeof(HTTP1_RESP_400) - 1,
           GRPC_STATUS_UNAVAILABLE, nullptr);

  // http2 response without sending a SETTINGs frame. RPC should fail with
  // DEADLINE_EXCEEDED since the RPC deadline is lower than the connection
  // attempt deadline.
  run_test(true, false, HTTP2_RESP(404), sizeof(HTTP2_RESP(404)) - 1,
           GRPC_STATUS_DEADLINE_EXCEEDED, nullptr);
  grpc_shutdown();
  return 0;
}
