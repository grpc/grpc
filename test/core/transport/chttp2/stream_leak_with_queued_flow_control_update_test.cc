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

#include <inttypes.h>
#include <string.h>

#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/sync.h"
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

  void HandleRpc() {
    grpc_call_details call_details;
    grpc_call_details_init(&call_details);
    grpc_metadata_array request_metadata_recv;
    grpc_metadata_array_init(&request_metadata_recv);
    grpc_slice status_details = grpc_slice_from_static_string("xyz");
    int was_cancelled;
    // request a call
    void* tag = this;
    grpc_call* call;
    grpc_call_error error = grpc_server_request_call(
        server_, &call, &call_details, &request_metadata_recv, cq_, cq_, tag);
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
    grpc_op ops[4];
    grpc_op* op;
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
    op->data.recv_close_on_server.cancelled = &was_cancelled;
    op++;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op++;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = response_payload;
    op++;
    op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
    op->data.send_status_from_server.status = GRPC_STATUS_OK;
    op->data.send_status_from_server.status_details = &status_details;
    op++;
    error = grpc_call_start_batch(call, ops, static_cast<size_t>(op - ops), tag,
                                  nullptr);
    GPR_ASSERT(error == GRPC_CALL_OK);
    event = grpc_completion_queue_next(cq_, gpr_inf_future(GPR_CLOCK_REALTIME),
                                       nullptr);
    GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(event.success);
    GPR_ASSERT(event.tag == tag);
    grpc_byte_buffer_destroy(response_payload);
    grpc_call_unref(call);
  }

  std::string address() const { return address_; }

 private:
  grpc_server* server_;
  grpc_completion_queue* cq_;
  std::string address_;
};

void StartCallAndCloseWrites(grpc_call* call, grpc_completion_queue* cq) {
  grpc_op ops[2];
  grpc_op* op;
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op++;
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
}

void FinishCall(grpc_call* call, grpc_completion_queue* cq) {
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_status_code status = GRPC_STATUS_UNKNOWN;
  grpc_slice details;
  grpc_byte_buffer* recv_payload = nullptr;
  void* tag = call;
  // Note: we're only doing read ops here.  The goal here is to finish the call
  // with a queued stream flow control update, due to receipt of a small
  // message. We won't do anything to explicitly initiate writes on the
  // transport, which could accidentally flush out that queued update.
  grpc_op ops[3];
  grpc_op* op;
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &recv_payload;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op++;
  grpc_call_error error = grpc_call_start_batch(
      call, ops, static_cast<size_t>(op - ops), tag, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  grpc_event event = grpc_completion_queue_next(
      cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  GPR_ASSERT(event.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(event.success);
  GPR_ASSERT(event.tag == tag);
  EXPECT_EQ(status, GRPC_STATUS_OK);
  grpc_byte_buffer_destroy(recv_payload);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_slice_unref(details);
}

class TransportCounter {
 public:
  void InitCallback() {
    grpc_core::MutexLock lock(&mu_);
    ++num_created_;
    ++num_live_;
    gpr_log(GPR_INFO,
            "TransportCounter num_created_=%ld num_live_=%" PRId64
            " InitCallback",
            num_created_, num_live_);
  }

  void DestructCallback() {
    grpc_core::MutexLock lock(&mu_);
    --num_live_;
    gpr_log(GPR_INFO,
            "TransportCounter num_created_=%ld num_live_=%" PRId64
            " DestructCallback",
            num_created_, num_live_);
  }

  int64_t num_live() {
    grpc_core::MutexLock lock(&mu_);
    return num_live_;
  }

  size_t num_created() {
    grpc_core::MutexLock lock(&mu_);
    return num_created_;
  }

 private:
  grpc_core::Mutex mu_;
  int64_t num_live_ ABSL_GUARDED_BY(mu_) = 0;
  size_t num_created_ ABSL_GUARDED_BY(mu_) = 0;
};

TransportCounter* g_transport_counter;

void CounterInitCallback() { g_transport_counter->InitCallback(); }

void CounterDestructCallback() { g_transport_counter->DestructCallback(); }

void EnsureConnectionsArentLeaked(grpc_completion_queue* cq) {
  gpr_log(
      GPR_INFO,
      "The channel has been destroyed, wait for it to shut down and close...");
  // Do a quick initial poll to try to exit the test early if things have
  // already cleaned up.
  GPR_ASSERT(grpc_completion_queue_next(
                 cq,
                 gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                              gpr_time_from_millis(1, GPR_TIMESPAN)),
                 nullptr)
                 .type == GRPC_QUEUE_TIMEOUT);
  if (g_transport_counter->num_created() < 2) {
    gpr_log(GPR_ERROR,
            "g_transport_counter->num_created() == %ld. This means that "
            "g_transport_counter isn't working and this test is broken. At "
            "least a couple of transport objects should have been created.",
            g_transport_counter->num_created());
    GPR_ASSERT(0);
  }
  gpr_timespec overall_deadline = grpc_timeout_seconds_to_deadline(120);
  for (;;) {
    // Note: the main goal of this test is to try to repro a chttp2 stream leak,
    // which also holds on to transports objects.
    int64_t live_transports = g_transport_counter->num_live();
    if (live_transports == 0) return;
    if (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), overall_deadline) > 0) {
      gpr_log(GPR_INFO,
              "g_transport_counter->num_live() never returned 0. "
              "It's likely this test has triggered a connection leak.");
      GPR_ASSERT(0);
    }
    gpr_log(GPR_INFO,
            "g_transport_counter->num_live() returned %" PRId64
            ", keep waiting "
            "until it reaches 0",
            live_transports);
    GPR_ASSERT(grpc_completion_queue_next(
                   cq,
                   gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                gpr_time_from_seconds(1, GPR_TIMESPAN)),
                   nullptr)
                   .type == GRPC_QUEUE_TIMEOUT);
  }
}

TEST(
    Chttp2,
    TestStreamDoesntLeakWhenItsWriteClosedAndThenReadClosedBeforeStartOfReadingMessageAndStatus) {
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  {
    // Prevent pings from client to server and server to client, since they can
    // cause chttp2 to initiate writes and thus dodge the bug we're trying to
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
    // reading the response, so that the client transport marks the stream
    // both read and write closed as soon as it reads a status off the wire.
    StartCallAndCloseWrites(call, cq);
    // Send a small message from server to client. The message needs to be small
    // enough such that the client will queue a stream flow control update,
    // without flushing it out to the wire.
    server.HandleRpc();
    // Do some polling to let the client to pick up the message and status off
    // the wire, *before* it begins the RECV_MESSAGE and RECV_STATUS ops.
    // The timeout here just needs to be long enough that the client has
    // most likely reads everything the server sent it by the time it's done.
    GPR_ASSERT(grpc_completion_queue_next(
                   cq, grpc_timeout_milliseconds_to_deadline(20), nullptr)
                   .type == GRPC_QUEUE_TIMEOUT);
    // Perform the receive message and status. Note that the incoming bytes
    // should already be in the client's buffers by the time we start these ops.
    // Thus, the client should *not* need to urgently send a flow control update
    // to the server, to ensure progress, and it can simply queue the flow
    // control update instead.
    FinishCall(call, cq);
    grpc_call_unref(call);
    grpc_channel_destroy(channel);
    // There should be nothing to prevent stream and transport objects from
    // shutdown and destruction at this point. So check that this happens.
    // The timeout is somewhat arbitrary, and is set long enough so that it's
    // extremely unlikely to be hit due to CPU starvation.
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
  g_transport_counter = new TransportCounter();
  grpc_core::TestOnlySetGlobalHttp2TransportInitCallback(CounterInitCallback);
  grpc_core::TestOnlySetGlobalHttp2TransportDestructCallback(
      CounterDestructCallback);
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
