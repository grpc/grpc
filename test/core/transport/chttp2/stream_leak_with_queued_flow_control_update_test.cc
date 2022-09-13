// Copyright 2022 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <string.h>

#include <thread>
#include <string>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace {

class TestServer {
 public:
  explicit TestServer(grpc_completion_queue* cq,
                      grpc_channel_args* channel_args)
      : cq_(cq) {
    server_ = grpc_server_create(channel_args, nullptr);
    address_ = grpc_core::JoinHostPort("[::1]", grpc_pick_unused_port_or_die());
    grpc_server_register_completion_queue(server_, cq_, nullptr);
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    GPR_ASSERT(
        grpc_server_add_http2_port(server_, address_.c_str(), server_creds));
    grpc_server_credentials_release(server_creds);
    grpc_server_start(server_);
  }

  ~TestServer() {
    grpc_server_shutdown_and_notify(server_, cq_, this /* tag */);
    grpc_event event = grpc_completion_queue_next(
        cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(event.success);
    GPR_ASSERT(event.tag == this);
    grpc_server_destroy(server_);
  }

  void ReceiveCallAndSendMessage() {
    gpr_log(GPR_INFO, "ReceiveCallAndSendMessage BEGIN");
    grpc_call_details call_details;
    grpc_call_details_init(&call_details);
    grpc_metadata_array request_metadata_recv;
    grpc_metadata_array_init(&request_metadata_recv);
    // request a call
    void* tag = this;
    GPR_ASSERT(call_ == nullptr);
    grpc_call_error error = grpc_server_request_call(
        server_, &call_, &call_details, &request_metadata_recv, cq_, cq_, tag);
    GPR_ASSERT(error == GRPC_CALL_OK);
    grpc_event event = grpc_completion_queue_next(
        cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
    grpc_call_details_destroy(&call_details);
    grpc_metadata_array_destroy(&request_metadata_recv);
    GPR_ASSERT(event.success);
    GPR_ASSERT(event.tag == tag);
    // Send a response with a 1-byte payload. The 1-byte length is important
    // because it's enough to get the client to *queue* a flow control update,
    // but not long enough to get the client to initiate a write on that update.
    grpc_slice response_payload_slice = grpc_slice_from_static_string("a");
    grpc_byte_buffer* response_payload =
        grpc_raw_byte_buffer_create(&response_payload_slice, 1);
    grpc_op ops[2];
    grpc_op* op;
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op++;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = response_payload;
    op++;
    error = grpc_call_start_batch(call_, ops, static_cast<size_t>(op - ops), tag,
                                  nullptr);
    GPR_ASSERT(error == GRPC_CALL_OK);
    gpr_log(GPR_INFO, "ReceiveCallAndSendMessage poll CQ");
    event = grpc_completion_queue_next(cq_, gpr_inf_future(GPR_CLOCK_REALTIME),
                                       nullptr);
    GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(event.success);
    GPR_ASSERT(event.tag == tag);
    gpr_log(GPR_INFO, "ReceiveCallAndSendMessage END");
  }

  void SendStatus() {
    gpr_log(GPR_INFO, "SendStatus BEGIN");
    GPR_ASSERT(call_ != nullptr);
    int was_cancelled;
    void* tag = this;
    grpc_op ops[2];
    grpc_op* op;
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
    op->data.recv_close_on_server.cancelled = &was_cancelled;
    op++;
    op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
    op->data.send_status_from_server.status = GRPC_STATUS_OK;
    grpc_slice status_details = grpc_slice_from_static_string("xyz");
    op->data.send_status_from_server.status_details = &status_details;
    op++;
    grpc_call_error error = grpc_call_start_batch(call_, ops, static_cast<size_t>(op - ops), tag,
                                  nullptr);
    GPR_ASSERT(error == GRPC_CALL_OK);
    gpr_log(GPR_INFO, "SendStatus CQ poll begin");
    grpc_event event = grpc_completion_queue_next(cq_, gpr_inf_future(GPR_CLOCK_REALTIME),
                                       nullptr);
    gpr_log(GPR_INFO, "SendStatus CQ poll done");
    GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(event.success);
    GPR_ASSERT(event.tag == tag);
    grpc_call_unref(call_);
    gpr_log(GPR_INFO, "SendStatus END");
  }

  std::string address() const { return address_; }

 private:
  grpc_server* server_;
  grpc_completion_queue* cq_;
  std::string address_;
  grpc_call* call_ = nullptr;
};

void StartCallAndCloseWrites(grpc_call* call, grpc_completion_queue* cq) {
  gpr_log(GPR_INFO, "StartCallAndCloseWrites BEGIN");
  //grpc_slice payload_slice = grpc_slice_from_static_string("a");
  //grpc_byte_buffer* payload =
  //    grpc_raw_byte_buffer_create(&payload_slice, 1);
  grpc_op ops[2];
  grpc_op* op;
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op++;
  //op->op = GRPC_OP_SEND_MESSAGE;
  //op->data.send_message.send_message = payload;
  //op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op++;
  void* tag = call;
  grpc_call_error error = grpc_call_start_batch(
      call, ops, static_cast<size_t>(op - ops), tag, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  grpc_event event = grpc_completion_queue_next(
      cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(event.success);
  GPR_ASSERT(event.tag == tag);
  //grpc_byte_buffer_destroy(payload);
  gpr_log(GPR_INFO, "StartCallAndCloseWrites END");
}

void ReceiveFirstMessage(grpc_call* call, grpc_completion_queue* cq) {
  gpr_log(GPR_INFO, "ReceiveFirstMessage BEGIN");
  grpc_op ops[2];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_byte_buffer* recv_payload = nullptr;
  void* tag = call;
  // Receive initial md and message. This is important because
  // we want to get the client to queue a flow control update
  // after it's done receiving the message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &recv_payload;
  op++;
  grpc_call_error error = grpc_call_start_batch(
      call, ops, static_cast<size_t>(op - ops), tag, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  grpc_event event = grpc_completion_queue_next(
      cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(event.success);
  GPR_ASSERT(event.tag == tag);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_byte_buffer_destroy(recv_payload);
  gpr_log(GPR_INFO, "ReceiveFirstMessage END");
}

void FinishCall(grpc_call* call, grpc_completion_queue* cq, bool attempt_receive_message) {
  gpr_log(GPR_INFO, "FinishCall BEGIN");
  grpc_op ops[3];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_status_code status = GRPC_STATUS_UNKNOWN;
  grpc_slice details;
  grpc_byte_buffer* recv_payload = nullptr;
  const char* error_str = nullptr;
  void* tag = call;
  // Attempt to receive a message again, and a status. The attempt
  // to RECV_MESSAGE op will come up with nothing, because the server isn't
  // sending a second message. However, it's important for our repro that
  // we attempt to receive one, so that we can trigger a previously-broken
  // code path that would queue a flow control update (and add a new ref) on
  // a stream that's in the process of shutting down.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op++;
  if (attempt_receive_message) {
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &recv_payload;
    op++;
  }
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.error_string = &error_str;
  op++;
  grpc_call_error error = grpc_call_start_batch(call, ops, static_cast<size_t>(op - ops), tag,
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  grpc_event event = grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                     nullptr);
  GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(event.success);
  GPR_ASSERT(event.tag == tag);
  EXPECT_EQ(status, GRPC_STATUS_OK);
  EXPECT_EQ(recv_payload, nullptr);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  gpr_log(GPR_INFO, "FinishCall got call details: %s", grpc_dump_slice(details, 0));
  gpr_log(GPR_INFO, "FinishCall got call error str: %s", error_str == nullptr ? "<null>" : error_str);
  gpr_free(const_cast<char*>(error_str));
  grpc_slice_unref(details);
  gpr_log(GPR_INFO, "FinishCall END");
}

void EnsureConnectionsArentLeaked(grpc_completion_queue* cq) {
    gpr_log(
        GPR_INFO,
        "The channel has been destroyed, wait for to shut down and close...");
    gpr_timespec deadline = grpc_timeout_seconds_to_deadline(120);
    bool success = false;
    for (;;) {
      // TODO(apolcyn): grpc_iomgr_count_objects_for_testing() is an internal
      // and unstable API. Consider a different method of detecting leaks if
      // it becomes no longer useable. Perhaps use
      // TestOnlySetGlobalHttp2TransportDestructCallback to check whether
      // transports are still around, for example. Note: at the time of writing,
      // this test is  meant to repro a chttp2 stream leak, which also holds on
      // to transports and iomgr objects.
      size_t active_fds = grpc_iomgr_count_objects_for_testing();
      if (active_fds == 1) {
        success = true;
        break;
      }
      if (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline) > 0) {
        break;
      }
      gpr_log(
          GPR_INFO,
          "grpc_iomgr_count_objects_for_testing() returned %ld, keep waiting "
          "until it reaches 1 (only the server listen socket should remain)",
          active_fds);
      grpc_event event = grpc_completion_queue_next(
          cq,
          gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                       gpr_time_from_seconds(1, GPR_TIMESPAN)),
          nullptr);
      GPR_ASSERT(event.type == GRPC_QUEUE_TIMEOUT);
    }
    if (!success) {
      gpr_log(GPR_INFO,
              "grpc_iomgr_count_objects_for_testing() never returned 1 (only "
              "the server listen socket should remain). "
              "It's likely this test has triggered a connection leak.");
      GPR_ASSERT(0);
    }
}

TEST(
    Chttp2,
    TestStreamDoesntLeakWhenItsWriteClosedAndThenReadClosedWhileReadingMessage) {
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  {
    // Prevent pings from client to server and server to client, since they can
    // cause chttp2 to initiate a write and so dodge the bug we're trying to
    // repro.
    auto channel_args =
        grpc_core::ChannelArgs().Set(GRPC_ARG_HTTP2_BDP_PROBE, 0);
    TestServer server(cq,
                      const_cast<grpc_channel_args*>(channel_args.ToC().get()));
    grpc_channel_credentials* creds = grpc_insecure_credentials_create();
    grpc_channel* channel =
        grpc_channel_create(absl::StrCat("ipv6:", server.address()).c_str(),
                            creds, channel_args.ToC().get());
    grpc_channel_credentials_release(creds);
    grpc_call* call =
        grpc_channel_create_call(channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                                 grpc_slice_from_static_string("/foo"), nullptr,
                                 gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    // Start the call. It's important for our repro to close writes before
    // reading the response.
    StartCallAndCloseWrites(call, cq);
    // Send a small message from server to client
    server.ReceiveCallAndSendMessage();
    //  // Poll for a bit extra, to be extra sure that the client transport reads
    //  // the status and marks the stream "read closed" (the CQ is shared by both
    //  // client and server).
    //  GPR_ASSERT(grpc_completion_queue_next(
    //      cq,
    //      grpc_timeout_milliseconds_to_deadline(100),
    //      nullptr).type == GRPC_QUEUE_TIMEOUT);
    //ReceiveFirstMessage(call, cq);
    //std::thread send_status_thd([&server]() {
    //  // Sleep for a tad before sending status, to make it highly likely that
    //  // the client initiates the next batch of RECV_MESSAGE and RECV_STATUS
    //  // stream ops BEFORE it actually reads the status off the wire.
    //  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
    //  server.SendStatus();
    //});
    server.SendStatus();
    // Do some polling to the client to pick up the message and status off
    // the wire, before it's began the RECV_MESSAGE and RECV_STATUS ops.
    GPR_ASSERT(grpc_completion_queue_next(
        cq, grpc_timeout_milliseconds_to_deadline(10), nullptr
        ).type == GRPC_QUEUE_TIMEOUT);
    FinishCall(call, cq, true /* attempt_receive_message */);
    //send_status_thd.join();
    grpc_call_unref(call);
    grpc_channel_destroy(channel);
    EnsureConnectionsArentLeaked(cq);
  }
  grpc_completion_queue_shutdown(cq);
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN) {
  }
  grpc_completion_queue_destroy(cq);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
