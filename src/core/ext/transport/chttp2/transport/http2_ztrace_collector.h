// Copyright 2025 The gRPC Authors
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_ZTRACE_COLLECTOR_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_ZTRACE_COLLECTOR_H

#include <cstdint>
#include <map>
#include <string>

#include "src/core/channelz/property_list.h"
#include "src/core/channelz/ztrace_collector.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"

namespace grpc_core {
namespace http2_ztrace_collector_detail {

class Config {
 public:
  explicit Config(const channelz::ZTrace::Args&) {}

  template <typename T>
  bool Finishes(const T&) {
    return false;
  }
};

}  // namespace http2_ztrace_collector_detail

template <bool kRead>
struct H2DataTrace {
  uint32_t stream_id;
  bool end_stream;
  uint32_t payload_length;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("read", kRead)
        .Set("frame_type", "DATA")
        .Set("stream_id", stream_id)
        .Set("end_stream", end_stream)
        .Set("payload_length", payload_length);
  }
};

template <bool kRead>
struct H2HeaderTrace {
  uint32_t stream_id;
  bool end_headers;
  bool end_stream;
  bool continuation;
  uint32_t payload_length;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("read", kRead)
        .Set("frame_type", continuation ? "CONTINUATION" : "HEADERS")
        .Set("stream_id", stream_id)
        .Set("end_headers", end_headers)
        .Set("end_stream", end_stream)
        .Set("payload_length", payload_length);
  }
};

template <bool kRead>
struct H2RstStreamTrace {
  uint32_t stream_id;
  uint32_t error_code;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("read", kRead)
        .Set("frame_type", "RST_STREAM")
        .Set("stream_id", stream_id)
        .Set("error_code", error_code);
  }
};

template <bool kRead>
struct H2SettingsTrace {
  bool ack;
  std::vector<Http2SettingsFrame::Setting> settings;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("read", kRead)
        .Set("frame_type", "SETTINGS")
        .Set("ack", ack)
        .Set("settings", [this]() {
          channelz::PropertyTable table;
          for (const auto& setting : settings) {
            table.AppendRow(channelz::PropertyList()
                                .Set("id", setting.id)
                                .Set("value", setting.value));
          }
          return table;
        }());
  }
};

template <bool kRead>
struct H2PingTrace {
  bool ack;
  uint64_t opaque;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("read", kRead)
        .Set("frame_type", "PING")
        .Set("ack", ack)
        .Set("opaque", opaque);
  }
};

template <bool kRead>
struct H2GoAwayTrace {
  uint32_t last_stream_id;
  uint32_t error_code;
  std::string debug_data;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("read", kRead)
        .Set("frame_type", "GOAWAY")
        .Set("last_stream_id", last_stream_id)
        .Set("error_code", error_code)
        .Set("debug_data", debug_data);
  }
};

template <bool kRead>
struct H2WindowUpdateTrace {
  uint32_t stream_id;
  uint32_t window_size_increment;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("read", kRead)
        .Set("frame_type", "WINDOW_UPDATE")
        .Set("stream_id", stream_id)
        .Set("window_size_increment", window_size_increment);
  }
};

template <bool kRead>
struct H2SecurityTrace {
  uint32_t payload_length;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("read", kRead)
        .Set("frame_type", "SECURITY")
        .Set("payload_length", payload_length);
  }
};

struct H2UnknownFrameTrace {
  uint8_t type;
  uint8_t flags;
  uint32_t stream_id;
  uint32_t payload_length;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("frame_type", "UNKNOWN")
        .Set("type", type)
        .Set("flags", flags)
        .Set("stream_id", stream_id)
        .Set("payload_length", payload_length);
  }
};

struct H2FlowControlStall {
  int64_t transport_window;
  int64_t stream_window;
  uint32_t stream_id;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("frame_type", "FLOW_CONTROL_STALL")
        .Set("transport_window", transport_window)
        .Set("stream_window", stream_window)
        .Set("stream_id", stream_id);
  }
};

struct H2BeginWriteCycle {
  uint32_t target_size;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("frame_type", "BEGIN_WRITE_CYCLE")
        .Set("target_size", target_size);
  }
};

struct H2BeginEndpointWrite {
  uint32_t write_size;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("frame_type", "BEGIN_ENDPOINT_WRITE")
        .Set("write_size", write_size);
  }
};

struct H2EndWriteCycle {
  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList().Set("frame_type", "END_WRITE_CYCLE");
  }
};

struct H2TcpMetricsTrace {
  std::shared_ptr<
      grpc_event_engine::experimental::EventEngine::Endpoint::TelemetryInfo>
      telemetry_info;
  grpc_event_engine::experimental::EventEngine::Endpoint::WriteEvent event;
  std::vector<
      grpc_event_engine::experimental::EventEngine::Endpoint::WriteMetric>
      metrics;
  absl::Time timestamp;

  size_t MemoryUsage() const {
    return sizeof(H2TcpMetricsTrace) + metrics.capacity() * sizeof(metrics[0]);
  }

  channelz::PropertyList ChannelzProperties() const {
    absl::string_view event_string = "unknown";
    switch (event) {
      case grpc_event_engine::experimental::EventEngine::Endpoint::WriteEvent::
          kSendMsg:
        event_string = "send_msg";
        break;
      case grpc_event_engine::experimental::EventEngine::Endpoint::WriteEvent::
          kScheduled:
        event_string = "scheduled";
        break;
      case grpc_event_engine::experimental::EventEngine::Endpoint::WriteEvent::
          kSent:
        event_string = "sent";
        break;
      case grpc_event_engine::experimental::EventEngine::Endpoint::WriteEvent::
          kAcked:
        event_string = "acked";
        break;
      case grpc_event_engine::experimental::EventEngine::Endpoint::WriteEvent::
          kClosed:
        event_string = "closed";
        break;
      default:
        break;
    }
    return channelz::PropertyList()
        .Set("event", event_string)
        .Set("tcp_event_timestamp", timestamp)
        .Merge([this]() {
          channelz::PropertyList props;
          for (const auto& metric : metrics) {
            if (auto key = telemetry_info->GetMetricName(metric.key);
                key.has_value()) {
              props.Set(*key, metric.value);
            }
          }
          return props;
        }());
  }
};

using Http2ZTraceCollector = channelz::ZTraceCollector<
    http2_ztrace_collector_detail::Config, H2DataTrace<false>,
    H2HeaderTrace<false>, H2RstStreamTrace<false>, H2SettingsTrace<false>,
    H2PingTrace<false>, H2GoAwayTrace<false>, H2WindowUpdateTrace<false>,
    H2SecurityTrace<false>, H2DataTrace<true>, H2HeaderTrace<true>,
    H2RstStreamTrace<true>, H2SettingsTrace<true>, H2PingTrace<true>,
    H2GoAwayTrace<true>, H2WindowUpdateTrace<true>, H2SecurityTrace<true>,
    H2UnknownFrameTrace, H2FlowControlStall, H2BeginWriteCycle, H2EndWriteCycle,
    H2BeginEndpointWrite, H2TcpMetricsTrace>;

struct PromiseEndpointReadTrace {
  uint64_t bytes;
  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList().Set("read_bytes", bytes);
  }
};

struct PromiseEndpointWriteTrace {
  uint64_t count;
  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList().Set("frames_count", count);
  }
};

namespace promise_http2_ztrace_collector_detail {
class Config {
 public:
  explicit Config(const channelz::ZTrace::Args&) {}

  template <typename T>
  bool Finishes(const T&) {
    return false;
  }
};
}  // namespace promise_http2_ztrace_collector_detail

using PromiseHttp2ZTraceCollector =
    channelz::ZTraceCollector<promise_http2_ztrace_collector_detail::Config,
                              PromiseEndpointReadTrace,
                              PromiseEndpointWriteTrace>;

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_ZTRACE_COLLECTOR_H
