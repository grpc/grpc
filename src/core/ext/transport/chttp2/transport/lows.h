//
//
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
//
//

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_LOWS_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_LOWS_H

#include <queue>

#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/mpsc.h"

namespace grpc_core {
namespace http2 {

#define GRPC_LOWS_DEBUG VLOG(2)

class Lows {
 public:
  enum class StreamPriority : uint8_t {
    kStreamClosed = 0,
    kTransportJail,
    kDefault,
    kMax
  };
  Lows(const uint32_t max_queue_size = std::numeric_limits<uint32_t>::max())
      : queue_(max_queue_size) {}
  auto Enqueue(const uint32_t stream_id, const StreamPriority priority) {
    return Map(
        queue_.MakeSender().Send(StreamIDAndPriority{stream_id, priority}, 1),
        [stream_id, priority](StatusFlag status) {
          GRPC_LOWS_DEBUG << "Enqueue stream id " << stream_id
                          << " with priority " << GetPriorityString(priority)
                          << " status " << status;
          return status.ok() ? absl::OkStatus()
                             : absl::InternalError(absl::StrCat(
                                   "Failed to enqueue stream id ", stream_id));
        });
  }
  absl::Status UnbufferedImmediateEnqueue(const uint32_t stream_id,
                                          const StreamPriority priority) {
    StatusFlag status = queue_.MakeSender().UnbufferedImmediateSend(
        StreamIDAndPriority{stream_id, priority}, 1);
    GRPC_LOWS_DEBUG << "UnbufferedImmediateEnqueue stream id " << stream_id
                    << " with priority " << GetPriorityString(priority)
                    << " status " << status;
    return (status.ok()) ? absl::OkStatus()
                         : absl::InternalError(absl::StrCat(
                               "Failed to enqueue stream id ", stream_id));
  }
  absl::Status AddToTransportJail(const uint32_t stream_id) {
    prioritized_queue_.Push(stream_id, StreamPriority::kTransportJail);
    GRPC_LOWS_DEBUG << "AddToTransportJail stream id " << stream_id;
    return absl::OkStatus();
  }

  auto Next(const bool transport_tokens_available) {
    return Loop([this, transport_tokens_available]() {
      auto stream_id = prioritized_queue_.Pop(transport_tokens_available);
      return If(
          stream_id.has_value(),
          [stream_id]() -> LoopCtl<absl::StatusOr<uint32_t>> {
            GRPC_LOWS_DEBUG << "Next stream id " << stream_id.value();
            return stream_id.value();
          },
          [this] {
            GRPC_LOWS_DEBUG << "Query queue for next batch";
            return Map(
                queue_.NextBatch(128),
                [this](auto batch) -> LoopCtl<absl::StatusOr<uint32_t>> {
                  if (batch.ok()) {
                    GRPC_LOWS_DEBUG << "Next batch size "
                                    << batch.value().size();
                    for (auto stream_id_priority : batch.value()) {
                      prioritized_queue_.Push(stream_id_priority.stream_id,
                                              stream_id_priority.priority);
                    }
                    return Continue();
                  }
                  return absl::InternalError("Failed to read from queue");
                });
          });
    });
  }

  static inline std::string GetPriorityString(const StreamPriority priority) {
    switch (priority) {
      case StreamPriority::kStreamClosed:
        return "StreamClosed";
      case StreamPriority::kTransportJail:
        return "TransportJail";
      case StreamPriority::kDefault:
        return "Default";
      default:
        return "unknown";
    }
  }

 private:
  class PrioritizedQueue {
   public:
    PrioritizedQueue() : buckets_(kMaxPriority) {}

    void Push(const uint32_t stream_id, StreamPriority priority) {
      if (priority >= StreamPriority::kMax) {
        priority = StreamPriority::kDefault;
      }

      buckets_[static_cast<uint8_t>(priority)].push(stream_id);
    }

    std::optional<uint32_t> Pop(const bool transport_tokens_available) {
      for (uint8_t i = 0; i < buckets_.size(); ++i) {
        auto& bucket = buckets_[i];
        if (!bucket.empty()) {
          if (i == kTransportJailIndex && !transport_tokens_available) {
            GRPC_LOWS_DEBUG << "Transport tokens unavailable, skipping "
                               "transport jail";
            continue;
          }

          uint32_t stream_id = bucket.front();
          bucket.pop();
          GRPC_LOWS_DEBUG << "Popping stream id " << stream_id
                          << " from priority "
                          << GetPriorityString(static_cast<StreamPriority>(i));
          return stream_id;
        }
      }
      return std::nullopt;
    }

    static constexpr uint8_t kMaxPriority =
        static_cast<uint8_t>(StreamPriority::kMax);
    static constexpr uint8_t kTransportJailIndex =
        static_cast<uint8_t>(StreamPriority::kTransportJail);
    std::vector<std::queue<uint32_t>> buckets_;
  };
  struct StreamIDAndPriority {
    const uint32_t stream_id;
    StreamPriority priority;
  };
  MpscReceiver<StreamIDAndPriority> queue_;
  PrioritizedQueue prioritized_queue_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_LOWS_H
