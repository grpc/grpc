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

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "src/core/call/metadata_batch.h"
#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/frame_goaway.h"
#include "src/core/ext/transport/chttp2/transport/frame_ping.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/internal_channel_arg_names.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/transport/call_final_info.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/server/server.h"
#include "src/core/util/crash.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/notification.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/sync.h"
#include "src/core/util/useful.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc_core {
namespace {

void* Tag(intptr_t t) { return reinterpret_cast<void*>(t); }

class GracefulShutdownTest : public ::testing::Test {
 protected:
  GracefulShutdownTest() { SetupAndStart(); }

  ~GracefulShutdownTest() override { ShutdownAndDestroy(); }

  // Sets up the client and server
  void SetupAndStart() {
    ExecCtx exec_ctx;
    cq_ = grpc_completion_queue_create_for_next(nullptr);
    cqv_ = std::make_unique<CqVerifier>(cq_);
    grpc_arg server_args[] = {
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_HTTP2_BDP_PROBE), 0),
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_KEEPALIVE_TIME_MS), INT_MAX),
        grpc_channel_arg_integer_create(
            const_cast<char*>(GRPC_ARG_PING_TIMEOUT_MS), 2000)};
    grpc_channel_args server_channel_args = {GPR_ARRAY_SIZE(server_args),
                                             server_args};
    // Create server
    server_ = grpc_server_create(&server_channel_args, nullptr);
    auto* core_server = Server::FromC(server_);
    grpc_server_register_completion_queue(server_, cq_, nullptr);
    grpc_server_start(server_);
    fds_ = grpc_iomgr_create_endpoint_pair("fixture", nullptr);
    auto* transport = grpc_create_chttp2_transport(
        core_server->channel_args(), OrphanablePtr<grpc_endpoint>(fds_.server),
        false);
    grpc_endpoint_add_to_pollset(fds_.server, grpc_cq_pollset(cq_));
    GRPC_CHECK(core_server->SetupTransport(transport, nullptr,
                                           core_server->channel_args()) ==
               absl::OkStatus());
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr,
                                        nullptr);
    // Start polling on the client
    Notification client_poller_thread_started_notification;
    client_poll_thread_ = std::make_unique<std::thread>(
        [this, &client_poller_thread_started_notification]() {
          grpc_completion_queue* client_cq =
              grpc_completion_queue_create_for_next(nullptr);
          {
            ExecCtx exec_ctx;
            grpc_endpoint_add_to_pollset(fds_.client,
                                         grpc_cq_pollset(client_cq));
            grpc_endpoint_add_to_pollset(fds_.server,
                                         grpc_cq_pollset(client_cq));
          }
          client_poller_thread_started_notification.Notify();
          while (!shutdown_) {
            GRPC_CHECK(grpc_completion_queue_next(
                           client_cq, grpc_timeout_milliseconds_to_deadline(10),
                           nullptr)
                           .type == GRPC_QUEUE_TIMEOUT);
          }
          grpc_completion_queue_destroy(client_cq);
        });
    client_poller_thread_started_notification.WaitForNotification();
    // Write connection prefix and settings frame
    constexpr char kPrefix[] =
        "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n\x00\x00\x00\x04\x00\x00\x00\x00\x00";
    Write(absl::string_view(kPrefix, sizeof(kPrefix) - 1));
    // Start reading on the client
    grpc_slice_buffer_init(&read_buffer_);
    GRPC_CLOSURE_INIT(&on_read_done_, OnReadDone, this, nullptr);
    GRPC_CLOSURE_INIT(&on_read_done_scheduler_, OnReadDoneScheduler, this,
                      nullptr);
    grpc_endpoint_read(fds_.client, &read_buffer_, &on_read_done_, false,
                       /*min_progress_size=*/1);
  }

  // Shuts down and destroys the client and server.
  void ShutdownAndDestroy() {
    shutdown_ = true;
    ExecCtx exec_ctx;
    {
      MutexLock lock(ep_destroy_mu_);
      grpc_endpoint_destroy(fds_.client);
      fds_.client = nullptr;
    }
    ExecCtx::Get()->Flush();
    client_poll_thread_->join();
    GRPC_CHECK(read_end_notification_.WaitForNotificationWithTimeout(
        absl::Seconds(5)));
    // Shutdown and destroy server
    grpc_server_shutdown_and_notify(server_, cq_, Tag(1000));
    cqv_->Expect(Tag(1000), true);
    cqv_->Verify();
    grpc_server_destroy(server_);
    cqv_.reset();
    grpc_completion_queue_destroy(cq_);
  }

  static void OnReadDone(void* arg, grpc_error_handle error) {
    GracefulShutdownTest* self = static_cast<GracefulShutdownTest*>(arg);
    if (error.ok()) {
      {
        MutexLock lock(self->mu_);
        for (size_t i = 0; i < self->read_buffer_.count; ++i) {
          char* dump = grpc_dump_slice(self->read_buffer_.slices[i],
                                       GPR_DUMP_HEX | GPR_DUMP_ASCII);
          LOG(INFO) << "Read: " << dump;
          gpr_free(dump);
          absl::StrAppend(&self->read_bytes_,
                          StringViewFromSlice(self->read_buffer_.slices[i]));
        }
        self->read_cv_.SignalAll();
      }
      MutexLock lock(self->ep_destroy_mu_);
      if (self->fds_.client != nullptr) {
        grpc_slice_buffer_reset_and_unref(&self->read_buffer_);
        grpc_endpoint_read(self->fds_.client, &self->read_buffer_,
                           &self->on_read_done_scheduler_, false,
                           /*min_progress_size=*/1);
        return;
      }
    }
    grpc_slice_buffer_destroy(&self->read_buffer_);
    self->read_end_notification_.Notify();
  }

  // Do async hop for OnReadDone() in case grpc_endpoint_read() invokes
  // us synchronously while we're holding the lock.
  static void OnReadDoneScheduler(void* arg, grpc_error_handle error) {
    GracefulShutdownTest* self = static_cast<GracefulShutdownTest*>(arg);
    ExecCtx::Run(DEBUG_LOCATION, &self->on_read_done_, std::move(error));
  }

  // Waits for \a bytes to show up in read_bytes_
  void WaitForReadBytes(absl::string_view bytes) {
    auto start_time = absl::Now();
    MutexLock lock(mu_);
    while (true) {
      auto where = read_bytes_.find(std::string(bytes));
      if (where != std::string::npos) {
        read_bytes_ = read_bytes_.substr(where + bytes.size());
        break;
      }
      ASSERT_LT(absl::Now() - start_time, absl::Seconds(60))
          << "Timeout reading bytes";
      read_cv_.WaitWithTimeout(&mu_, absl::Seconds(5));
    }
  }

  std::string WaitForNBytes(size_t bytes) {
    auto start_time = absl::Now();
    MutexLock lock(mu_);
    while (read_bytes_.size() < bytes) {
      EXPECT_LT(absl::Now() - start_time, absl::Seconds(60));
      read_cv_.WaitWithTimeout(&mu_, absl::Seconds(5));
    }
    std::string result = read_bytes_.substr(0, bytes);
    read_bytes_ = read_bytes_.substr(bytes);
    return result;
  }

  void WaitForClose() {
    ASSERT_TRUE(read_end_notification_.WaitForNotificationWithTimeout(
        absl::Minutes(1)));
  }

  void WaitForGoaway(uint32_t last_stream_id, uint32_t error_code,
                     grpc_slice message) {
    grpc_slice_buffer buffer;
    grpc_slice_buffer_init(&buffer);
    grpc_chttp2_goaway_append(last_stream_id, error_code, message, &buffer,
                              &http2_ztrace_collector_);
    std::string expected_bytes;
    for (size_t i = 0; i < buffer.count; ++i) {
      absl::StrAppend(&expected_bytes, StringViewFromSlice(buffer.slices[i]));
    }
    grpc_slice_buffer_destroy(&buffer);
    WaitForReadBytes(expected_bytes);
  }

  uint64_t WaitForPing() {
    grpc_slice ping_slice = grpc_chttp2_ping_create(0, 0);
    auto whole_ping = StringViewFromSlice(ping_slice);
    GRPC_CHECK(whole_ping.size() == 9 + 8);
    WaitForReadBytes(whole_ping.substr(0, 9));
    std::string ping = WaitForNBytes(8);
    return (static_cast<uint64_t>(static_cast<uint8_t>(ping[0])) << 56) |
           (static_cast<uint64_t>(static_cast<uint8_t>(ping[1])) << 48) |
           (static_cast<uint64_t>(static_cast<uint8_t>(ping[2])) << 40) |
           (static_cast<uint64_t>(static_cast<uint8_t>(ping[3])) << 32) |
           (static_cast<uint64_t>(static_cast<uint8_t>(ping[4])) << 24) |
           (static_cast<uint64_t>(static_cast<uint8_t>(ping[5])) << 16) |
           (static_cast<uint64_t>(static_cast<uint8_t>(ping[6])) << 8) |
           (static_cast<uint64_t>(static_cast<uint8_t>(ping[7])));
  }

  void SendPingAck(uint64_t opaque_data) {
    grpc_slice ping_slice = grpc_chttp2_ping_create(1, opaque_data);
    Write(StringViewFromSlice(ping_slice));
  }

  // This is a blocking call. It waits for the write callback to be invoked
  // before returning. (In other words, do not call this from a thread that
  // should not be blocked, for example, a polling thread.)
  void Write(absl::string_view bytes) {
    ExecCtx exec_ctx;
    grpc_slice slice =
        StaticSlice::FromStaticBuffer(bytes.data(), bytes.size()).TakeCSlice();
    grpc_slice_buffer buffer;
    grpc_slice_buffer_init(&buffer);
    grpc_slice_buffer_add(&buffer, slice);
    WriteBuffer(&buffer);
    grpc_slice_buffer_destroy(&buffer);
  }

  void WriteBuffer(grpc_slice_buffer* buffer) {
    Notification on_write_done_notification_;
    GRPC_CLOSURE_INIT(&on_write_done_, OnWriteDone,
                      &on_write_done_notification_, nullptr);
    grpc_endpoint_write(
        fds_.client, buffer, &on_write_done_,
        grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs());
    ExecCtx::Get()->Flush();
    GRPC_CHECK(on_write_done_notification_.WaitForNotificationWithTimeout(
        absl::Seconds(5)));
  }

  static void OnWriteDone(void* arg, grpc_error_handle error) {
    if (!error.ok()) {
      Crash(absl::StrCat("Write failed: ", error.ToString()));
    }
    Notification* on_write_done_notification_ = static_cast<Notification*>(arg);
    on_write_done_notification_->Notify();
  }

  // Held when destroying fds_.client so we know not to start another read.
  Mutex ep_destroy_mu_;

  grpc_endpoint_pair fds_;
  grpc_server* server_ = nullptr;
  grpc_completion_queue* cq_ = nullptr;
  std::unique_ptr<CqVerifier> cqv_;
  std::unique_ptr<std::thread> client_poll_thread_;
  std::atomic<bool> shutdown_{false};
  grpc_closure on_read_done_;
  grpc_closure on_read_done_scheduler_;
  Mutex mu_;
  CondVar read_cv_;
  Notification read_end_notification_;
  grpc_slice_buffer read_buffer_;
  std::string read_bytes_ ABSL_GUARDED_BY(mu_);
  grpc_closure on_write_done_;
  Http2ZTraceCollector http2_ztrace_collector_;
};

TEST_F(GracefulShutdownTest, GracefulGoaway) {
  // Initiate shutdown on the server
  grpc_server_shutdown_and_notify(server_, cq_, Tag(1));
  // Wait for first goaway
  WaitForGoaway((1u << 31) - 1, /*error_code=*/0,
                /*message=*/
                Slice::FromStaticString("Server shutdown").TakeCSlice());
  // Wait for the ping
  uint64_t ping_id = WaitForPing();
  // Reply to the ping
  SendPingAck(ping_id);
  // Wait for final goaway
  WaitForGoaway(0, /*error_code=*/0,
                /*message=*/
                Slice::FromStaticString("Server shutdown").TakeCSlice());
  // The shutdown should successfully complete.
  cqv_->Expect(Tag(1), true);
  cqv_->Verify();
}

TEST_F(GracefulShutdownTest, RequestStartedBeforeFinalGoaway) {
  grpc_call_error error;
  grpc_call* s;
  grpc_call_details call_details;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details_init(&call_details);
  grpc_metadata_array_init(&request_metadata_recv);
  error = grpc_server_request_call(server_, &s, &call_details,
                                   &request_metadata_recv, cq_, cq_, Tag(100));
  GRPC_CHECK_EQ(error, GRPC_CALL_OK);
  // Initiate shutdown on the server
  grpc_server_shutdown_and_notify(server_, cq_, Tag(1));
  // Wait for first goaway
  WaitForGoaway((1u << 31) - 1, /*error_code=*/0,
                /*message=*/
                Slice::FromStaticString("Server shutdown").TakeCSlice());
  // Wait for the ping
  uint64_t ping_id = WaitForPing();
  // Start a request
  constexpr char kRequestFrame[] =
      "\x00\x00\xbe\x01\x05\x00\x00\x00\x01"
      "\x10\x05:path\x08/foo/bar"
      "\x10\x07:scheme\x04http"
      "\x10\x07:method\x04POST"
      "\x10\x0a:authority\x09localhost"
      "\x10\x0c"
      "content-type\x10"
      "application/grpc"
      "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"
      "\x10\x02te\x08trailers"
      "\x10\x0auser-agent\x17grpc-c/0.12.0.0 (linux)";
  Write(absl::string_view(kRequestFrame, sizeof(kRequestFrame) - 1));
  // Reply to the ping
  SendPingAck(ping_id);
  // Wait for final goaway with last stream ID 1 to show that the HTTP2
  // transport accepted the stream.
  WaitForGoaway(1, /*error_code=*/0,
                /*message=*/
                Slice::FromStaticString("Server shutdown").TakeCSlice());
  // TODO(yashykt): The surface layer automatically cancels calls received after
  // shutdown has been called. Once that is fixed, this should be a success.
  cqv_->Expect(Tag(100), false);
  // The shutdown should successfully complete.
  cqv_->Expect(Tag(1), true);
  cqv_->Verify();
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
}

TEST_F(GracefulShutdownTest, RequestStartedAfterFinalGoawayIsIgnored) {
  // Start a request before shutdown to make sure that the connection stays
  // alive.
  grpc_call_error error;
  grpc_call* s;
  grpc_call_details call_details;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details_init(&call_details);
  grpc_metadata_array_init(&request_metadata_recv);
  error = grpc_server_request_call(server_, &s, &call_details,
                                   &request_metadata_recv, cq_, cq_, Tag(100));
  GRPC_CHECK_EQ(error, GRPC_CALL_OK);
  // Send the request from the client.
  constexpr char kRequestFrame[] =
      "\x00\x00\xbe\x01\x05\x00\x00\x00\x01"
      "\x10\x05:path\x08/foo/bar"
      "\x10\x07:scheme\x04http"
      "\x10\x07:method\x04POST"
      "\x10\x0a:authority\x09localhost"
      "\x10\x0c"
      "content-type\x10"
      "application/grpc"
      "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"
      "\x10\x02te\x08trailers"
      "\x10\x0auser-agent\x17grpc-c/0.12.0.0 (linux)";
  Write(absl::string_view(kRequestFrame, sizeof(kRequestFrame) - 1));
  cqv_->Expect(Tag(100), true);
  cqv_->Verify();

  // Initiate shutdown on the server
  grpc_server_shutdown_and_notify(server_, cq_, Tag(1));
  // Wait for first goaway
  WaitForGoaway((1u << 31) - 1, /*error_code=*/0,
                /*message=*/
                Slice::FromStaticString("Server shutdown").TakeCSlice());
  // Wait for the ping
  uint64_t ping_id = WaitForPing();
  // Reply to the ping
  SendPingAck(ping_id);
  // Wait for final goaway
  WaitForGoaway(1, /*error_code=*/0,
                /*message=*/
                Slice::FromStaticString("Server shutdown").TakeCSlice());

  // Send another request from the client which should be ignored.
  constexpr char kNewRequestFrame[] =
      "\x00\x00\xbe\x01\x05\x00\x00\x00\x03"
      "\x10\x05:path\x08/foo/bar"
      "\x10\x07:scheme\x04http"
      "\x10\x07:method\x04POST"
      "\x10\x0a:authority\x09localhost"
      "\x10\x0c"
      "content-type\x10"
      "application/grpc"
      "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"
      "\x10\x02te\x08trailers"
      "\x10\x0auser-agent\x17grpc-c/0.12.0.0 (linux)";
  Write(absl::string_view(kNewRequestFrame, sizeof(kNewRequestFrame) - 1));

  // Finish the accepted request.
  grpc_op ops[3];
  grpc_op* op;
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  int was_cancelled = 2;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), Tag(101),
                                nullptr);
  GRPC_CHECK_EQ(error, GRPC_CALL_OK);
  cqv_->Expect(Tag(101), true);
  // The shutdown should successfully complete.
  cqv_->Expect(Tag(1), true);
  cqv_->Verify();
  grpc_call_unref(s);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
}

// Make sure that the graceful goaway eventually makes progress even if a client
// does not respond to the ping.
TEST_F(GracefulShutdownTest, UnresponsiveClient) {
  absl::Time initial_time = absl::Now();
  // Initiate shutdown on the server
  grpc_server_shutdown_and_notify(server_, cq_, Tag(1));
  // Wait for first goaway
  WaitForGoaway((1u << 31) - 1, /*error_code=*/0,
                /*message=*/
                Slice::FromStaticString("Server shutdown").TakeCSlice());
  // Wait for the ping
  std::ignore = WaitForPing();
  // Wait for final goaway without sending a ping ACK.
  WaitForClose();
  EXPECT_GE(absl::Now() - initial_time,
            absl::Seconds(2) -
                absl::Seconds(
                    1) /* clock skew between threads due to time caching */);
  // The shutdown should successfully complete.
  cqv_->Expect(Tag(1), true);
  cqv_->Verify();
}

// Test that servers send a GOAWAY with the last stream ID even when the
// transport is disconnected without letting Graceful GOAWAY complete
// successfully.
TEST_F(GracefulShutdownTest, GoawayReceivedOnServerDisconnect) {
  // Initiate shutdown on the server and immediately disconnect.
  grpc_server_shutdown_and_notify(server_, cq_, Tag(1));
  grpc_server_cancel_all_calls(server_);
  // Wait for final goaway.
  WaitForGoaway(/*last_stream_id=*/0, /*error_code=*/2,
                grpc_slice_from_static_string("Cancelling all calls"));
  // The shutdown should successfully complete.
  cqv_->Expect(Tag(1), true);
  cqv_->Verify();
}

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::URIToResolvedAddress;

constexpr absl::Duration kCloseTestTimeout = absl::Seconds(10);

constexpr absl::string_view kClientPreface("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n",
                                           24);
constexpr absl::string_view kEmptySettingsFrame(
    "\x00\x00\x00\x04\x00\x00\x00\x00\x00", 9);
// SETTINGS frame advertising MAX_CONCURRENT_STREAMS (0x0003) = 1.
constexpr absl::string_view kSettingsMaxConcurrentStreams1(
    "\x00\x00\x06\x04\x00\x00\x00\x00\x00"
    "\x00\x03\x00\x00\x00\x01",
    15);

std::string MakeOpenClientHeadersFrame(uint32_t stream_id) {
  // Same literal HPACK request headers as GracefulShutdownTest, but with
  // flags = 0x04 (END_HEADERS only, no END_STREAM) so the stream stays open.
  constexpr absl::string_view kHeaderBlock(
      "\x10\x05:path\x08/foo/bar"
      "\x10\x07:scheme\x04http"
      "\x10\x07:method\x04POST"
      "\x10\x0a:authority\x09localhost"
      "\x10\x0c"
      "content-type\x10"
      "application/grpc"
      "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"
      "\x10\x02te\x08trailers"
      "\x10\x0auser-agent\x17grpc-c/0.12.0.0 (linux)",
      190);
  std::string frame;
  frame.reserve(9 + kHeaderBlock.size());
  frame.push_back(static_cast<char>((kHeaderBlock.size() >> 16) & 0xff));
  frame.push_back(static_cast<char>((kHeaderBlock.size() >> 8) & 0xff));
  frame.push_back(static_cast<char>(kHeaderBlock.size() & 0xff));
  frame.push_back(0x01);  // HEADERS
  frame.push_back(0x04);  // END_HEADERS
  frame.push_back(static_cast<char>((stream_id >> 24) & 0x7f));
  frame.push_back(static_cast<char>((stream_id >> 16) & 0xff));
  frame.push_back(static_cast<char>((stream_id >> 8) & 0xff));
  frame.push_back(static_cast<char>(stream_id & 0xff));
  frame.append(kHeaderBlock.data(), kHeaderBlock.size());
  return frame;
}

std::string MakePingAckFrame(absl::string_view opaque_data) {
  GRPC_CHECK_EQ(opaque_data.size(), 8u);
  std::string frame("\x00\x00\x08\x06\x01\x00\x00\x00\x00", 9);
  frame.append(opaque_data.data(), opaque_data.size());
  return frame;
}

struct ParsedHttp2Frame {
  uint8_t type = 0;
  uint8_t flags = 0;
  uint32_t stream_id = 0;
  std::string payload;
};

// Scans HTTP/2 frames starting at `offset` in `bytes`. Returns true and
// advances `offset` when a complete frame matching `wanted_type` (and optional
// `wanted_flags` if >= 0) is found.
bool FindNextFrame(absl::string_view bytes, size_t& offset, uint8_t wanted_type,
                   int wanted_flags, ParsedHttp2Frame* out) {
  size_t pos = offset;
  while (pos + 9 <= bytes.size()) {
    uint32_t len = (static_cast<uint8_t>(bytes[pos]) << 16) |
                   (static_cast<uint8_t>(bytes[pos + 1]) << 8) |
                   static_cast<uint8_t>(bytes[pos + 2]);
    if (pos + 9 + len > bytes.size()) {
      return false;
    }
    uint8_t type = static_cast<uint8_t>(bytes[pos + 3]);
    uint8_t flags = static_cast<uint8_t>(bytes[pos + 4]);
    uint32_t stream_id = ((static_cast<uint8_t>(bytes[pos + 5]) & 0x7f) << 24) |
                         (static_cast<uint8_t>(bytes[pos + 6]) << 16) |
                         (static_cast<uint8_t>(bytes[pos + 7]) << 8) |
                         static_cast<uint8_t>(bytes[pos + 8]);
    size_t next_pos = pos + 9 + len;
    if (type == wanted_type && (wanted_flags < 0 || flags == wanted_flags)) {
      if (out != nullptr) {
        out->type = type;
        out->flags = flags;
        out->stream_id = stream_id;
        out->payload.assign(bytes.data() + pos + 9, len);
      }
      offset = next_pos;
      return true;
    }
    pos = next_pos;
  }
  return false;
}

size_t CountOccurrences(absl::string_view haystack, absl::string_view needle) {
  size_t count = 0;
  size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != absl::string_view::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

void RunOnCombiner(grpc_chttp2_transport* t, absl::AnyInvocable<void()> fn) {
  absl::Notification done;
  {
    ExecCtx exec_ctx;
    t->combiner->Run(NewClosure([fn = std::move(fn),
                                 &done](grpc_error_handle) mutable {
                       fn();
                       done.Notify();
                     }),
                     absl::OkStatus());
  }
  GRPC_CHECK(done.WaitForNotificationWithTimeout(kCloseTestTimeout));
}

// Fake endpoint state allowing tests to pause writes in flight
// deterministically and inject incoming reads on demand.
class ControllableEndpointState {
 public:
  // Reads from the endpoint. Returns false if the read should be paused.
  // If a read is paused, the `on_read` callback will be called when the read
  // is resumed.
  bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
            grpc_event_engine::experimental::SliceBuffer* buffer) {
    MutexLock lock(mu_);
    on_read_ = std::move(on_read);
    read_buffer_ = buffer;
    cv_.SignalAll();
    return false;
  }

  // Writes the given data to the endpoint and returns false if the write
  // should be paused.
  // If a write is paused, the `on_writable` callback will be called when the
  // write is resumed.
  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             grpc_event_engine::experimental::SliceBuffer* data) {
    MutexLock lock(mu_);
    for (size_t i = 0; i < data->Count(); ++i) {
      written_.append((*data)[i].as_string_view());
    }
    data->Clear();
    if (pause_writes_) {
      GRPC_CHECK(on_writable_ == nullptr);
      on_writable_ = std::move(on_writable);
      ++paused_write_count_;
      cv_.SignalAll();
      return false;
    }
    cv_.SignalAll();
    return true;
  }

  void SetPauseWrites(bool pause) {
    MutexLock lock(mu_);
    pause_writes_ = pause;
  }

  // Returns true if there is a currently paused write.
  bool WaitForPausedWrite(absl::Duration timeout = kCloseTestTimeout) {
    MutexLock lock(mu_);
    const absl::Time deadline = absl::Now() + timeout;
    while (on_writable_ == nullptr) {
      if (cv_.WaitWithDeadline(&mu_, deadline)) break;
    }
    return on_writable_ != nullptr;
  }

  // Completes the currently paused write with the given status.
  // Does nothing if there is no currently paused write.
  void CompleteWrite(absl::Status status = absl::OkStatus()) {
    absl::AnyInvocable<void(absl::Status)> on_writable;
    {
      MutexLock lock(mu_);
      const absl::Time deadline = absl::Now() + kCloseTestTimeout;
      while (on_writable_ == nullptr) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) break;
      }
      ASSERT_NE(on_writable_, nullptr) << "No paused write to complete";
      on_writable = std::move(on_writable_);
      on_writable_ = nullptr;
      cv_.SignalAll();
    }
    GetDefaultEventEngine()->Run(
        [on_writable = std::move(on_writable),
         status = std::move(status)]() mutable {
          on_writable(std::move(status));
        });
  }

  // Disables write pausing and releases any currently paused write.
  void ResumeAllWrites() {
    absl::AnyInvocable<void(absl::Status)> on_writable;
    {
      MutexLock lock(mu_);
      pause_writes_ = false;
      on_writable = std::move(on_writable_);
      on_writable_ = nullptr;
      cv_.SignalAll();
    }
    if (on_writable != nullptr) {
      GetDefaultEventEngine()->Run(
          [on_writable = std::move(on_writable)]() mutable {
            on_writable(absl::OkStatus());
          });
    }
  }

  int paused_write_count() {
    MutexLock lock(mu_);
    return paused_write_count_;
  }

  // Delivers a read to the endpoint.
  // The read will be delivered to the `on_read` callback provided to `Read`.
  // Does nothing if there is no currently paused read.
  void DeliverRead(absl::string_view bytes) {
    absl::AnyInvocable<void(absl::Status)> on_read;
    {
      MutexLock lock(mu_);
      const absl::Time deadline = absl::Now() + kCloseTestTimeout;
      while (on_read_ == nullptr) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) break;
      }
      ASSERT_NE(on_read_, nullptr) << "Transport never issued a read";
      read_buffer_->Append(
          grpc_event_engine::experimental::Slice::FromCopiedString(bytes));
      on_read = std::move(on_read_);
      on_read_ = nullptr;
      read_buffer_ = nullptr;
    }
    on_read(absl::OkStatus());
  }

  // Delivers a read error to the endpoint.
  // The read error will be delivered to the `on_read` callback provided to
  // `Read`.
  // Does nothing if there is no currently paused read.
  void DeliverReadError(absl::Status status) {
    absl::AnyInvocable<void(absl::Status)> on_read;
    {
      MutexLock lock(mu_);
      const absl::Time deadline = absl::Now() + kCloseTestTimeout;
      while (on_read_ == nullptr) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) break;
      }
      ASSERT_NE(on_read_, nullptr) << "Transport never issued a read";
      on_read = std::move(on_read_);
      on_read_ = nullptr;
      read_buffer_ = nullptr;
    }
    on_read(std::move(status));
  }

  // Waits for the next frame in the written data.
  // Returns true if a frame is found, false if the timeout is reached.
  bool WaitForFrame(size_t& offset, uint8_t wanted_type, int wanted_flags,
                    ParsedHttp2Frame* out,
                    absl::Duration timeout = kCloseTestTimeout) {
    MutexLock lock(mu_);
    const absl::Time deadline = absl::Now() + timeout;
    while (!FindNextFrame(written_, offset, wanted_type, wanted_flags, out)) {
      if (cv_.WaitWithDeadline(&mu_, deadline)) {
        return FindNextFrame(written_, offset, wanted_type, wanted_flags, out);
      }
    }
    return true;
  }

  void OnEndpointDestroyed() {
    absl::AnyInvocable<void(absl::Status)> on_read;
    {
      MutexLock lock(mu_);
      on_read = std::move(on_read_);
      on_read_ = nullptr;
      read_buffer_ = nullptr;
      endpoint_destroyed_ = true;
      cv_.SignalAll();
    }
    if (on_read != nullptr) {
      GetDefaultEventEngine()->Run([on_read = std::move(on_read)]() mutable {
        on_read(absl::CancelledError("endpoint destroyed"));
      });
    }
  }

  bool WaitForEndpointDestroyed(absl::Duration timeout = kCloseTestTimeout) {
    MutexLock lock(mu_);
    const absl::Time deadline = absl::Now() + timeout;
    while (!endpoint_destroyed_) {
      if (cv_.WaitWithDeadline(&mu_, deadline)) break;
    }
    return endpoint_destroyed_;
  }

 private:
  Mutex mu_;
  CondVar cv_;
  bool pause_writes_ ABSL_GUARDED_BY(mu_) = false;
  int paused_write_count_ ABSL_GUARDED_BY(mu_) = 0;
  absl::AnyInvocable<void(absl::Status)> on_writable_ ABSL_GUARDED_BY(mu_);
  absl::AnyInvocable<void(absl::Status)> on_read_ ABSL_GUARDED_BY(mu_);
  grpc_event_engine::experimental::SliceBuffer* read_buffer_
      ABSL_GUARDED_BY(mu_) = nullptr;
  std::string written_ ABSL_GUARDED_BY(mu_);
  bool endpoint_destroyed_ ABSL_GUARDED_BY(mu_) = false;
};

class ControllableEndpoint : public EventEngine::Endpoint {
 public:
  explicit ControllableEndpoint(
      std::shared_ptr<ControllableEndpointState> state)
      : state_(std::move(state)),
        peer_address_(URIToResolvedAddress("ipv4:127.0.0.1:12345").value()),
        local_address_(URIToResolvedAddress("ipv4:127.0.0.1:23456").value()) {}

  ~ControllableEndpoint() override { state_->OnEndpointDestroyed(); }

  bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
            grpc_event_engine::experimental::SliceBuffer* buffer,
            ReadArgs /*args*/) override {
    return state_->Read(std::move(on_read), buffer);
  }

  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             grpc_event_engine::experimental::SliceBuffer* data,
             WriteArgs /*args*/) override {
    return state_->Write(std::move(on_writable), data);
  }

  const EventEngine::ResolvedAddress& GetPeerAddress() const override {
    return peer_address_;
  }
  const EventEngine::ResolvedAddress& GetLocalAddress() const override {
    return local_address_;
  }
  std::shared_ptr<TelemetryInfo> GetTelemetryInfo() const override {
    return nullptr;
  }

 private:
  std::shared_ptr<ControllableEndpointState> state_;
  EventEngine::ResolvedAddress peer_address_;
  EventEngine::ResolvedAddress local_address_;
};

class TestStateWatcher : public Transport::StateWatcher {
 public:
  void OnDisconnect(absl::Status status,
                    DisconnectInfo disconnect_info) override {
    MutexLock lock(mu_);
    disconnect_status_ = std::move(status);
    disconnect_info_ = disconnect_info;
    ++disconnect_count_;
    cv_.SignalAll();
  }

  void OnPeerMaxConcurrentStreamsUpdate(
      uint32_t /*max_concurrent_streams*/,
      std::unique_ptr<MaxConcurrentStreamsUpdateDoneHandle> /*on_done*/)
      override {}

  grpc_pollset_set* interested_parties() const override { return nullptr; }

  bool WaitForDisconnect(absl::Duration timeout = kCloseTestTimeout) {
    MutexLock lock(mu_);
    const absl::Time deadline = absl::Now() + timeout;
    while (disconnect_count_ == 0) {
      if (cv_.WaitWithDeadline(&mu_, deadline)) break;
    }
    return disconnect_count_ > 0;
  }

  int disconnect_count() {
    MutexLock lock(mu_);
    return disconnect_count_;
  }

  DisconnectInfo disconnect_info() {
    MutexLock lock(mu_);
    return disconnect_info_;
  }

 private:
  Mutex mu_;
  CondVar cv_;
  int disconnect_count_ ABSL_GUARDED_BY(mu_) = 0;
  absl::Status disconnect_status_ ABSL_GUARDED_BY(mu_);
  DisconnectInfo disconnect_info_ ABSL_GUARDED_BY(mu_);
};

// Lightweight wrapper around a single grpc_chttp2_stream for direct transport
// unit tests.
class TestStream {
 public:
  TestStream(RefCountedPtr<grpc_chttp2_transport> transport,
             const void* server_data = nullptr)
      : transport_(std::move(transport)),
        arena_(SimpleArenaAllocator()->MakeArena()),
        storage_(arena_->Alloc(transport_->SizeOfStream())) {
    GRPC_STREAM_REF_INIT(&refcount_, 1, OnRefcountZero, this, "test_stream");
    GRPC_CLOSURE_INIT(&on_destroy_done_, OnDestroyDone, this, nullptr);
    GRPC_CLOSURE_INIT(&on_send_complete_, OnSendComplete, this, nullptr);
    GRPC_CLOSURE_INIT(&on_recv_trailing_, OnRecvTrailing, this, nullptr);
    transport_->InitStream(stream(), &refcount_, server_data, arena_.get());
  }

  ~TestStream() {
    if (send_started_) {
      GRPC_CHECK(
          send_complete_.WaitForNotificationWithTimeout(kCloseTestTimeout));
    }
    if (recv_trailing_started_) {
      GRPC_CHECK(
          recv_trailing_.WaitForNotificationWithTimeout(kCloseTestTimeout));
    }
    Release();
    GRPC_CHECK(destroyed_.WaitForNotificationWithTimeout(kCloseTestTimeout));
  }

  void StartClientCall() {
    ExecCtx exec_ctx;
    send_started_ = true;
    recv_trailing_started_ = true;
    send_initial_metadata_.Set(HttpPathMetadata(),
                               Slice::FromStaticString("/foo/bar"));
    send_initial_metadata_.Set(HttpAuthorityMetadata(),
                               Slice::FromStaticString("localhost"));
    send_initial_metadata_.Set(HttpSchemeMetadata(),
                               HttpSchemeMetadata::kHttp);
    send_initial_metadata_.Set(HttpMethodMetadata(),
                               HttpMethodMetadata::kPost);
    send_initial_metadata_.Set(TeMetadata(), TeMetadata::kTrailers);
    payload_.send_initial_metadata.send_initial_metadata =
        &send_initial_metadata_;
    payload_.recv_trailing_metadata.recv_trailing_metadata =
        &recv_trailing_metadata_;
    payload_.recv_trailing_metadata.recv_trailing_metadata_ready =
        &on_recv_trailing_;
    payload_.recv_trailing_metadata.collect_stats = &stats_;
    batch_.payload = &payload_;
    batch_.on_complete = &on_send_complete_;
    batch_.send_initial_metadata = true;
    batch_.recv_trailing_metadata = true;
    transport_->PerformStreamOp(stream(), &batch_);
  }

  void StartServerRecvTrailing() {
    ExecCtx exec_ctx;
    recv_trailing_started_ = true;
    payload_.recv_trailing_metadata.recv_trailing_metadata =
        &recv_trailing_metadata_;
    payload_.recv_trailing_metadata.recv_trailing_metadata_ready =
        &on_recv_trailing_;
    payload_.recv_trailing_metadata.collect_stats = &stats_;
    batch_.payload = &payload_;
    batch_.recv_trailing_metadata = true;
    transport_->PerformStreamOp(stream(), &batch_);
  }

  void Cancel(absl::Status error) {
    ExecCtx exec_ctx;
    cancel_payload_.cancel_stream.cancel_error = std::move(error);
    cancel_batch_.payload = &cancel_payload_;
    cancel_batch_.cancel_stream = true;
    transport_->PerformStreamOp(stream(), &cancel_batch_);
  }

  bool WaitForSendComplete(absl::Duration timeout = kCloseTestTimeout) {
    return send_complete_.WaitForNotificationWithTimeout(timeout);
  }

  bool WaitForRecvTrailing(absl::Duration timeout = kCloseTestTimeout) {
    return recv_trailing_.WaitForNotificationWithTimeout(timeout);
  }

  std::optional<grpc_status_code> TrailingStatus() const {
    return recv_trailing_metadata_.get(GrpcStatusMetadata());
  }

  std::optional<GrpcStreamNetworkState::ValueType> NetworkState() const {
    return recv_trailing_metadata_.get(GrpcStreamNetworkState());
  }

 private:
  grpc_stream* stream() { return reinterpret_cast<grpc_stream*>(storage_); }

  void Release() {
    if (!released_) {
      released_ = true;
      ExecCtx exec_ctx;
#ifndef NDEBUG
      grpc_stream_unref(&refcount_, "test_stream");
#else
      grpc_stream_unref(&refcount_);
#endif
    }
  }

  static void OnRefcountZero(void* arg, grpc_error_handle /*error*/) {
    auto* self = static_cast<TestStream*>(arg);
    self->transport_->DestroyStream(self->stream(), &self->on_destroy_done_);
  }

  static void OnDestroyDone(void* arg, grpc_error_handle /*error*/) {
    static_cast<TestStream*>(arg)->destroyed_.Notify();
  }

  static void OnSendComplete(void* arg, grpc_error_handle /*error*/) {
    static_cast<TestStream*>(arg)->send_complete_.Notify();
  }

  static void OnRecvTrailing(void* arg, grpc_error_handle /*error*/) {
    static_cast<TestStream*>(arg)->recv_trailing_.Notify();
  }

  RefCountedPtr<grpc_chttp2_transport> transport_;
  RefCountedPtr<Arena> arena_;
  void* storage_;
  grpc_stream_refcount refcount_;
  grpc_closure on_destroy_done_;
  grpc_closure on_send_complete_;
  grpc_closure on_recv_trailing_;
  grpc_metadata_batch send_initial_metadata_;
  grpc_metadata_batch recv_trailing_metadata_;
  grpc_transport_stream_stats stats_ = {};
  grpc_transport_stream_op_batch_payload payload_{};
  grpc_transport_stream_op_batch batch_;
  grpc_transport_stream_op_batch_payload cancel_payload_{};
  grpc_transport_stream_op_batch cancel_batch_;
  bool send_started_ = false;
  bool recv_trailing_started_ = false;
  bool released_ = false;
  absl::Notification send_complete_;
  absl::Notification recv_trailing_;
  absl::Notification destroyed_;
};

class Chttp2CloseTransportTest : public ::testing::Test {
 protected:
  void CreateTransport(bool is_client) {
    ExecCtx exec_ctx;
    ChannelArgs args = ChannelArgs()
                           .SetObject(ResourceQuota::Default())
                           .SetObject(GetDefaultEventEngine())
                           .Set(GRPC_ARG_HTTP2_BDP_PROBE, 0)
                           .Set(GRPC_ARG_KEEPALIVE_TIME_MS, INT_MAX);
    grpc_endpoint* ep =
        grpc_event_engine::experimental::grpc_event_engine_endpoint_create(
            std::make_unique<ControllableEndpoint>(state_));
    auto* raw_transport = reinterpret_cast<grpc_chttp2_transport*>(
        grpc_create_chttp2_transport(args, OrphanablePtr<grpc_endpoint>(ep),
                                     is_client));
    transport_ = raw_transport->Ref();
    raw_transport_ = raw_transport;
    if (!is_client) {
      grpc_transport_op* op = grpc_make_transport_op(nullptr);
      op->set_accept_stream = true;
      op->set_accept_stream_fn = AcceptStreamCb;
      op->set_accept_stream_user_data = this;
      raw_transport_->PerformOp(op);
    }
    grpc_chttp2_transport_start_reading(
        raw_transport_, nullptr, nullptr, nullptr,
        [this]() { close_notification_.Notify(); });
  }

  void OrphanTransport() {
    if (raw_transport_ != nullptr) {
      ExecCtx exec_ctx;
      std::exchange(raw_transport_, nullptr)->Orphan();
    }
  }

  absl::Status GetClosedWithError() {
    absl::Status result;
    RunOnCombiner(transport_.get(),
                  [&]() { result = transport_->closed_with_error; });
    return result;
  }

  bool WaitForAcceptedStreams(size_t count) {
    MutexLock lock(accepted_mu_);
    const absl::Time deadline = absl::Now() + kCloseTestTimeout;
    while (accepted_streams_.size() < count) {
      if (accepted_cv_.WaitWithDeadline(&accepted_mu_, deadline)) break;
    }
    return accepted_streams_.size() >= count;
  }

  std::shared_ptr<TestStream> AcceptedStream(size_t idx) {
    MutexLock lock(accepted_mu_);
    return accepted_streams_.at(idx);
  }

  void TearDown() override {
    state_->ResumeAllWrites();
    OrphanTransport();
    if (transport_ != nullptr) {
      EXPECT_TRUE(state_->WaitForEndpointDestroyed());
      EXPECT_TRUE(close_notification_.WaitForNotificationWithTimeout(
          kCloseTestTimeout));
    }
    {
      std::vector<std::shared_ptr<TestStream>> streams;
      {
        MutexLock lock(accepted_mu_);
        streams = std::move(accepted_streams_);
      }
      streams.clear();
    }
    transport_.reset();
  }

  static void AcceptStreamCb(void* user_data, Transport* transport,
                             const void* server_data) {
    auto* self = static_cast<Chttp2CloseTransportTest*>(user_data);
    auto* t = reinterpret_cast<grpc_chttp2_transport*>(transport);
    auto stream = std::make_shared<TestStream>(t->Ref(), server_data);
    {
      MutexLock lock(self->accepted_mu_);
      self->accepted_streams_.push_back(std::move(stream));
      self->accepted_cv_.SignalAll();
    }
  }

  std::shared_ptr<ControllableEndpointState> state_ =
      std::make_shared<ControllableEndpointState>();
  RefCountedPtr<grpc_chttp2_transport> transport_;
  grpc_chttp2_transport* raw_transport_ = nullptr;
  absl::Notification close_notification_;
  Mutex accepted_mu_;
  CondVar accepted_cv_;
  std::vector<std::shared_ptr<TestStream>> accepted_streams_
      ABSL_GUARDED_BY(accepted_mu_);
};

// Verifies that when a client transport initiates closure on ping timeout while
// the final GOAWAY write is still in flight:
// 1. The StateWatcher is notified of disconnection (with reason kGoaway)
//    immediately, before the GOAWAY write completes, so upper layers stop
//    routing new RPCs onto the dying transport.
// 2. Any new client streams started while the GOAWAY write is still pending are
//    rejected immediately with UNAVAILABLE and kNotSentOnWire (never inserted
//    into stream_map or written as HEADERS).
// 3. Once the GOAWAY write finishes, the transport closes cleanly without
//    issuing extra write cycles or triggering recursive GOAWAY close errors.
TEST_F(Chttp2CloseTransportTest,
       ClientRejectsNewStreamsWhileFinalGoawayWritePending) {
  CreateTransport(/*is_client=*/true);
  auto watcher = MakeRefCounted<TestStateWatcher>();
  {
    ExecCtx exec_ctx;
    transport_->StartWatch(watcher);
  }

  // Complete initial SETTINGS exchange.
  size_t read_offset = kClientPreface.size();
  // Wait for the client SETTINGS frame to be written.
  ASSERT_TRUE(state_->WaitForFrame(read_offset, /*SETTINGS=*/0x04,
                                   /*flags=*/0x00, nullptr));
  // Deliver the empty SETTINGS frame (from the server) and wait for the
  // SETTINGS ACK to be written.
  state_->DeliverRead(kEmptySettingsFrame);
  ASSERT_TRUE(state_->WaitForFrame(read_offset, /*SETTINGS=*/0x04,
                                   /*ACK=*/0x01, nullptr));
  // Ensure the SETTINGS ACK write has settled back to IDLE.
  RunOnCombiner(transport_.get(), [&]() {
    EXPECT_EQ(transport_->write_state, GRPC_CHTTP2_WRITE_STATE_IDLE);
  });

  // Pause the next write (GOAWAY) and trigger ping timeout.
  state_->SetPauseWrites(true);
  {
    ExecCtx exec_ctx;
    grpc_chttp2_ping_timeout(transport_->Ref());
  }
  ASSERT_TRUE(state_->WaitForPausedWrite());
  // Ensure the GOAWAY frame is going to be written.
  ASSERT_TRUE(state_->WaitForFrame(read_offset, /*GOAWAY=*/0x07, -1, nullptr));

  // Subchannel/StateWatcher must be notified of disconnect before the GOAWAY
  // write completes so retries stop targeting this transport.
  ASSERT_TRUE(watcher->WaitForDisconnect());
  EXPECT_EQ(watcher->disconnect_info().reason,
            Transport::StateWatcher::kGoaway);

  // Opening new client streams while the GOAWAY write is still in flight must
  // reject them immediately with UNAVAILABLE + kNotSentOnWire.
  TestStream s1(transport_);
  TestStream s2(transport_);
  s1.StartClientCall();
  s2.StartClientCall();
  ASSERT_TRUE(s1.WaitForRecvTrailing());
  ASSERT_TRUE(s2.WaitForRecvTrailing());
  EXPECT_EQ(s1.TrailingStatus(), GRPC_STATUS_UNAVAILABLE);
  EXPECT_EQ(s2.TrailingStatus(), GRPC_STATUS_UNAVAILABLE);
  EXPECT_EQ(s1.NetworkState(), GrpcStreamNetworkState::kNotSentOnWire);
  EXPECT_EQ(s2.NetworkState(), GrpcStreamNetworkState::kNotSentOnWire);

  // Once the GOAWAY write finishes, the transport closes cleanly without
  // writing any new HEADERS or RST_STREAM frames.
  state_->CompleteWrite(absl::OkStatus());
  ASSERT_TRUE(state_->WaitForEndpointDestroyed());
  ASSERT_TRUE(
      close_notification_.WaitForNotificationWithTimeout(kCloseTestTimeout));
  EXPECT_EQ(state_->paused_write_count(), 1);

  std::string closed_err = StatusToString(GetClosedWithError());
  EXPECT_FALSE(
      absl::StrContains(closed_err, "Last stream closed after sending GOAWAY"))
      << closed_err;
}

// Regression test for the O(2^N) error-string doubling loop on client ping
// timeout when concurrent retries keep opening streams on a dying transport:
// 1. Starts two active client streams and lets their HEADERS flush so both have
//    sent_initial_metadata == true.
// 2. Pauses writes and triggers grpc_chttp2_ping_timeout, which schedules a
//    final GOAWAY and defers close_transport_locked while the write is pending.
// 3. Immediately opens two retry streams while the GOAWAY write is in flight.
// 4. Verifies the retry streams are rejected immediately with kNotSentOnWire,
//    no extra write cycles occur when the GOAWAY write finishes, and the final
//    closed_with_error neither contains "Last stream closed after sending
//    GOAWAY" nor duplicates "ping timeout".
TEST_F(Chttp2CloseTransportTest, ClientPingTimeoutRetryLoopDoesNotDoubleError) {
  CreateTransport(/*is_client=*/true);

  size_t read_offset = kClientPreface.size();
  // Complete initial SETTINGS exchange.
  ASSERT_TRUE(state_->WaitForFrame(read_offset, /*SETTINGS=*/0x04,
                                   /*flags=*/0x00, nullptr));

  // Deliver the empty SETTINGS frame (from the server) and wait for the
  // SETTINGS ACK to be written.
  state_->DeliverRead(kEmptySettingsFrame);
  // Wait for the SETTINGS ACK to be written.
  ASSERT_TRUE(state_->WaitForFrame(read_offset, /*SETTINGS=*/0x04,
                                   /*ACK=*/0x01, nullptr));

  // Start two active streams and ensure their HEADERS are written out.
  TestStream active1(transport_);
  TestStream active2(transport_);
  active1.StartClientCall();
  active2.StartClientCall();
  ASSERT_TRUE(state_->WaitForFrame(read_offset, /*HEADERS=*/0x01, -1, nullptr));
  ASSERT_TRUE(state_->WaitForFrame(read_offset, /*HEADERS=*/0x01, -1, nullptr));
  ASSERT_TRUE(active1.WaitForSendComplete());
  ASSERT_TRUE(active2.WaitForSendComplete());

  // Pause writes and fire ping_timeout.
  state_->SetPauseWrites(true);
  {
    ExecCtx exec_ctx;
    grpc_chttp2_ping_timeout(transport_->Ref());
  }
  ASSERT_TRUE(state_->WaitForPausedWrite());
  ASSERT_TRUE(active1.WaitForRecvTrailing());
  ASSERT_TRUE(active2.WaitForRecvTrailing());

  // Simulate concurrent retries opening new streams while GOAWAY is in flight.
  TestStream retry1(transport_);
  TestStream retry2(transport_);
  retry1.StartClientCall();
  retry2.StartClientCall();
  ASSERT_TRUE(retry1.WaitForRecvTrailing());
  ASSERT_TRUE(retry2.WaitForRecvTrailing());
  EXPECT_EQ(retry1.NetworkState(), GrpcStreamNetworkState::kNotSentOnWire);
  EXPECT_EQ(retry2.NetworkState(), GrpcStreamNetworkState::kNotSentOnWire);

  // Release the GOAWAY write (and resume writes so any buggy extra cycles
  // don't deadlock the test before assertions run).
  state_->ResumeAllWrites();
  ASSERT_TRUE(state_->WaitForEndpointDestroyed());
  ASSERT_TRUE(
      close_notification_.WaitForNotificationWithTimeout(kCloseTestTimeout));

  EXPECT_EQ(state_->paused_write_count(), 1);
  std::string closed_err = StatusToString(GetClosedWithError());
  EXPECT_FALSE(
      absl::StrContains(closed_err, "Last stream closed after sending GOAWAY"))
      << closed_err;
  EXPECT_LE(CountOccurrences(closed_err, "ping timeout"), 1u) << closed_err;
}

// Verifies client stream rejection on the send_goaway path that does NOT
// immediately call close_transport_locked (e.g. PerformOp with op->goaway_error
// or benign resource-quota reclamation):
// 1. Peer advertises MAX_CONCURRENT_STREAMS = 1; stream 1 becomes active in
//    stream_map while stream 2 queues in waiting_for_concurrency.
// 2. Pauses writes and sends a client GOAWAY via PerformOp(op->goaway_error).
// 3. Verifies StateWatcher::OnDisconnect fires immediately with kGoaway, queued
//    stream 2 is cancelled immediately with UNAVAILABLE + kNotSentOnWire, and a
//    newly opened stream 3 is also rejected with UNAVAILABLE + kNotSentOnWire.
// 4. Releases writes, cancels stream 1, and verifies the transport closes.
TEST_F(Chttp2CloseTransportTest,
       ClientRejectsNewAndQueuedStreamsAfterGoawayOp) {
  CreateTransport(/*is_client=*/true);
  auto watcher = MakeRefCounted<TestStateWatcher>();
  {
    ExecCtx exec_ctx;
    transport_->StartWatch(watcher);
  }

  size_t read_offset = kClientPreface.size();
  // Complete initial SETTINGS exchange.
  ASSERT_TRUE(state_->WaitForFrame(read_offset, /*SETTINGS=*/0x04,
                                   /*flags=*/0x00, nullptr));
  state_->DeliverRead(kSettingsMaxConcurrentStreams1);
  ASSERT_TRUE(state_->WaitForFrame(read_offset, /*SETTINGS=*/0x04,
                                   /*ACK=*/0x01, nullptr));

  // Stream 1 occupies the single concurrency slot; stream 2 queues in
  // waiting_for_concurrency.
  TestStream active_stream(transport_);
  active_stream.StartClientCall();
  // Stream 1 HEADERS frame is written out.
  ASSERT_TRUE(state_->WaitForFrame(read_offset, /*HEADERS=*/0x01, -1, nullptr));
  ASSERT_TRUE(active_stream.WaitForSendComplete());

  TestStream queued_stream(transport_);
  queued_stream.StartClientCall();
  RunOnCombiner(transport_.get(), [&]() {
    EXPECT_EQ(transport_->stream_map.size(), 1u);
  });

  // Schedule a client GOAWAY via transport op while pausing writes.
  state_->SetPauseWrites(true);
  {
    ExecCtx exec_ctx;
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->goaway_error = absl::UnavailableError("client goaway");
    transport_->PerformOp(op);
  }
  ASSERT_TRUE(state_->WaitForPausedWrite());
  // Ensure the GOAWAY frame is going to be written.
  ASSERT_TRUE(state_->WaitForFrame(read_offset, /*GOAWAY=*/0x07, -1, nullptr));

  ASSERT_TRUE(watcher->WaitForDisconnect());
  EXPECT_EQ(watcher->disconnect_info().reason,
            Transport::StateWatcher::kGoaway);

  // The queued stream must be cancelled immediately with kNotSentOnWire.
  ASSERT_TRUE(queued_stream.WaitForRecvTrailing());
  EXPECT_EQ(queued_stream.TrailingStatus(), GRPC_STATUS_UNAVAILABLE);
  EXPECT_EQ(queued_stream.NetworkState(),
            GrpcStreamNetworkState::kNotSentOnWire);

  // New streams must also be rejected immediately with kNotSentOnWire.
  TestStream rejected_stream(transport_);
  rejected_stream.StartClientCall();
  ASSERT_TRUE(rejected_stream.WaitForRecvTrailing());
  EXPECT_EQ(rejected_stream.TrailingStatus(), GRPC_STATUS_UNAVAILABLE);
  EXPECT_EQ(rejected_stream.NetworkState(),
            GrpcStreamNetworkState::kNotSentOnWire);

  // Finish the GOAWAY write and cancel the active stream so the transport
  // closes cleanly.
  state_->ResumeAllWrites();
  active_stream.Cancel(absl::CancelledError("done"));
  ASSERT_TRUE(active_stream.WaitForRecvTrailing());
  ASSERT_TRUE(state_->WaitForEndpointDestroyed());
  ASSERT_TRUE(
      close_notification_.WaitForNotificationWithTimeout(kCloseTestTimeout));
}

// Verifies that close_transport_locked is guarded against synchronous
// re-entrancy from remove_stream() even when no new streams are opened:
// 1. Opens two server streams (IDs 1 and 3, without END_STREAM) and completes
//    the two-phase graceful GOAWAY + PING ACK handshake so the transport is in
//    GRPC_CHTTP2_FINAL_GOAWAY_SENT with write_state == IDLE and 2 streams still
//    draining in stream_map.
// 2. Pauses writes and injects a read error ("forced close sentinel"), causing
//    close_transport_locked -> end_all_the_calls to cancel both streams.
// 3. Cancelling stream 1 flips write_state from IDLE to WRITING, and removing
//    stream 3 empties stream_map while FINAL_GOAWAY_SENT is set. Without the
//    in_close_transport guard, remove_stream() would re-enter
//    close_transport_locked() and the outer call would append the error a
//    second time.
// 4. Verifies the final closed_with_error does not contain "Last stream closed
//    after sending GOAWAY" and contains "forced close sentinel" exactly once.
TEST_F(Chttp2CloseTransportTest,
       ServerCloseAfterFinalGoawayWithMultipleStreamsDoesNotRecurse) {
  CreateTransport(/*is_client=*/false);

  size_t read_offset = 0;
  // Send the initial SETTINGS frame.
  ASSERT_TRUE(state_->WaitForFrame(read_offset, /*SETTINGS=*/0x04,
                                   /*flags=*/0x00, nullptr));

  // Client preface + SETTINGS + 2 open streams (IDs 1 and 3).
  std::string initial_bytes = absl::StrCat(kClientPreface, kEmptySettingsFrame,
                                           MakeOpenClientHeadersFrame(1),
                                           MakeOpenClientHeadersFrame(3));
  // Simulate the client opening two streams.
  state_->DeliverRead(initial_bytes);
  ASSERT_TRUE(WaitForAcceptedStreams(2));
  AcceptedStream(0)->StartServerRecvTrailing();
  AcceptedStream(1)->StartServerRecvTrailing();

  // Trigger graceful GOAWAY on server, answer its PING, and wait for the final
  // GOAWAY write to finish while both streams remain active.
  {
    ExecCtx exec_ctx;
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->goaway_error = absl::UnavailableError("server shutdown");
    transport_->PerformOp(op);
  }
  ParsedHttp2Frame goaway1;
  ASSERT_TRUE(
      state_->WaitForFrame(read_offset, /*GOAWAY=*/0x07, -1, &goaway1));
  ParsedHttp2Frame ping;
  ASSERT_TRUE(
      state_->WaitForFrame(read_offset, /*PING=*/0x06, /*flags=*/0x00, &ping));
  state_->DeliverRead(MakePingAckFrame(ping.payload));
  ParsedHttp2Frame final_goaway;
  ASSERT_TRUE(
      state_->WaitForFrame(read_offset, /*GOAWAY=*/0x07, -1, &final_goaway));

  // Verify the transport is in FINAL_GOAWAY_SENT with write_state == IDLE and 2
  // streams still draining in stream_map.
  RunOnCombiner(transport_.get(), [&]() {
    EXPECT_EQ(transport_->sent_goaway_state, GRPC_CHTTP2_FINAL_GOAWAY_SENT);
    EXPECT_EQ(transport_->write_state, GRPC_CHTTP2_WRITE_STATE_IDLE);
    EXPECT_EQ(transport_->stream_map.size(), 2u);
  });

  // Trigger close_transport_locked with write_state == IDLE and 2 active
  // streams. Stream 1 flips write_state to WRITING; stream 2 empties
  // stream_map and must not re-enter close_transport_locked.
  state_->SetPauseWrites(true);
  state_->DeliverReadError(absl::UnavailableError("forced close sentinel"));
  ASSERT_TRUE(state_->WaitForPausedWrite());
  ASSERT_TRUE(AcceptedStream(0)->WaitForRecvTrailing());
  ASSERT_TRUE(AcceptedStream(1)->WaitForRecvTrailing());

  state_->ResumeAllWrites();
  ASSERT_TRUE(state_->WaitForEndpointDestroyed());
  ASSERT_TRUE(
      close_notification_.WaitForNotificationWithTimeout(kCloseTestTimeout));

  std::string closed_err = StatusToString(GetClosedWithError());
  EXPECT_FALSE(
      absl::StrContains(closed_err, "Last stream closed after sending GOAWAY"))
      << closed_err;
  EXPECT_EQ(CountOccurrences(closed_err, "forced close sentinel"), 1u)
      << closed_err;
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
