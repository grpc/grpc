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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_WRITABLE_STREAMS_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_WRITABLE_STREAMS_H

#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <vector>

#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/grpc_check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {
namespace http2 {

namespace writable_streams_internal {
template <typename StreamPtr, typename = void>
struct HasGetStreamId {
  static constexpr bool value = false;
};

template <typename StreamPtr>
struct HasGetStreamId<
    StreamPtr,
    absl::void_t<decltype(std::declval<StreamPtr>()->GetStreamId())>> {
  static constexpr bool value =
      std::is_same_v<decltype(std::declval<StreamPtr>()->GetStreamId()),
                     uint32_t>;
};

}  // namespace writable_streams_internal

#define GRPC_WRITABLE_STREAMS_DEBUG VLOG(2)

template <typename StreamPtr>
class WritableStreams {
  static_assert(writable_streams_internal::HasGetStreamId<StreamPtr>::value,
                "StreamPtr must have a GetStreamId() function");

 public:
  explicit WritableStreams(
      const uint32_t max_queue_size = std::numeric_limits<uint32_t>::max())
      : queue_(max_queue_size), sender_(queue_.MakeSender()) {}

  // WritableStreams is neither copyable nor movable.
  WritableStreams(const WritableStreams&) = delete;
  WritableStreams& operator=(const WritableStreams&) = delete;
  WritableStreams(WritableStreams&&) = delete;
  WritableStreams& operator=(WritableStreams&&) = delete;

  // Enqueues a stream id with the given priority.
  // If this returns error, transport MUST be closed.
  absl::Status Enqueue(const StreamPtr stream,
                       const WritableStreamPriority priority) {
    // Streams waiting for transport flow control MUST not be added to list of
    // writable streams via this API, instead they MUST be added via
    // BlockedOnTransportFlowControl. The reason being there is no merit in
    // re-adding the stream to mpsc queue while it can be immediately enqueued
    // to the prioritized queue.
    GRPC_DCHECK(priority !=
                WritableStreamPriority::kWaitForTransportFlowControl);
    StatusFlag status = sender_.UnbufferedImmediateSend(
        StreamIDAndPriority{stream, priority}, /*tokens*/ 1);
    GRPC_WRITABLE_STREAMS_DEBUG
        << "UnbufferedImmediateEnqueue stream with priority "
        << GetWritableStreamPriorityString(priority) << " status " << status;

    return (status.ok())
               ? absl::OkStatus()
               : absl::InternalError(absl::StrCat(
                     "Failed to enqueue stream to list of writable streams "));
  }

  // A synchronous function to add a stream id to the transport flow control
  // wait list.
  absl::Status BlockedOnTransportFlowControl(const StreamPtr stream) {
    prioritized_queue_.Push(
        stream, WritableStreamPriority::kWaitForTransportFlowControl);
    GRPC_WRITABLE_STREAMS_DEBUG << "Enqueuing a stream with priority "
                                   "kWaitForTransportFlowControl ";
    return absl::OkStatus();
  }

  // Dequeues a single stream id from the queue.
  // Returns a promise that resolves to the next stream id or an error if the
  // dequeue fails. High level flow:
  // 1. Synchronous dequeue from the mpsc queue to get a batch of stream ids.
  // 2. If the batch is non-empty, the stream ids are pushed to the prioritized
  //    queue.
  // 3. If the prioritized queue is non-empty, the stream with the highest
  //    priority is popped. If there are multiple stream ids with the same
  //    priority, the stream enqueued first is popped first.
  // 4. If the prioritized queue is empty, the mpsc queue is queried again for
  //    a batch of stream ids. If the mpsc queue is empty, we block until a
  //    stream id is enqueued.
  // 5. Once mpsc dequeue promise is resolved, the stream ids are pushed to the
  //    prioritized queue.
  // 6. Return the stream id with the highest priority.
  // If this returns error, transport MUST be closed.
  // TODO(akshitpatel) : [PH2][P2] - This will be deprecated in favor of
  // WaitForReady.
  auto Next(const bool transport_tokens_available) {
    // TODO(akshitpatel) : [PH2][P2] - Need to add an immediate dequeue option
    // for the mpsc queue in favor of the race.

    return AssertResultType<absl::StatusOr<StreamPtr>>(TrySeq(
        // The current MPSC queue does not have a version of NextBatch that
        // resolves immediately. So we made this Race to ensure that the
        // "Dequeue" from the mpsc resolves immediately - Either with data , or
        // empty.
        Race(
            queue_.NextBatch(kMaxBatchSize),
            Immediate(
                ValueOrFailure<std::vector<std::optional<StreamIDAndPriority>>>(
                    std::vector<std::optional<StreamIDAndPriority>>()))),
        [this, transport_tokens_available](
            std::vector<std::optional<StreamIDAndPriority>> batch) {
          AddToPrioritizedQueue(batch);
          std::optional<StreamPtr> stream =
              prioritized_queue_.Pop(transport_tokens_available);
          return If(
              stream.has_value(),
              [stream]() -> absl::StatusOr<StreamPtr> {
                GRPC_WRITABLE_STREAMS_DEBUG << "Next stream id: "
                                            << (*stream)->GetStreamId();
                return stream.value();
              },
              [this, transport_tokens_available] {
                GRPC_WRITABLE_STREAMS_DEBUG << "Query queue for next batch";
                return Map(
                    // PrioritizedQueue is empty at this point. Hence we block
                    // on mpsc queue to get a new batch of stream ids.
                    queue_.NextBatch(kMaxBatchSize),
                    [this, transport_tokens_available](
                        ValueOrFailure<
                            std::vector<std::optional<StreamIDAndPriority>>>
                            batch) -> absl::StatusOr<StreamPtr> {
                      if (batch.ok()) {
                        GRPC_WRITABLE_STREAMS_DEBUG << "Next batch size "
                                                    << batch.value().size();
                        AddToPrioritizedQueue(batch.value());
                        std::optional<StreamPtr> stream =
                            prioritized_queue_.Pop(transport_tokens_available);
                        // TODO(akshitpatel) : [PH2][P4] - This GRPC_DCHECK
                        // should ideally be fine. But in case if
                        // queue_.NextBatch spuriously returns an empty batch,
                        // move to a Loop to avoid this.
                        GRPC_DCHECK(stream.has_value());
                        GRPC_WRITABLE_STREAMS_DEBUG
                            << "Next stream id: "
                            << stream.value()->GetStreamId();
                        return stream.value();
                      }
                      return absl::InternalError("Failed to read from queue");
                    });
              });
        }));
  }

  // Wait for a stream to be ready to be dequeued. This is a blocking call.
  // This returns a promise that resolves when there is a writeable stream ready
  // to be dequeued or ForceReadyForWrite() is called.
  auto WaitForReady(const bool transport_tokens_available) {
    return TrySeq(
        If(
            PrioritizedQueueHasWritableStreams(transport_tokens_available),
            [this]() {
              // TODO(akshitpatel) : [PH2][P3] - This is temporary. Replace with
              // native MPSC::ImmediateNextBatch.
              // We already have writable streams in the prioritized queue.
              // We check for any newly added streams to the un-prioritised
              // queue. We dequeue to honor the priority of any newly enqueued
              // streams.
              return Race(
                  queue_.NextBatch(kMaxBatchSize),
                  Immediate(ValueOrFailure<
                            std::vector<std::optional<StreamIDAndPriority>>>(
                      std::vector<std::optional<StreamIDAndPriority>>())));
            },
            // The prioritised queue is empty. So we wait for something to
            // enter the un-prioritised queue and then dequeue it.
            [this]() { return queue_.NextBatch(kMaxBatchSize); }),
        [this](std::vector<std::optional<StreamIDAndPriority>> batch) {
          AddToPrioritizedQueue(batch);
          return Empty{};
        });
  }

  // Synchronously drain the prioritized queue.
  std::optional<StreamPtr> ImmediateNext(
      const bool transport_tokens_available) {
    return prioritized_queue_.Pop(transport_tokens_available);
  }

  // Force resolve WaitForReady. This is used to induce a write cycle on the
  // transport.
  absl::Status ForceReadyForWrite() {
    StatusFlag status =
        sender_.UnbufferedImmediateSend(std::nullopt, /*tokens*/ 1);
    GRPC_WRITABLE_STREAMS_DEBUG << "ForceReadyForWrite status " << status;
    return (status.ok()) ? absl::OkStatus()
                         : absl::InternalError(
                               "Failed to enqueue to list of writable streams");
  }

  bool TestOnlyPriorityQueueHasWritableStreams(
      const bool transport_tokens_available) const {
    return !prioritized_queue_.HasNoWritableStreams(transport_tokens_available);
  }

 private:
  class PrioritizedQueue {
   public:
    PrioritizedQueue() : buckets_(kLastPriority) {}

    // Pushes a stream id with the given priority to the queue. Sorting is done
    // based on the priority. If the priority is higher than the max priority,
    // it will be set to the default priority.
    void Push(const StreamPtr stream, WritableStreamPriority priority) {
      if (priority >= WritableStreamPriority::kLastPriority) {
        priority = WritableStreamPriority::kDefault;
      }

      total_streams_++;
      GRPC_WRITABLE_STREAMS_DEBUG
          << "Pushing stream id: " << stream->GetStreamId() << " with priority "
          << GetWritableStreamPriorityString(priority) << " with total streams "
          << total_streams_;
      buckets_[static_cast<uint8_t>(priority)].push(stream);
    }

    // Pops a stream id from the queue based on the priority. If the priority is
    // kWaitForTransportFlowControl, transport_tokens_available is checked to
    // see if the stream id can be popped.
    std::optional<StreamPtr> Pop(const bool transport_tokens_available) {
      if (HasNoWritableStreams(transport_tokens_available)) {
        return std::nullopt;
      }
      for (uint8_t i = 0; i < buckets_.size(); ++i) {
        auto& bucket = buckets_[i];
        if (!bucket.empty()) {
          if (i == kWaitForTransportFlowControlIndex &&
              !transport_tokens_available) {
            GRPC_WRITABLE_STREAMS_DEBUG
                << "Transport tokens unavailable, skipping "
                   "transport flow control wait list";
            continue;
          }

          StreamPtr stream = bucket.front();
          bucket.pop();
          total_streams_--;
          GRPC_WRITABLE_STREAMS_DEBUG
              << "Popping stream id: " << stream->GetStreamId()
              << " from priority "
              << GetWritableStreamPriorityString(
                     static_cast<WritableStreamPriority>(i))
              << " with " << total_streams_ << " streams remaining";
          return stream;
        }
      }
      return std::nullopt;
    }

    // Returns true if the queue does not have any stream that can be popped.
    // If transport_tokens_available is false, streams with priority of
    // kWaitForTransportFlowControl are not considered.
    inline bool HasNoWritableStreams(
        const bool transport_tokens_available) const {
      return (transport_tokens_available)
                 ? (total_streams_ == 0)
                 : (total_streams_ -
                        buckets_[kWaitForTransportFlowControlIndex].size() ==
                    0);
    }

    static constexpr uint8_t kLastPriority =
        static_cast<uint8_t>(WritableStreamPriority::kLastPriority);
    static constexpr uint8_t kWaitForTransportFlowControlIndex =
        static_cast<uint8_t>(
            WritableStreamPriority::kWaitForTransportFlowControl);
    std::vector<std::queue<StreamPtr>> buckets_;
    uint32_t total_streams_ = 0u;
  };

  struct StreamIDAndPriority {
    const StreamPtr stream;
    const WritableStreamPriority priority;
  };

  void AddToPrioritizedQueue(
      const std::vector<std::optional<StreamIDAndPriority>>& batch) {
    GRPC_WRITABLE_STREAMS_DEBUG << "AddToPrioritizedQueue batch size "
                                << batch.size();
    for (const std::optional<StreamIDAndPriority>& stream_id_priority : batch) {
      // Ignore stream id kInvalidStreamID. These are used to force resolve
      // WaitForReady().
      if (!stream_id_priority.has_value()) {
        GRPC_WRITABLE_STREAMS_DEBUG << "Skipping nullopt from batch";
        continue;
      }
      prioritized_queue_.Push(stream_id_priority->stream,
                              stream_id_priority->priority);
    }
  }

  // Returns true if the prioritized queue has any stream that can be popped.
  bool PrioritizedQueueHasWritableStreams(
      const bool transport_tokens_available) const {
    GRPC_WRITABLE_STREAMS_DEBUG
        << "PrioritizedQueueHasWritableStreams "
        << !prioritized_queue_.HasNoWritableStreams(transport_tokens_available)
        << " transport_tokens_available " << transport_tokens_available;
    return !prioritized_queue_.HasNoWritableStreams(transport_tokens_available);
  }

  // TODO(akshitpatel) : [PH2][P4] - Verify if this works for large number of
  // active streams based on the load tests. The reasoning to use max uint32_t
  // is that even when the streams are dequeued from the queue, the streams
  // will only be marked as non-writable after stream data queue dequeue
  // happens. With this said, it should not matter whether the streams are
  // kept in mpsc queue or in the PriorityQueue. Additionally, having all
  // the writable streams in the PriorityQueue will return streams based on
  // a more recent enqueue snapshot.
  static constexpr uint32_t kMaxBatchSize =
      std::numeric_limits<uint32_t>::max();

  MpscReceiver<std::optional<StreamIDAndPriority>> queue_;
  MpscSender<std::optional<StreamIDAndPriority>> sender_;
  PrioritizedQueue prioritized_queue_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_WRITABLE_STREAMS_H
