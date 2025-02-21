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
#include <grpc/support/time.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
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
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/telemetry/tcp_tracer.h"
#include "src/core/util/notification.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/test_util/fake_stats_plugin.h"

namespace grpc_core {
namespace {

Mutex* g_mu;
CoreEnd2endTest::TestNotification* g_client_call_ended_notify;
CoreEnd2endTest::TestNotification* g_server_call_ended_notify;

class FakeCallTracer : public ClientCallTracer {
 public:
  class FakeCallAttemptTracer : public CallAttemptTracer {
   public:
    FakeCallAttemptTracer() {
      MutexLock lock(g_mu);
      incoming_bytes_ = TransportByteSize();
      outgoing_bytes_ = TransportByteSize();
    }
    std::string TraceId() override { return ""; }
    std::string SpanId() override { return ""; }
    bool IsSampled() override { return false; }
    void RecordSendInitialMetadata(
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
      if (IsCallTracerInTransportEnabled()) return;
      TransportByteSize incoming_bytes = {
          transport_stream_stats->incoming.framing_bytes,
          transport_stream_stats->incoming.data_bytes,
          transport_stream_stats->incoming.header_bytes};
      TransportByteSize outgoing_bytes = {
          transport_stream_stats->outgoing.framing_bytes,
          transport_stream_stats->outgoing.data_bytes,
          transport_stream_stats->outgoing.header_bytes};
      MutexLock lock(g_mu);
      incoming_bytes_ = incoming_bytes;
      outgoing_bytes_ = outgoing_bytes;
    }

    void RecordIncomingBytes(
        const TransportByteSize& transport_byte_size) override {
      MutexLock lock(g_mu);
      incoming_bytes_ += transport_byte_size;
    }

    void RecordOutgoingBytes(
        const TransportByteSize& transport_byte_size) override {
      MutexLock lock(g_mu);
      outgoing_bytes_ += transport_byte_size;
    }

    void RecordCancel(grpc_error_handle /*cancel_error*/) override {}
    std::shared_ptr<TcpTracerInterface> StartNewTcpTrace() override {
      return nullptr;
    }
    void RecordEnd(const gpr_timespec& /*latency*/) override {
      g_client_call_ended_notify->Notify();
      delete this;
    }
    void RecordAnnotation(absl::string_view /*annotation*/) override {}
    void RecordAnnotation(const Annotation& /*annotation*/) override {}

    void SetOptionalLabel(OptionalLabelKey /*key*/,
                          RefCountedStringValue /*value*/) override {}

    static TransportByteSize incoming_bytes() {
      MutexLock lock(g_mu);
      return incoming_bytes_;
    }

    static TransportByteSize outgoing_bytes() {
      MutexLock lock(g_mu);
      return outgoing_bytes_;
    }

   private:
    static TransportByteSize incoming_bytes_ ABSL_GUARDED_BY(g_mu);
    static TransportByteSize outgoing_bytes_ ABSL_GUARDED_BY(g_mu);
  };

  explicit FakeCallTracer() {}
  ~FakeCallTracer() override {}
  std::string TraceId() override { return ""; }
  std::string SpanId() override { return ""; }
  bool IsSampled() override { return false; }

  FakeCallAttemptTracer* StartNewAttempt(
      bool /*is_transparent_retry*/) override {
    return new FakeCallAttemptTracer;
  }

  void RecordAnnotation(absl::string_view /*annotation*/) override {}
  void RecordAnnotation(const Annotation& /*annotation*/) override {}
};

CallTracerInterface::TransportByteSize
    FakeCallTracer::FakeCallAttemptTracer::incoming_bytes_;
CallTracerInterface::TransportByteSize
    FakeCallTracer::FakeCallAttemptTracer::outgoing_bytes_;

class FakeServerCallTracer : public ServerCallTracer {
 public:
  FakeServerCallTracer() {
    MutexLock lock(g_mu);
    incoming_bytes_ = TransportByteSize();
    outgoing_bytes_ = TransportByteSize();
  }
  ~FakeServerCallTracer() override {}
  void RecordSendInitialMetadata(
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
  std::shared_ptr<TcpTracerInterface> StartNewTcpTrace() override {
    return nullptr;
  }
  void RecordReceivedTrailingMetadata(
      grpc_metadata_batch* /*recv_trailing_metadata*/) override {}

  void RecordEnd(const grpc_call_final_info* final_info) override {
    if (!IsCallTracerInTransportEnabled()) {
      TransportByteSize incoming_bytes = {
          final_info->stats.transport_stream_stats.incoming.framing_bytes,
          final_info->stats.transport_stream_stats.incoming.data_bytes,
          final_info->stats.transport_stream_stats.incoming.header_bytes};
      TransportByteSize outgoing_bytes = {
          final_info->stats.transport_stream_stats.outgoing.framing_bytes,
          final_info->stats.transport_stream_stats.outgoing.data_bytes,
          final_info->stats.transport_stream_stats.outgoing.header_bytes};
      MutexLock lock(g_mu);
      incoming_bytes_ = incoming_bytes;
      outgoing_bytes_ = outgoing_bytes;
    }
    g_server_call_ended_notify->Notify();
  }

  void RecordIncomingBytes(
      const TransportByteSize& transport_byte_size) override {
    MutexLock lock(g_mu);
    incoming_bytes_ += transport_byte_size;
  }

  void RecordOutgoingBytes(
      const TransportByteSize& transport_byte_size) override {
    MutexLock lock(g_mu);
    outgoing_bytes_ += transport_byte_size;
  }

  void RecordAnnotation(absl::string_view /*annotation*/) override {}
  void RecordAnnotation(const Annotation& /*annotation*/) override {}
  std::string TraceId() override { return ""; }
  std::string SpanId() override { return ""; }
  bool IsSampled() override { return false; }

  static TransportByteSize incoming_bytes() {
    MutexLock lock(g_mu);
    return incoming_bytes_;
  }

  static TransportByteSize outgoing_bytes() {
    MutexLock lock(g_mu);
    return outgoing_bytes_;
  }

 private:
  static TransportByteSize incoming_bytes_ ABSL_GUARDED_BY(g_mu);
  static TransportByteSize outgoing_bytes_ ABSL_GUARDED_BY(g_mu);
};

CallTracerInterface::TransportByteSize FakeServerCallTracer::incoming_bytes_;
CallTracerInterface::TransportByteSize FakeServerCallTracer::outgoing_bytes_;

// TODO(yijiem): figure out how to reuse FakeStatsPlugin instead of
// inheriting and overriding it here.
class NewFakeStatsPlugin : public FakeStatsPlugin {
 public:
  ClientCallTracer* GetClientCallTracer(
      const Slice& /*path*/, bool /*registered_method*/,
      std::shared_ptr<StatsPlugin::ScopeConfig> /*scope_config*/) override {
    return GetContext<Arena>()->ManagedNew<FakeCallTracer>();
  }
  ServerCallTracer* GetServerCallTracer(
      std::shared_ptr<StatsPlugin::ScopeConfig> /*scope_config*/) override {
    return GetContext<Arena>()->ManagedNew<FakeServerCallTracer>();
  }
};

// This test verifies the HTTP2 stats on a stream
CORE_END2END_TEST(Http2FullstackSingleHopTests, StreamStats) {
  g_mu = new Mutex();
  g_client_call_ended_notify = new CoreEnd2endTest::TestNotification(this);
  g_server_call_ended_notify = new CoreEnd2endTest::TestNotification(this);
  GlobalStatsPluginRegistryTestPeer::ResetGlobalStatsPluginRegistry();
  GlobalStatsPluginRegistry::RegisterStatsPlugin(
      std::make_shared<NewFakeStatsPlugin>());
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
  g_client_call_ended_notify->WaitForNotificationWithTimeout(absl::Seconds(5));
  g_server_call_ended_notify->WaitForNotificationWithTimeout(absl::Seconds(5));

  auto client_outgoing_transport_stats =
      FakeCallTracer::FakeCallAttemptTracer::outgoing_bytes();
  auto client_incoming_transport_stats =
      FakeCallTracer::FakeCallAttemptTracer::incoming_bytes();
  auto server_outgoing_transport_stats = FakeServerCallTracer::outgoing_bytes();
  auto server_incoming_transport_stats = FakeServerCallTracer::incoming_bytes();
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

  delete g_client_call_ended_notify;
  g_client_call_ended_notify = nullptr;
  delete g_server_call_ended_notify;
  g_server_call_ended_notify = nullptr;
  delete g_mu;
  g_mu = nullptr;
}

}  // namespace
}  // namespace grpc_core
