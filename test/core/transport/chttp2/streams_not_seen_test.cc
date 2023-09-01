//
//
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
//
//

#include <grpc/support/port_platform.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/frame_goaway.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/core/util/test_tcp_server.h"

namespace grpc_core {
namespace {

void* Tag(intptr_t t) { return reinterpret_cast<void*>(t); }

// A filter that records state about trailing metadata.
class TrailingMetadataRecordingFilter {
 public:
  static grpc_channel_filter kFilterVtable;

  static bool trailing_metadata_available() {
    return trailing_metadata_available_;
  }

  static void reset_trailing_metadata_available() {
    trailing_metadata_available_ = false;
  }

  static absl::optional<GrpcStreamNetworkState::ValueType>
  stream_network_state() {
    return stream_network_state_;
  }

  static void reset_stream_network_state() {
    stream_network_state_ = absl::nullopt;
  }

  static void reset_state() {
    reset_trailing_metadata_available();
    reset_stream_network_state();
  }

 private:
  class CallData {
   public:
    static grpc_error_handle Init(grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
      new (elem->call_data) CallData(args);
      return absl::OkStatus();
    }

    static void Destroy(grpc_call_element* elem,
                        const grpc_call_final_info* /*final_info*/,
                        grpc_closure* /*ignored*/) {
      auto* calld = static_cast<CallData*>(elem->call_data);
      calld->~CallData();
    }

    static void StartTransportStreamOpBatch(
        grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
      auto* calld = static_cast<CallData*>(elem->call_data);
      if (batch->recv_initial_metadata) {
        calld->trailing_metadata_available_ =
            batch->payload->recv_initial_metadata.trailing_metadata_available;
        calld->original_recv_initial_metadata_ready_ =
            batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
            &calld->recv_initial_metadata_ready_;
      }
      if (batch->recv_trailing_metadata) {
        calld->recv_trailing_metadata_ =
            batch->payload->recv_trailing_metadata.recv_trailing_metadata;
        calld->original_recv_trailing_metadata_ready_ =
            batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
            &calld->recv_trailing_metadata_ready_;
      }
      grpc_call_next_op(elem, batch);
    }

   private:
    explicit CallData(const grpc_call_element_args* /*args*/) {
      GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_, RecvInitialMetadataReady,
                        this, nullptr);
      GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                        RecvTrailingMetadataReady, this, nullptr);
    }

    static void RecvInitialMetadataReady(void* arg, grpc_error_handle error) {
      auto* calld = static_cast<CallData*>(arg);
      TrailingMetadataRecordingFilter::trailing_metadata_available_ =
          *calld->trailing_metadata_available_;
      Closure::Run(DEBUG_LOCATION, calld->original_recv_initial_metadata_ready_,
                   error);
    }

    static void RecvTrailingMetadataReady(void* arg, grpc_error_handle error) {
      auto* calld = static_cast<CallData*>(arg);
      stream_network_state_ =
          calld->recv_trailing_metadata_->get(GrpcStreamNetworkState());
      Closure::Run(DEBUG_LOCATION,
                   calld->original_recv_trailing_metadata_ready_, error);
    }

    bool* trailing_metadata_available_ = nullptr;
    grpc_closure recv_initial_metadata_ready_;
    grpc_closure* original_recv_initial_metadata_ready_ = nullptr;
    grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
    grpc_closure recv_trailing_metadata_ready_;
    grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
  };

  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* /*args*/) {
    new (elem->channel_data) TrailingMetadataRecordingFilter();
    return absl::OkStatus();
  }

  static void Destroy(grpc_channel_element* elem) {
    auto* chand =
        static_cast<TrailingMetadataRecordingFilter*>(elem->channel_data);
    chand->~TrailingMetadataRecordingFilter();
  }

  static bool trailing_metadata_available_;
  static absl::optional<GrpcStreamNetworkState::ValueType>
      stream_network_state_;
};

grpc_channel_filter TrailingMetadataRecordingFilter::kFilterVtable = {
    CallData::StartTransportStreamOpBatch,
    nullptr,
    grpc_channel_next_op,
    sizeof(CallData),
    CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CallData::Destroy,
    sizeof(TrailingMetadataRecordingFilter),
    Init,
    grpc_channel_stack_no_post_init,
    Destroy,
    grpc_channel_next_get_info,
    "trailing-metadata-recording-filter",
};
bool TrailingMetadataRecordingFilter::trailing_metadata_available_;
absl::optional<GrpcStreamNetworkState::ValueType>
    TrailingMetadataRecordingFilter::stream_network_state_;

class StreamsNotSeenTest : public ::testing::Test {
 protected:
  explicit StreamsNotSeenTest(bool server_allows_streams = true)
      : server_allows_streams_(server_allows_streams) {
    // Reset the filter state
    TrailingMetadataRecordingFilter::reset_state();
    grpc_slice_buffer_init(&read_buffer_);
    GRPC_CLOSURE_INIT(&on_read_done_, OnReadDone, this, nullptr);
    // Start the test tcp server
    port_ = grpc_pick_unused_port_or_die();
    test_tcp_server_init(&server_, OnConnect, this);
    test_tcp_server_start(&server_, port_);
    // Start polling on the test tcp server
    server_poll_thread_ = std::make_unique<std::thread>([this]() {
      while (!shutdown_) {
        test_tcp_server_poll(&server_, 10);
      }
    });
    // Create the channel
    cq_ = grpc_completion_queue_create_for_next(nullptr);
    cqv_ = std::make_unique<CqVerifier>(cq_);
    grpc_arg client_args[] = {
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA), 0),
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_HTTP2_BDP_PROBE), 0),
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_ENABLE_RETRIES), 0)};
    grpc_channel_args client_channel_args = {GPR_ARRAY_SIZE(client_args),
                                             client_args};
    grpc_channel_credentials* creds = grpc_insecure_credentials_create();
    channel_ = grpc_channel_create(JoinHostPort("127.0.0.1", port_).c_str(),
                                   creds, &client_channel_args);
    grpc_channel_credentials_release(creds);
    // Wait for the channel to connect
    grpc_connectivity_state state = grpc_channel_check_connectivity_state(
        channel_, /*try_to_connect=*/true);
    while (state != GRPC_CHANNEL_READY) {
      grpc_channel_watch_connectivity_state(
          channel_, state, grpc_timeout_seconds_to_deadline(1), cq_, Tag(1));
      cqv_->Expect(Tag(1), true);
      cqv_->Verify(Duration::Seconds(5));
      state = grpc_channel_check_connectivity_state(channel_, false);
    }
    ExecCtx::Get()->Flush();
    GPR_ASSERT(
        connect_notification_.WaitForNotificationWithTimeout(absl::Seconds(1)));
  }

  ~StreamsNotSeenTest() override {
    cqv_.reset();
    grpc_completion_queue_shutdown(cq_);
    grpc_event ev;
    do {
      ev = grpc_completion_queue_next(cq_, grpc_timeout_seconds_to_deadline(1),
                                      nullptr);
    } while (ev.type != GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cq_);
    grpc_channel_destroy(channel_);
    grpc_endpoint_shutdown(tcp_, GRPC_ERROR_CREATE("Test Shutdown"));
    ExecCtx::Get()->Flush();
    GPR_ASSERT(read_end_notification_.WaitForNotificationWithTimeout(
        absl::Seconds(5)));
    grpc_endpoint_destroy(tcp_);
    shutdown_ = true;
    server_poll_thread_->join();
    test_tcp_server_destroy(&server_);
    ExecCtx::Get()->Flush();
  }

  static void OnConnect(void* arg, grpc_endpoint* tcp,
                        grpc_pollset* /* accepting_pollset */,
                        grpc_tcp_server_acceptor* acceptor) {
    gpr_free(acceptor);
    StreamsNotSeenTest* self = static_cast<StreamsNotSeenTest*>(arg);
    self->tcp_ = tcp;
    grpc_endpoint_add_to_pollset(tcp, self->server_.pollset[0]);
    grpc_endpoint_read(tcp, &self->read_buffer_, &self->on_read_done_, false,
                       /*min_progress_size=*/1);
    std::thread([self]() {
      ExecCtx exec_ctx;
      // Send settings frame from server
      if (self->server_allows_streams_) {
        constexpr char kHttp2SettingsFrame[] =
            "\x00\x00\x00\x04\x00\x00\x00\x00\x00";
        self->Write(absl::string_view(kHttp2SettingsFrame,
                                      sizeof(kHttp2SettingsFrame) - 1));
      } else {
        // Create a settings frame with a max concurrent stream setting of 0
        constexpr char kHttp2SettingsFrame[] =
            "\x00\x00\x06\x04\x00\x00\x00\x00\x00\x00\x03\x00\x00\x00\x00";
        self->Write(absl::string_view(kHttp2SettingsFrame,
                                      sizeof(kHttp2SettingsFrame) - 1));
      }
      self->connect_notification_.Notify();
    }).detach();
  }

  // This is a blocking call. It waits for the write callback to be invoked
  // before returning. (In other words, do not call this from a thread that
  // should not be blocked, for example, a polling thread.)
  void Write(absl::string_view bytes) {
    grpc_slice slice =
        StaticSlice::FromStaticBuffer(bytes.data(), bytes.size()).TakeCSlice();
    grpc_slice_buffer buffer;
    grpc_slice_buffer_init(&buffer);
    grpc_slice_buffer_add(&buffer, slice);
    WriteBuffer(&buffer);
    grpc_slice_buffer_destroy(&buffer);
  }

  void SendPing() {
    // Send and recv ping ack
    const char ping_bytes[] =
        "\x00\x00\x08\x06\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
    const char ping_ack_bytes[] =
        "\x00\x00\x08\x06\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
    Write(absl::string_view(ping_bytes, sizeof(ping_bytes) - 1));
    WaitForReadBytes(
        absl::string_view(ping_ack_bytes, sizeof(ping_ack_bytes) - 1));
  }

  void SendGoaway(uint32_t last_stream_id) {
    grpc_slice_buffer buffer;
    grpc_slice_buffer_init(&buffer);
    grpc_chttp2_goaway_append(last_stream_id, 0, grpc_empty_slice(), &buffer);
    WriteBuffer(&buffer);
    grpc_slice_buffer_destroy(&buffer);
  }

  void WriteBuffer(grpc_slice_buffer* buffer) {
    Notification on_write_done_notification_;
    GRPC_CLOSURE_INIT(&on_write_done_, OnWriteDone,
                      &on_write_done_notification_, nullptr);
    grpc_endpoint_write(tcp_, buffer, &on_write_done_, nullptr,
                        /*max_frame_size=*/INT_MAX);
    ExecCtx::Get()->Flush();
    GPR_ASSERT(on_write_done_notification_.WaitForNotificationWithTimeout(
        absl::Seconds(5)));
  }

  static void OnWriteDone(void* arg, grpc_error_handle error) {
    GPR_ASSERT(error.ok());
    Notification* on_write_done_notification_ = static_cast<Notification*>(arg);
    on_write_done_notification_->Notify();
  }

  static void OnReadDone(void* arg, grpc_error_handle error) {
    StreamsNotSeenTest* self = static_cast<StreamsNotSeenTest*>(arg);
    if (error.ok()) {
      {
        MutexLock lock(&self->mu_);
        for (size_t i = 0; i < self->read_buffer_.count; ++i) {
          absl::StrAppend(&self->read_bytes_,
                          StringViewFromSlice(self->read_buffer_.slices[i]));
        }
        self->read_cv_.SignalAll();
      }
      grpc_slice_buffer_reset_and_unref(&self->read_buffer_);
      grpc_endpoint_read(self->tcp_, &self->read_buffer_, &self->on_read_done_,
                         false, /*min_progress_size=*/1);
    } else {
      grpc_slice_buffer_destroy(&self->read_buffer_);
      self->read_end_notification_.Notify();
    }
  }

  // Waits for \a bytes to show up in read_bytes_
  void WaitForReadBytes(absl::string_view bytes) {
    std::atomic<bool> done{false};
    std::thread cq_driver([&]() {
      while (!done) {
        grpc_event ev = grpc_completion_queue_next(
            cq_, grpc_timeout_milliseconds_to_deadline(10), nullptr);
        GPR_ASSERT(ev.type == GRPC_QUEUE_TIMEOUT);
      }
    });
    {
      MutexLock lock(&mu_);
      while (!absl::StrContains(read_bytes_, bytes)) {
        read_cv_.WaitWithTimeout(&mu_, absl::Seconds(5));
      }
    }
    done = true;
    cq_driver.join();
  }

  // Flag to check whether the server's MAX_CONCURRENT_STREAM setting is
  // non-zero or not.
  bool server_allows_streams_;
  int port_;
  test_tcp_server server_;
  std::unique_ptr<std::thread> server_poll_thread_;
  grpc_endpoint* tcp_ = nullptr;
  Notification connect_notification_;
  grpc_slice_buffer read_buffer_;
  grpc_closure on_write_done_;
  grpc_closure on_read_done_;
  Notification read_end_notification_;
  std::string read_bytes_ ABSL_GUARDED_BY(mu_);
  grpc_channel* channel_ = nullptr;
  grpc_completion_queue* cq_ = nullptr;
  std::unique_ptr<CqVerifier> cqv_;
  Mutex mu_;
  CondVar read_cv_;
  std::atomic<bool> shutdown_{false};
};

// Client's HTTP2 transport starts a new stream, sends the request on the wire,
// but receives a GOAWAY with a stream ID of 0, meaning that the request was
// unseen by the server.The test verifies that the HTTP2 transport adds
// GrpcNetworkStreamState with a value of kNotSeenByServer to the trailing
// metadata.
TEST_F(StreamsNotSeenTest, StartStreamBeforeGoaway) {
  grpc_call* c =
      grpc_channel_create_call(channel_, nullptr, GRPC_PROPAGATE_DEFAULTS, cq_,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               grpc_timeout_seconds_to_deadline(1), nullptr);
  GPR_ASSERT(c);
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_op* op;
  grpc_op ops[6];
  grpc_status_code status;
  const char* error_string;
  grpc_call_error error;
  grpc_slice details;
  // Send the request
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
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), Tag(101),
                                nullptr);
  cqv_->Expect(Tag(101), true);
  cqv_->Verify();
  // Send a goaway from server signalling that the request was unseen by the
  // server.
  SendGoaway(0);
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.error_string = &error_string;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), Tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv_->Expect(Tag(102), true);
  cqv_->Verify();
  // Verify status and metadata
  EXPECT_EQ(status, GRPC_STATUS_UNAVAILABLE);
  ASSERT_TRUE(TrailingMetadataRecordingFilter::trailing_metadata_available());
  ASSERT_TRUE(
      TrailingMetadataRecordingFilter::stream_network_state().has_value());
  EXPECT_EQ(TrailingMetadataRecordingFilter::stream_network_state().value(),
            GrpcStreamNetworkState::kNotSeenByServer);
  grpc_slice_unref(details);
  gpr_free(const_cast<char*>(error_string));
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_call_unref(c);
  ExecCtx::Get()->Flush();
}

// Client's HTTP2 transport starts a new stream, sends the request on the wire,
// notices that the transport is destroyed. The test verifies that the HTTP2
// transport does not add GrpcNetworkStreamState metadata since we don't know
// whether the server saw the request or not.
TEST_F(StreamsNotSeenTest, TransportDestroyed) {
  grpc_call* c =
      grpc_channel_create_call(channel_, nullptr, GRPC_PROPAGATE_DEFAULTS, cq_,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               grpc_timeout_seconds_to_deadline(1), nullptr);
  GPR_ASSERT(c);
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_op* op;
  grpc_op ops[6];
  grpc_status_code status;
  const char* error_string;
  grpc_call_error error;
  grpc_slice details;
  // Send the request
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
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), Tag(101),
                                nullptr);
  cqv_->Expect(Tag(101), true);
  cqv_->Verify();
  // Shutdown the server endpoint
  grpc_endpoint_shutdown(tcp_, GRPC_ERROR_CREATE("Server shutdown"));
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.error_string = &error_string;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), Tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv_->Expect(Tag(102), true);
  cqv_->Verify();
  // Verify status and metadata
  EXPECT_EQ(status, GRPC_STATUS_UNAVAILABLE);
  EXPECT_FALSE(
      TrailingMetadataRecordingFilter::stream_network_state().has_value());
  grpc_slice_unref(details);
  gpr_free(const_cast<char*>(error_string));
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_call_unref(c);
  ExecCtx::Get()->Flush();
}

// Client's HTTP2 transport tries to send an RPC after having received a GOAWAY
// frame. The test verifies that the HTTP2 transport adds GrpcNetworkStreamState
// with a value of kNotSentOnWire to the trailing metadata.
TEST_F(StreamsNotSeenTest, StartStreamAfterGoaway) {
  // Send Goaway from the server
  SendGoaway(0);
  // Send a ping to make sure that the goaway was received.
  SendPing();
  // Try sending an RPC
  grpc_call* c =
      grpc_channel_create_call(channel_, nullptr, GRPC_PROPAGATE_DEFAULTS, cq_,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               grpc_timeout_seconds_to_deadline(1), nullptr);
  GPR_ASSERT(c);
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_op* op;
  grpc_op ops[6];
  grpc_status_code status;
  const char* error_string;
  grpc_call_error error;
  grpc_slice details;
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
  op->data.recv_status_on_client.error_string = &error_string;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), Tag(101),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv_->Expect(Tag(101), true);
  cqv_->Verify();
  // Verify status and metadata
  EXPECT_EQ(status, GRPC_STATUS_UNAVAILABLE);
  ASSERT_TRUE(TrailingMetadataRecordingFilter::trailing_metadata_available());
  ASSERT_TRUE(
      TrailingMetadataRecordingFilter::stream_network_state().has_value());
  EXPECT_EQ(TrailingMetadataRecordingFilter::stream_network_state().value(),
            GrpcStreamNetworkState::kNotSentOnWire);
  grpc_slice_unref(details);
  gpr_free(const_cast<char*>(error_string));
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_call_unref(c);
  ExecCtx::Get()->Flush();
}

// These tests have the server sending a SETTINGS_FRAME with a max concurrent
// streams settings of 0 which denies the client the chance to start a stream.
// Note that in the future, these tests might become outdated if the
// client_channel learns about the max concurrent streams setting.
class ZeroConcurrencyTest : public StreamsNotSeenTest {
 protected:
  ZeroConcurrencyTest() : StreamsNotSeenTest(/*server_allows_streams=*/false) {}
};

// Client's HTTP2 transport receives a RPC request, but it cannot start the RPC
// because of the max concurrent streams setting. A goaway frame is then
// received which should result in the RPC getting cancelled with
// kNotSentOnWire.
TEST_F(ZeroConcurrencyTest, StartStreamBeforeGoaway) {
  grpc_call* c =
      grpc_channel_create_call(channel_, nullptr, GRPC_PROPAGATE_DEFAULTS, cq_,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               grpc_timeout_seconds_to_deadline(5), nullptr);
  GPR_ASSERT(c);
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_op* op;
  grpc_op ops[6];
  grpc_status_code status;
  const char* error_string;
  grpc_call_error error;
  grpc_slice details;
  // Send the request
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
  op->data.recv_status_on_client.error_string = &error_string;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), Tag(101),
                                nullptr);
  // This test assumes that nothing would pause the RPC before its received by
  // the transport. If that no longer holds true, we might need to drive the cq
  // for some time to make sure that the RPC reaches the HTTP2 layer.
  SendGoaway(0);
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv_->Expect(Tag(101), true);
  cqv_->Verify();
  // Verify status and metadata
  EXPECT_EQ(status, GRPC_STATUS_UNAVAILABLE);
  ASSERT_TRUE(TrailingMetadataRecordingFilter::trailing_metadata_available());
  ASSERT_TRUE(
      TrailingMetadataRecordingFilter::stream_network_state().has_value());
  EXPECT_EQ(TrailingMetadataRecordingFilter::stream_network_state().value(),
            GrpcStreamNetworkState::kNotSentOnWire);
  grpc_slice_unref(details);
  gpr_free(const_cast<char*>(error_string));
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_call_unref(c);
  ExecCtx::Get()->Flush();
}

// Client's HTTP2 transport receives a RPC request, but it cannot start the RPC
// because of the max concurrent streams setting. Server then shuts its endpoint
// which should result in the RPC getting cancelled with kNotSentOnWire.
TEST_F(ZeroConcurrencyTest, TransportDestroyed) {
  grpc_call* c =
      grpc_channel_create_call(channel_, nullptr, GRPC_PROPAGATE_DEFAULTS, cq_,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               grpc_timeout_seconds_to_deadline(5), nullptr);
  GPR_ASSERT(c);
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_op* op;
  grpc_op ops[6];
  grpc_status_code status;
  const char* error_string;
  grpc_call_error error;
  grpc_slice details;
  // Send the request
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
  op->data.recv_status_on_client.error_string = &error_string;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), Tag(101),
                                nullptr);
  grpc_endpoint_shutdown(tcp_, GRPC_ERROR_CREATE("Server shutdown"));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv_->Expect(Tag(101), true);
  cqv_->Verify();
  // Verify status and metadata
  EXPECT_EQ(status, GRPC_STATUS_UNAVAILABLE);
  ASSERT_TRUE(TrailingMetadataRecordingFilter::trailing_metadata_available());
  ASSERT_TRUE(
      TrailingMetadataRecordingFilter::stream_network_state().has_value());
  EXPECT_EQ(TrailingMetadataRecordingFilter::stream_network_state().value(),
            GrpcStreamNetworkState::kNotSentOnWire);
  grpc_slice_unref(details);
  gpr_free(const_cast<char*>(error_string));
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_call_unref(c);
  ExecCtx::Get()->Flush();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  int result;
  grpc_core::CoreConfiguration::RunWithSpecialConfiguration(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        grpc_core::BuildCoreConfiguration(builder);
        auto register_stage = [builder](grpc_channel_stack_type type,
                                        const grpc_channel_filter* filter) {
          builder->channel_init()->RegisterStage(
              type, INT_MAX, [filter](grpc_core::ChannelStackBuilder* builder) {
                // Want to add the filter as close to the end as possible, to
                // make sure that all of the filters work well together.
                // However, we can't add it at the very end, because the
                // connected channel filter must be the last one.  So we add it
                // right before the last one.
                auto it = builder->mutable_stack()->end();
                --it;
                builder->mutable_stack()->insert(it, filter);
                return true;
              });
        };
        register_stage(
            GRPC_CLIENT_SUBCHANNEL,
            &grpc_core::TrailingMetadataRecordingFilter::kFilterVtable);
      },
      [&] {
        grpc_core::
            TestOnlyGlobalHttp2TransportDisableTransientFailureStateNotification(
                true);
        grpc_init();
        {
          grpc_core::ExecCtx exec_ctx;
          result = RUN_ALL_TESTS();
        }
        grpc_shutdown();
      });
  return result;
}
