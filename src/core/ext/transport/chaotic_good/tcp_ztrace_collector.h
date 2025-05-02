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

#include <grpc/support/thd_id.h>

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

  size_t MemoryUsage() const { return sizeof(*this); }

  void RenderJson(Json::Object& object) const {
    tcp_ztrace_collector_detail::MarkRead(true, object);
    tcp_ztrace_collector_detail::TcpFrameHeaderToJsonObject(header, object);
  }
};

struct ReadDataHeaderTrace {
  TcpDataFrameHeader header;

  size_t MemoryUsage() const { return sizeof(*this); }

  void RenderJson(Json::Object& object) const {
    tcp_ztrace_collector_detail::MarkRead(true, object);
    tcp_ztrace_collector_detail::TcpDataFrameHeaderToJsonObject(header, object);
  }
};

struct WriteFrameHeaderTrace {
  TcpFrameHeader header;
  gpr_thd_id thread_id = gpr_thd_currentid();

  size_t MemoryUsage() const { return sizeof(*this); }

  void RenderJson(Json::Object& object) const {
    tcp_ztrace_collector_detail::MarkRead(false, object);
    tcp_ztrace_collector_detail::TcpFrameHeaderToJsonObject(header, object);
    object["thd"] = Json::FromNumber(thread_id);
  }
};

struct EndpointWriteMetricsTrace {
  absl::Time timestamp;
  grpc_event_engine::experimental::EventEngine::Endpoint::WriteEvent
      write_event;
  std::vector<std::pair<absl::string_view, int64_t>> metrics;
  gpr_thd_id thread_id = gpr_thd_currentid();

  size_t MemoryUsage() const {
    return sizeof(*this) + sizeof(metrics[0]) * metrics.size();
  }

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString(absl::StrCat(
        "Endpoint Write: ",
        grpc_event_engine::experimental::WriteEventToString(write_event)));
    object["fathom_timestamp"] = Json::FromString(absl::StrCat(timestamp));
    for (const auto& [name, value] : metrics) {
      object.emplace(name, Json::FromNumber(value));
    }
    object["thd"] = Json::FromNumber(thread_id);
  }
};

struct LbDecision {
  struct CurrentSend {
    uint64_t bytes;
    double age;
  };

  uint64_t bytes;
  std::optional<CurrentSend> current_send;
  double current_rate;
  std::optional<double> delivery_time;

  Json ToJson() const {
    Json::Object object;
    object["bytes"] = Json::FromNumber(bytes);
    if (current_send.has_value()) {
      object["send_size"] = Json::FromNumber(current_send->bytes);
      object["send_age"] = Json::FromNumber(current_send->age);
    }
    object["current_rate"] = Json::FromNumber(current_rate);
    if (delivery_time.has_value()) {
      object["delivery_time"] = Json::FromNumber(*delivery_time);
    }
    return Json::FromObject(std::move(object));
  }
};

struct WriteLargeFrameHeaderTrace {
  TcpDataFrameHeader data_header;
  size_t chosen_endpoint;
  std::vector<std::optional<LbDecision>> lb_decisions;
  gpr_thd_id thread_id = gpr_thd_currentid();

  size_t MemoryUsage() const {
    return sizeof(*this) + sizeof(lb_decisions[0]) * lb_decisions.size();
  }

  void RenderJson(Json::Object& object) const {
    tcp_ztrace_collector_detail::MarkRead(false, object);
    tcp_ztrace_collector_detail::TcpDataFrameHeaderToJsonObject(data_header,
                                                                object);
    Json::Array lb;
    for (const auto& d : lb_decisions) {
      if (d.has_value()) {
        lb.emplace_back(d->ToJson());
      } else {
        lb.emplace_back(Json::FromObject({}));
      }
    }
    object["chosen_endpoint"] = Json::FromNumber(chosen_endpoint);
    object["lb_decisions"] = Json::FromArray(std::move(lb));
    object["thd"] = Json::FromNumber(thread_id);
  }
};

struct NoEndpointForWriteTrace {
  size_t bytes;
  uint64_t payload_tag;
  gpr_thd_id thread_id = gpr_thd_currentid();

  size_t MemoryUsage() const { return sizeof(*this); }

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("NO_ENDPOINT_FOR_WRITE");
    object["payload_tag"] = Json::FromNumber(payload_tag);
    object["bytes"] = Json::FromNumber(bytes);
    object["thd"] = Json::FromNumber(thread_id);
  }
};

struct WriteBytesToEndpointTrace {
  size_t bytes;
  size_t endpoint_id;
  bool trace;
  gpr_thd_id thread_id = gpr_thd_currentid();

  size_t MemoryUsage() const { return sizeof(*this); }

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("WRITE_BYTES");
    object["bytes"] = Json::FromNumber(bytes);
    object["endpoint_id"] = Json::FromNumber(endpoint_id);
    object["thd"] = Json::FromNumber(thread_id);
    if (trace) {
      object["trace"] = Json::FromBool(true);
    }
  }
};

struct FinishWriteBytesToEndpointTrace {
  size_t endpoint_id;
  absl::Status status;
  gpr_thd_id thread_id = gpr_thd_currentid();

  size_t MemoryUsage() const {
    size_t size = sizeof(*this);
    if (!status.ok()) size += status.message().size();
    return size;
  }

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("FINISH_WRITE");
    object["endpoint_id"] = Json::FromNumber(endpoint_id);
    object["status"] = Json::FromString(status.ToString());
    object["thd"] = Json::FromNumber(thread_id);
  }
};

struct WriteBytesToControlChannelTrace {
  size_t bytes;
  gpr_thd_id thread_id = gpr_thd_currentid();

  size_t MemoryUsage() const { return sizeof(*this); }

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("WRITE_CTL_BYTES");
    object["bytes"] = Json::FromNumber(bytes);
    object["thd"] = Json::FromNumber(thread_id);
  }
};

struct FinishWriteBytesToControlChannelTrace {
  absl::Status status;
  gpr_thd_id thread_id = gpr_thd_currentid();

  size_t MemoryUsage() const { return sizeof(*this); }

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("FINISH_WRITE_CTL");
    object["status"] = Json::FromString(status.ToString());
    object["thd"] = Json::FromNumber(thread_id);
  }
};

template <bool read>
struct TransportError {
  absl::Status status;
  gpr_thd_id thread_id = gpr_thd_currentid();

  size_t MemoryUsage() const {
    size_t size = sizeof(*this);
    if (!status.ok()) size += status.message().size();
    return size;
  }

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] =
        Json::FromString(read ? "READ_ERROR" : "WRITE_ERROR");
    object["status"] = Json::FromString(status.ToString());
    object["thd"] = Json::FromNumber(thread_id);
  }
};

struct OrphanTrace {
  gpr_thd_id thread_id = gpr_thd_currentid();

  size_t MemoryUsage() const { return sizeof(*this); }

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("ORPHAN");
    object["thd"] = Json::FromNumber(thread_id);
  }
};

struct EndpointCloseTrace {
  uint32_t id;
  gpr_thd_id thread_id = gpr_thd_currentid();

  size_t MemoryUsage() const { return sizeof(*this); }

  void RenderJson(Json::Object& object) const {
    object["metadata_type"] = Json::FromString("ENDPOINT_CLOSE");
    object["endpoint_id"] = Json::FromNumber(id);
    object["thd"] = Json::FromNumber(thread_id);
  }
};

using TcpZTraceCollector = channelz::ZTraceCollector<
    tcp_ztrace_collector_detail::Config, ReadFrameHeaderTrace,
    ReadDataHeaderTrace, WriteFrameHeaderTrace, WriteLargeFrameHeaderTrace,
    EndpointWriteMetricsTrace, NoEndpointForWriteTrace,
    WriteBytesToEndpointTrace, FinishWriteBytesToEndpointTrace,
    WriteBytesToControlChannelTrace, FinishWriteBytesToControlChannelTrace,
    TransportError<true>, TransportError<false>, OrphanTrace,
    EndpointCloseTrace>;

}  // namespace grpc_core::chaotic_good

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_TCP_ZTRACE_COLLECTOR_H
