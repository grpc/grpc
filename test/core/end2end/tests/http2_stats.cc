// Copyright 2023 gRPC authors.
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

#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/telemetry/tcp_tracer.h"
#include "src/core/util/notification.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "gtest/gtest.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace grpc_core {
namespace {

class TestState {
 public:
  explicit TestState(CoreEnd2endTest* test)
      : client_call_ended_(test), server_call_ended_(test) {}

  void NotifyClient() { client_call_ended_.Notify(); }

  void NotifyServer() { server_call_ended_.Notify(); }

  void WaitForClient() {
    EXPECT_TRUE(
        client_call_ended_.WaitForNotificationWithTimeout(absl::Seconds(5)));
  }

  void WaitForServer() {
    EXPECT_TRUE(
        server_call_ended_.WaitForNotificationWithTimeout(absl::Seconds(5)));
  }

  void ResetClientByteSizes(
      CallTracerInterface::TransportByteSize incoming = {},
      CallTracerInterface::TransportByteSize outgoing = {}) {
    MutexLock lock(&mu_);
    client_incoming_bytes_ = incoming;
    client_outgoing_bytes_ = outgoing;
  }

  void IncrementClientIncomingBytes(
      CallTracerInterface::TransportByteSize bytes) {
    MutexLock lock(&mu_);
    client_incoming_bytes_ += bytes;
  }

  void IncrementClientOutgoingBytes(
      CallTracerInterface::TransportByteSize bytes) {
    MutexLock lock(&mu_);
    client_outgoing_bytes_ += bytes;
  }

  void ResetServerByteSizes(
      CallTracerInterface::TransportByteSize incoming = {},
      CallTracerInterface::TransportByteSize outgoing = {}) {
    MutexLock lock(&mu_);
    server_incoming_bytes_ = incoming;
    server_outgoing_bytes_ = outgoing;
  }

  void IncrementServerIncomingBytes(
      CallTracerInterface::TransportByteSize bytes) {
    MutexLock lock(&mu_);
    server_incoming_bytes_ += bytes;
  }

  void IncrementServerOutgoingBytes(
      CallTracerInterface::TransportByteSize bytes) {
    MutexLock lock(&mu_);
    server_outgoing_bytes_ += bytes;
  }

  std::tuple<CallTracerInterface::TransportByteSize,
             CallTracerInterface::TransportByteSize,
             CallTracerInterface::TransportByteSize,
             CallTracerInterface::TransportByteSize>
  ByteSizes() {
    MutexLock lock(&mu_);
    return std::tuple(client_incoming_bytes_, client_outgoing_bytes_,
                      server_incoming_bytes_, server_outgoing_bytes_);
  }

 private:
  Mutex mu_;
  CoreEnd2endTest::TestNotification client_call_ended_;
  CoreEnd2endTest::TestNotification server_call_ended_;
  CallTracerInterface::TransportByteSize client_incoming_bytes_
      ABSL_GUARDED_BY(mu_);
  CallTracerInterface::TransportByteSize client_outgoing_bytes_
      ABSL_GUARDED_BY(mu_);
  CallTracerInterface::TransportByteSize server_incoming_bytes_
      ABSL_GUARDED_BY(mu_);
  CallTracerInterface::TransportByteSize server_outgoing_bytes_
      ABSL_GUARDED_BY(mu_);
};

class FakeCallTracer : public ClientCallTracerInterface {
 public:
  class FakeCallAttemptTracer : public CallAttemptTracer {
   public:
    explicit FakeCallAttemptTracer(std::shared_ptr<TestState> test_state)
        : test_state_(std::move(test_state)) {
      test_state_->ResetClientByteSizes();
    }
    std::string TraceId() override { return ""; }
    std::string SpanId() override { return ""; }
    bool IsSampled() override { return false; }
    void RecordSendInitialMetadata(
        grpc_metadata_batch* send_initial_metadata) override {
      GRPC_CHECK(!IsCallTracerSendInitialMetadataIsAnAnnotationEnabled());
      MutateSendInitialMetadata(send_initial_metadata);
    }
    void MutateSendInitialMetadata(
        grpc_metadata_batch* /*send_initial_metadata*/) override {}
    void RecordSendTrailingMetadata(
        grpc_metadata_batch* /*send_trailing_metadata*/) override {}
    void RecordSendMessage(const Message& /*send_message*/) override {}
    void RecordSendCompressedMessage(
        const Message& /*send_compressed_message*/) override {}
    void RecordReceivedInitialMetadata(
        grpc_metadata_batch* /*recv_initial_metadata*/) override {}
    void RecordReceivedMessage(const Message& /*recv_message*/) override {}
    void RecordReceivedDecompressedMessage(
        const Message& /*recv_decompressed_message*/) override {}

    void RecordReceivedTrailingMetadata(
        absl::Status /*status*/,
        grpc_metadata_batch* /*recv_trailing_metadata*/,
        const grpc_transport_stream_stats* transport_stream_stats) override {
      if (IsCallTracerInTransportEnabled() ||
          transport_stream_stats == nullptr /* cancelled call */) {
        return;
      }
      test_state_->ResetClientByteSizes(
          {transport_stream_stats->incoming.framing_bytes,
           transport_stream_stats->incoming.data_bytes,
           transport_stream_stats->incoming.header_bytes},
          {transport_stream_stats->outgoing.framing_bytes,
           transport_stream_stats->outgoing.data_bytes,
           transport_stream_stats->outgoing.header_bytes});
    }

    void RecordIncomingBytes(
        const TransportByteSize& transport_byte_size) override {
      test_state_->IncrementClientIncomingBytes(transport_byte_size);
    }

    void RecordOutgoingBytes(
        const TransportByteSize& transport_byte_size) override {
      test_state_->IncrementClientOutgoingBytes(transport_byte_size);
    }

    void RecordCancel(grpc_error_handle /*cancel_error*/) override {}
    std::shared_ptr<TcpCallTracer> StartNewTcpTrace() override {
      return nullptr;
    }
    void RecordEnd() override {
      test_state_->NotifyClient();
      delete this;
    }
    void RecordAnnotation(absl::string_view /*annotation*/) override {}
    void RecordAnnotation(const Annotation& /*annotation*/) override {}

    void SetOptionalLabel(OptionalLabelKey /*key*/,
                          RefCountedStringValue /*value*/) override {}

   private:
    std::shared_ptr<TestState> test_state_;
  };

  explicit FakeCallTracer(std::shared_ptr<TestState> test_state)
      : test_state_(std::move(test_state)) {}
  ~FakeCallTracer() override {}
  std::string TraceId() override { return ""; }
  std::string SpanId() override { return ""; }
  bool IsSampled() override { return false; }

  FakeCallAttemptTracer* StartNewAttempt(
      bool /*is_transparent_retry*/) override {
    return new FakeCallAttemptTracer(test_state_);
  }

  void RecordAnnotation(absl::string_view /*annotation*/) override {}
  void RecordAnnotation(const Annotation& /*annotation*/) override {}

 private:
  std::shared_ptr<TestState> test_state_;
};

class FakeServerCallTracer : public ServerCallTracerInterface {
 public:
  explicit FakeServerCallTracer(std::shared_ptr<TestState> test_state)
      : test_state_(test_state) {
    test_state_->ResetServerByteSizes();
  }
  ~FakeServerCallTracer() override {}
  void RecordSendInitialMetadata(
      grpc_metadata_batch* send_initial_metadata) override {
    GRPC_CHECK(!IsCallTracerSendInitialMetadataIsAnAnnotationEnabled());
    MutateSendInitialMetadata(send_initial_metadata);
  }
  void MutateSendInitialMetadata(
      grpc_metadata_batch* /*send_initial_metadata*/) override {}
  void RecordSendTrailingMetadata(
      grpc_metadata_batch* /*send_trailing_metadata*/) override {}
  void RecordSendMessage(const Message& /*send_message*/) override {}
  void RecordSendCompressedMessage(
      const Message& /*send_compressed_message*/) override {}
  void RecordReceivedInitialMetadata(
      grpc_metadata_batch* /*recv_initial_metadata*/) override {}
  void RecordReceivedMessage(const Message& /*recv_message*/) override {}
  void RecordReceivedDecompressedMessage(
      const Message& /*recv_decompressed_message*/) override {}
  void RecordCancel(grpc_error_handle /*cancel_error*/) override {}
  std::shared_ptr<TcpCallTracer> StartNewTcpTrace() override { return nullptr; }
  void RecordReceivedTrailingMetadata(
      grpc_metadata_batch* /*recv_trailing_metadata*/) override {}

  void RecordEnd(const grpc_call_final_info* final_info) override {
    if (!IsCallTracerInTransportEnabled()) {
      test_state_->ResetServerByteSizes(
          {final_info->stats.transport_stream_stats.incoming.framing_bytes,
           final_info->stats.transport_stream_stats.incoming.data_bytes,
           final_info->stats.transport_stream_stats.incoming.header_bytes},
          {final_info->stats.transport_stream_stats.outgoing.framing_bytes,
           final_info->stats.transport_stream_stats.outgoing.data_bytes,
           final_info->stats.transport_stream_stats.outgoing.header_bytes});
    }
    test_state_->NotifyServer();
  }

  void RecordIncomingBytes(
      const TransportByteSize& transport_byte_size) override {
    test_state_->IncrementServerIncomingBytes(transport_byte_size);
  }

  void RecordOutgoingBytes(
      const TransportByteSize& transport_byte_size) override {
    test_state_->IncrementServerOutgoingBytes(transport_byte_size);
  }

  void RecordAnnotation(absl::string_view /*annotation*/) override {}
  void RecordAnnotation(const Annotation& /*annotation*/) override {}
  std::string TraceId() override { return ""; }
  std::string SpanId() override { return ""; }
  bool IsSampled() override { return false; }

 private:
  std::shared_ptr<TestState> test_state_;
};

// TODO(yijiem): figure out how to reuse FakeStatsPlugin instead of
// inheriting and overriding it here.
class NewFakeStatsPlugin : public FakeStatsPlugin {
 public:
  explicit NewFakeStatsPlugin(std::shared_ptr<TestState> test_state)
      : test_state_(std::move(test_state)) {}

  ClientCallTracerInterface* GetClientCallTracer(
      const Slice& /*path*/, bool /*registered_method*/,
      std::shared_ptr<StatsPlugin::ScopeConfig> /*scope_config*/) override {
    return GetContext<Arena>()->ManagedNew<FakeCallTracer>(test_state_);
  }
  ServerCallTracerInterface* GetServerCallTracer(
      std::shared_ptr<StatsPlugin::ScopeConfig> /*scope_config*/) override {
    return GetContext<Arena>()->ManagedNew<FakeServerCallTracer>(test_state_);
  }

 private:
  std::shared_ptr<TestState> test_state_;
};

// This test verifies the HTTP2 stats on a stream
CORE_END2END_TEST(Http2FullstackSingleHopTests, StreamStats) {
  auto test_state = std::make_shared<TestState>(this);
  GlobalStatsPluginRegistryTestPeer::ResetGlobalStatsPluginRegistry();
  GlobalStatsPluginRegistry::RegisterStatsPlugin(
      std::make_shared<NewFakeStatsPlugin>(test_state));
  auto send_from_client = RandomSlice(10);
  auto send_from_server = RandomSlice(20);
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  IncomingMessage client_message;
  IncomingCloseOnServer client_close;
  {
    auto c = NewClientCall("/foo").Timeout(Duration::Minutes(5)).Create();
    c.NewBatch(1)
        .SendInitialMetadata({})
        .SendMessage(send_from_client.Ref())
        .SendCloseFromClient()
        .RecvInitialMetadata(server_initial_metadata)
        .RecvMessage(server_message)
        .RecvStatusOnClient(server_status);
    auto s = RequestCall(101);
    Expect(101, true);
    Step(Duration::Minutes(1));
    s.NewBatch(102).SendInitialMetadata({}).RecvMessage(client_message);
    Expect(102, true);
    Step(Duration::Minutes(1));
    s.NewBatch(103)
        .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {})
        .SendMessage(send_from_server.Ref())
        .RecvCloseOnServer(client_close);
    Expect(103, true);
    Expect(1, true);
    Step(Duration::Minutes(1));
    EXPECT_EQ(s.method(), "/foo");
  }
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(client_message.payload(), send_from_client);
  EXPECT_EQ(server_message.payload(), send_from_server);
  // Make sure that the calls have ended for the stats to have been collected
  test_state->WaitForClient();
  test_state->WaitForServer();
  auto [client_incoming_transport_stats, client_outgoing_transport_stats,
        server_incoming_transport_stats, server_outgoing_transport_stats] =
      test_state->ByteSizes();
  EXPECT_EQ(client_outgoing_transport_stats.data_bytes,
            send_from_client.size());
  EXPECT_EQ(client_incoming_transport_stats.data_bytes,
            send_from_server.size());
  EXPECT_EQ(server_outgoing_transport_stats.data_bytes,
            send_from_server.size());
  EXPECT_EQ(server_incoming_transport_stats.data_bytes,
            send_from_client.size());
  // At the very minimum, we should have 9 bytes from initial header frame, 9
  // bytes from data header frame, 5 bytes from the grpc header on data and 9
  // bytes from the trailing header frame. The actual number might be more due
  // to RST_STREAM (13 bytes) and WINDOW_UPDATE (13 bytes) frames.
  EXPECT_GE(client_outgoing_transport_stats.framing_bytes, 32);
  EXPECT_LE(client_outgoing_transport_stats.framing_bytes, 58);
  EXPECT_GE(client_incoming_transport_stats.framing_bytes, 32);
  EXPECT_LE(client_incoming_transport_stats.framing_bytes, 58);
  EXPECT_GE(server_outgoing_transport_stats.framing_bytes, 32);
  EXPECT_LE(server_outgoing_transport_stats.framing_bytes, 58);
  EXPECT_GE(server_incoming_transport_stats.framing_bytes, 32);
  EXPECT_LE(server_incoming_transport_stats.framing_bytes, 58);
}

}  // namespace
}  // namespace grpc_core
