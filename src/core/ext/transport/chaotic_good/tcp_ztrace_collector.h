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

namespace grpc_core::chaotic_good {
namespace tcp_ztrace_collector_detail {

class Config {
 public:
  explicit Config(std::map<std::string, std::string>) {}

  template <typename T>
  bool Finishes(const T&) {
    return false;
  }
};

void TcpFrameHeaderToJsonObject(const TcpFrameHeader& header,
                                Json::Object& object);
void TcpDataFrameHeaderToJsonObject(const TcpDataFrameHeader& header,
                                    Json::Object& object);
void MarkRead(bool read, Json::Object& object);

}  // namespace tcp_ztrace_collector_detail

struct ReadFrameHeaderTrace {
  TcpFrameHeader header;

  void RenderJson(Json::Object& object) const {
    tcp_ztrace_collector_detail::MarkRead(true, object);
    tcp_ztrace_collector_detail::TcpFrameHeaderToJsonObject(header, object);
  }
};

struct ReadDataHeaderTrace {
  TcpDataFrameHeader header;

  void RenderJson(Json::Object& object) const {
    tcp_ztrace_collector_detail::MarkRead(true, object);
    tcp_ztrace_collector_detail::TcpDataFrameHeaderToJsonObject(header, object);
  }
};

struct WriteFrameHeaderTrace {
  TcpFrameHeader header;

  void RenderJson(Json::Object& object) const {
    tcp_ztrace_collector_detail::MarkRead(false, object);
    tcp_ztrace_collector_detail::TcpFrameHeaderToJsonObject(header, object);
  }
};

struct EndpointWriteMetricsTrace {
  absl::Time timestamp;
  grpc_event_engine::experimental::EventEngine::Endpoint::WriteEvent
      write_event;
  std::vector<std::pair<absl::string_view, int64_t>> metrics;
  size_t endpoint_id;

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
  Json ToJson() const {
    return Json::FromObject({
        {"id", Json::FromNumber(id)},
        {"ready", Json::FromBool(ready)},
        {"start_time", Json::FromNumber(start_time)},
        {"bytes_per_second", Json::FromNumber(bytes_per_second)},
        {"allowed_bytes", Json::FromNumber(allowed_bytes)},
    });
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
  void RenderJson(Json::Object& object) const {
    Json::Array channels;
    for (const auto& c : this->channels) {
      channels.emplace_back(c.ToJson());
    }
    object["channels"] = Json::FromArray(std::move(channels));
    object["end_time_requested"] = Json::FromNumber(end_time_requested);
    object["end_time_adjusted"] = Json::FromNumber(end_time_adjusted);
    object["min_tokens"] = Json::FromNumber(min_tokens);
    object["outstanding_bytes"] = Json::FromNumber(outstanding_bytes);
    object["num_ready"] = Json::FromNumber(num_ready);
  }
};

struct WriteLargeFrameHeaderTrace {
  uint64_t payload_tag;
  uint64_t payload_size;
  uint32_t chosen_endpoint;

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("WRITE_LARGE_HEADER");
    tcp_ztrace_collector_detail::MarkRead(false, object);
    object["payload_tag"] = Json::FromNumber(payload_tag);
    object["payload_size"] = Json::FromNumber(payload_size);
    object["chosen_endpoint"] = Json::FromNumber(chosen_endpoint);
  }
};

struct WriteBytesToEndpointTrace {
  size_t bytes;
  size_t endpoint_id;

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("WRITE_BYTES");
    object["bytes"] = Json::FromNumber(bytes);
    object["endpoint_id"] = Json::FromNumber(endpoint_id);
  }
};

struct FinishWriteBytesToEndpointTrace {
  size_t endpoint_id;
  absl::Status status;

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("FINISH_WRITE");
    object["endpoint_id"] = Json::FromNumber(endpoint_id);
    object["status"] = Json::FromString(status.ToString());
  }
};

struct WriteBytesToControlChannelTrace {
  size_t bytes;

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("WRITE_CTL_BYTES");
    object["bytes"] = Json::FromNumber(bytes);
  }
};

struct FinishWriteBytesToControlChannelTrace {
  absl::Status status;

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("FINISH_WRITE_CTL");
    object["status"] = Json::FromString(status.ToString());
  }
};

template <bool read>
struct TransportError {
  absl::Status status;

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] =
        Json::FromString(read ? "READ_ERROR" : "WRITE_ERROR");
    object["status"] = Json::FromString(status.ToString());
  }
};

struct OrphanTrace {
  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("ORPHAN");
  }
};

struct EndpointCloseTrace {
  uint32_t id;

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("ENDPOINT_CLOSE");
    object["endpoint_id"] = Json::FromNumber(id);
  }
};

using TcpZTraceCollector = channelz::ZTraceCollector<
    tcp_ztrace_collector_detail::Config, ReadFrameHeaderTrace,
    ReadDataHeaderTrace, WriteFrameHeaderTrace, TraceWriteSchedule,
    WriteLargeFrameHeaderTrace, EndpointWriteMetricsTrace,
    WriteBytesToEndpointTrace, FinishWriteBytesToEndpointTrace,
    WriteBytesToControlChannelTrace, FinishWriteBytesToControlChannelTrace,
    TransportError<true>, TransportError<false>, OrphanTrace,
    EndpointCloseTrace>;

}  // namespace grpc_core::chaotic_good

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_TCP_ZTRACE_COLLECTOR_H
