//
//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_TRANSPORT_STATS_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_TRANSPORT_STATS_H

#include <grpc/credentials.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "src/core/ext/transport/chttp2/transport/http2_stats_collector.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/transport/auth_context.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace http2 {

// Encapsulates telemetry and metrics collection for HTTP/2 transports.
// Shared between Http2ClientTransport and Http2ServerTransport.
class Http2TransportStats {
 public:
  explicit Http2TransportStats(const ChannelArgs& channel_args)
      : stats_collector_(CreateHttp2StatsCollector(
            channel_args.GetObject<grpc_auth_context>())) {}
  ~Http2TransportStats() = default;

  // Http2TransportStats is non-copyable and non-movable.
  Http2TransportStats(const Http2TransportStats&) = delete;
  Http2TransportStats& operator=(const Http2TransportStats&) = delete;
  Http2TransportStats(Http2TransportStats&&) = delete;
  Http2TransportStats& operator=(Http2TransportStats&&) = delete;

  std::shared_ptr<Http2StatsCollector> GetStatsCollector() const {
    return stats_collector_;
  }

  // Records the payload size (in bytes) of an incoming DATA frame.
  void RecordReadDataFrameSize(const uint32_t data_frame_size) {
    stats_collector_->IncrementHttp2ReadDataFrameSize(data_frame_size);
  }

  // Records the payload size (in bytes) of an outgoing gRPC message.
  void RecordSendMessageSize(const uint64_t send_message_size) {
    stats_collector_->IncrementHttp2SendMessageSize(send_message_size);
  }

  // Increments the count of transport write cycles initiated.
  void RecordWritesBegun() { stats_collector_->IncrementHttp2WritesBegun(); }

  // Records the target byte size available for writing in a transport write
  // cycle.
  void RecordWriteTargetSize(const size_t write_target_size) {
    stats_collector_->IncrementHttp2WriteTargetSize(write_target_size);
  }

  // Records the payload size (in bytes) of an outgoing DATA frame.
  void RecordWriteDataFrameSize(const size_t data_frame_size) {
    stats_collector_->IncrementHttp2WriteDataFrameSize(data_frame_size);
  }

  // Increments the count of non-ACK SETTINGS frames sent.
  void RecordSettingsWrites() {
    stats_collector_->IncrementHttp2SettingsWrites();
  }

  // Increments the count of non-ACK PING frames sent.
  void RecordPingsSent() { stats_collector_->IncrementHttp2PingsSent(); }

  // Records the window size increment from an incoming transport-level
  // WINDOW_UPDATE frame, and records the elapsed time (in ms) since the
  // previous transport-level WINDOW_UPDATE if one was previously received.
  void RecordTransportWindowUpdate(const uint32_t increment) {
    const Timestamp now = Timestamp::Now();
    if (last_transport_window_update_time_ != Timestamp::InfPast()) {
      stats_collector_->IncrementHttp2TransportWindowUpdatePeriod(
          (now - last_transport_window_update_time_).millis());
    }
    last_transport_window_update_time_ = now;
    stats_collector_->IncrementHttp2TransportRemoteWindowUpdate(increment);
  }

  // Records the window size increment from an incoming stream-level
  // WINDOW_UPDATE frame, and records the elapsed time (in ms) since the
  // previous WINDOW_UPDATE on that stream if one was previously received.
  void RecordStreamWindowUpdate(const uint32_t increment,
                                Timestamp& last_window_update_time) {
    const Timestamp now = Timestamp::Now();
    if (last_window_update_time != Timestamp::InfPast()) {
      stats_collector_->IncrementHttp2StreamWindowUpdatePeriod(
          (now - last_window_update_time).millis());
    }
    last_window_update_time = now;
    stats_collector_->IncrementHttp2StreamRemoteWindowUpdate(increment);
  }

  // Records a transport stall, where a write is attempted but blocked due to
  // lack of transport-level flow control tokens.
  void RecordTransportStalls() {
    stats_collector_->IncrementHttp2TransportStalls();
  }

  // Records a stream stall, where a write is attempted but blocked due to
  // lack of stream-level flow control tokens.
  void RecordStreamStalls() { stats_collector_->IncrementHttp2StreamStalls(); }

 private:
  std::shared_ptr<Http2StatsCollector> stats_collector_;
  Timestamp last_transport_window_update_time_ = Timestamp::InfPast();
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_TRANSPORT_STATS_H
