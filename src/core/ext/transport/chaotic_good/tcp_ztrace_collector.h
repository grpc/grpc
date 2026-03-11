// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_TCP_ZTRACE_COLLECTOR_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_TCP_ZTRACE_COLLECTOR_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include "src/core/channelz/ztrace_collector.h"
#include "src/core/ext/transport/chaotic_good/tcp_frame_header.h"
#include "src/core/lib/event_engine/utils.h"
#include "absl/strings/str_join.h"

namespace grpc_core::chaotic_good {
namespace tcp_ztrace_collector_detail {

class Config {
 public:
  explicit Config(const channelz::ZTrace::Args&) {}

  template <typename T>
  bool Finishes(const T&) {
    return false;
  }
};

void MarkRead(bool read, Json::Object& object);

}  // namespace tcp_ztrace_collector_detail

struct ReadFrameHeaderTrace {
  TcpFrameHeader header;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("read", true)
        .Merge(header.ChannelzProperties());
  }
};

struct ReadDataHeaderTrace {
  TcpDataFrameHeader header;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("read", false)
        .Merge(header.ChannelzProperties());
  }
};

struct WriteFrameHeaderTrace {
  TcpFrameHeader header;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("read", false)
        .Merge(header.ChannelzProperties());
  }
};

struct EndpointWriteMetricsTrace {
  absl::Time timestamp;
  grpc_event_engine::experimental::EventEngine::Endpoint::WriteEvent
      write_event;
  std::vector<std::pair<absl::string_view, int64_t>> metrics;
  size_t endpoint_id;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("metadata_type",
             grpc_event_engine::experimental::WriteEventToString(write_event))
        .Set("fathom_timestamp", absl::FormatTime(timestamp))
        .Merge([this]() {
          channelz::PropertyList props;
          for (const auto& [name, value] : metrics) {
            props.Set(name, value);
          }
          return props;
        }())
        .Set("endpoint_id", endpoint_id);
  }

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString(absl::StrCat(
        "Endpoint Write: ",
        grpc_event_engine::experimental::WriteEventToString(write_event)));
    object["fathom_timestamp"] = Json::FromString(absl::StrCat(timestamp));
    for (const auto& [name, value] : metrics) {
      object.emplace(name, Json::FromNumber(value));
    }
    object["endpoint_id"] = Json::FromNumber(endpoint_id);
  }
};

struct TraceScheduledChannel {
  uint32_t id;
  bool ready;
  double start_time;
  double bytes_per_second;
  double allowed_bytes;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("id", id)
        .Set("ready", ready)
        .Set("start_time", start_time)
        .Set("bytes_per_second", bytes_per_second)
        .Set("allowed_bytes", allowed_bytes);
  }
};

struct TraceWriteSchedule {
  std::vector<TraceScheduledChannel> channels;
  double outstanding_bytes;
  double end_time_requested;
  double end_time_adjusted;
  double min_tokens;
  size_t num_ready;
  size_t MemoryUsage() const {
    return sizeof(*this) + sizeof(channels[0]) * channels.size();
  }
  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("channels",
             [this]() {
               channelz::PropertyTable table;
               for (const auto& channel : channels) {
                 table.AppendRow(channel.ChannelzProperties());
               }
               return table;
             }())
        .Set("end_time_requested", end_time_requested)
        .Set("end_time_adjusted", end_time_adjusted)
        .Set("min_tokens", min_tokens)
        .Set("outstanding_bytes", outstanding_bytes)
        .Set("num_ready", num_ready);
  }
};

struct WriteLargeFrameHeaderTrace {
  uint64_t payload_tag;
  uint64_t payload_size;
  uint32_t chosen_endpoint;
  uint32_t stream_id;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("metadata_type", "WRITE_LARGE_HEADER")
        .Set("payload_tag", payload_tag)
        .Set("payload_size", payload_size)
        .Set("chosen_endpoint", chosen_endpoint)
        .Set("stream_id", stream_id);
  }
};

struct WriteBytesToEndpointTrace {
  size_t bytes;
  size_t endpoint_id;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("metadata_type", "WRITE_BYTES")
        .Set("bytes", bytes)
        .Set("endpoint_id", endpoint_id);
  }
};

struct FinishWriteBytesToEndpointTrace {
  size_t endpoint_id;
  absl::Status status;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("metadata_type", "FINISH_WRITE")
        .Set("endpoint_id", endpoint_id)
        .Set("status", status);
  }
};

struct WriteBytesToControlChannelTrace {
  size_t bytes;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("metadata_type", "WRITE_CTL_BYTES")
        .Set("bytes", bytes);
  }
};

struct ChunkStreamAssociationTrace {
  int64_t stream_id;
  uint64_t flow_id;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("metadata_type", "CHUNK_STREAM_ASSOCIATION")
        .Set("stream_id", stream_id)
        .Set("flow_id", flow_id);
  }
};

struct FinishWriteBytesToControlChannelTrace {
  absl::Status status;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("metadata_type", "FINISH_WRITE_CTL")
        .Set("status", status);
  }
};

template <bool read>
struct TransportError {
  absl::Status status;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("metadata_type", read ? "READ_ERROR" : "WRITE_ERROR")
        .Set("status", status);
  }
};

struct OrphanTrace {
  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList().Set("metadata_type", "ORPHAN");
  }
};

struct EndpointCloseTrace {
  uint32_t id;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("metadata_type", "ENDPOINT_CLOSE")
        .Set("endpoint_id", id);
  }
};

using TcpZTraceCollector = channelz::ZTraceCollector<
    tcp_ztrace_collector_detail::Config, ReadFrameHeaderTrace,
    ReadDataHeaderTrace, WriteFrameHeaderTrace, TraceWriteSchedule,
    WriteLargeFrameHeaderTrace, EndpointWriteMetricsTrace,
    WriteBytesToEndpointTrace, FinishWriteBytesToEndpointTrace,
    WriteBytesToControlChannelTrace, FinishWriteBytesToControlChannelTrace,
    ChunkStreamAssociationTrace, TransportError<true>, TransportError<false>,
    OrphanTrace, EndpointCloseTrace>;

}  // namespace grpc_core::chaotic_good

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_TCP_ZTRACE_COLLECTOR_H
