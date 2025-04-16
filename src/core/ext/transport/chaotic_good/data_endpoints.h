// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_DATA_ENDPOINTS_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_DATA_ENDPOINTS_H

#include <atomic>
#include <chrono>
#include <cstdint>

#include "src/core/ext/transport/chaotic_good/pending_connection.h"
#include "src/core/ext/transport/chaotic_good/transport_context.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/util/seq_bit_set.h"

namespace grpc_core {
namespace chaotic_good {

namespace data_endpoints_detail {

struct DataFrameHeader {
  enum { kFrameHeaderSize = 20 };
  uint64_t payload_tag;
  uint64_t send_timestamp;
  uint32_t payload_length;

  // Parses a frame header from a buffer of kFrameHeaderSize bytes. All
  // kFrameHeaderSize bytes are consumed.
  static absl::StatusOr<DataFrameHeader> Parse(const uint8_t* data);
  // Serializes a frame header into a buffer of kFrameHeaderSize bytes.
  void Serialize(uint8_t* data) const;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const DataFrameHeader& frame) {
    sink.Append(absl::StrCat("DataFrameHeader{payload_tag:", frame.payload_tag,
                             ",send_timestamp:", frame.send_timestamp,
                             ",payload_length:", frame.payload_length, "}"));
  }
};

class Clock {
 public:
  virtual uint64_t Now() = 0;

 protected:
  ~Clock() = default;
};

class SendRate {
 public:
  explicit SendRate(
      double initial_rate = 1.25 /*10 gigabits/sec in bytes/nanosec*/)
      : current_rate_(initial_rate) {}
  void StartSend(uint64_t current_time, uint64_t send_size) {
    CHECK_NE(current_time, 0u);
    send_start_time_ = current_time;
    send_size_ = send_size;
  }
  void MaybeCompleteSend(uint64_t current_time) {
    if (send_start_time_ == 0) return;
    if (current_time > send_start_time_) {
      const double rate = static_cast<double>(send_size_) /
                          static_cast<double>(current_time - send_start_time_);
      current_rate_ = 0.9 * current_rate_ + 0.1 * rate;
    }
    send_start_time_ = 0;
  }
  double DeliveryTime(uint64_t current_time, size_t bytes) {
    // start time relative to the current time for this send
    double start_time = 0.0;
    if (send_start_time_ != 0) {
      // Use integer subtraction to avoid rounding errors, getting everything
      // with a zero base of 'now' to maximize precision.
      // Since we have uint64_ts and want a signed double result we need to
      // care about argument ordering to get a valid result.
      const double send_start_time_relative_to_now =
          current_time > send_start_time_
              ? -static_cast<double>(current_time - send_start_time_)
              : static_cast<double>(send_start_time_ - current_time);
      const double predicted_end_time =
          send_start_time_relative_to_now + current_rate_ * send_size_;
      if (predicted_end_time > start_time) start_time = predicted_end_time;
    }
    return start_time + bytes / current_rate_;
  }

 private:
  uint64_t send_start_time_ = 0;
  uint64_t send_size_ = 0;
  double current_rate_;  // bytes per nanosecond
};

// Buffered writes for one data endpoint
class OutputBuffer {
 public:
  std::optional<double> DeliveryTime(uint64_t current_time, size_t bytes);
  SliceBuffer& pending() { return pending_; }
  Waker TakeWaker() { return std::move(flush_waker_); }
  void SetWaker() {
    flush_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
  }
  bool HavePending() const { return pending_.Length() > 0; }
  SliceBuffer TakePendingAndStartWrite(uint64_t current_time) {
    send_rate_.StartSend(current_time, pending_.Length());
    return std::move(pending_);
  }
  void MaybeCompleteSend(uint64_t current_time) {
    send_rate_.MaybeCompleteSend(current_time);
  }

 private:
  Waker flush_waker_;
  size_t pending_max_ = 64 * 1024 * 1024;
  SliceBuffer pending_;
  SendRate send_rate_;
};

// The set of output buffers for all connected data endpoints
class OutputBuffers : public RefCounted<OutputBuffers> {
 public:
  explicit OutputBuffers(Clock* clock) : clock_(clock) {}

  auto Write(uint64_t payload_tag, SliceBuffer output_buffer) {
    return [payload_tag, send_time = clock_->Now(),
            output_buffer = std::move(output_buffer), this]() mutable {
      return PollWrite(payload_tag, send_time, output_buffer);
    };
  }

  auto Next(uint32_t connection_id) {
    return [this, connection_id]() { return PollNext(connection_id); };
  }

  void AddEndpoint(uint32_t connection_id);

  uint32_t ReadyEndpoints() const {
    return ready_endpoints_.load(std::memory_order_relaxed);
  }

 private:
  Poll<Empty> PollWrite(uint64_t payload_tag, uint64_t send_time,
                        SliceBuffer& output_buffer);
  Poll<SliceBuffer> PollNext(uint32_t connection_id);

  Mutex mu_;
  std::vector<std::optional<OutputBuffer>> buffers_ ABSL_GUARDED_BY(mu_);
  Waker write_waker_ ABSL_GUARDED_BY(mu_);
  std::atomic<uint32_t> ready_endpoints_{0};
  Clock* const clock_;
};

class InputQueue : public RefCounted<InputQueue> {
 public:
  // One outstanding read.
  // ReadTickets get filed by read requests, and all tickets are fullfilled
  // by an endpoint.
  // A call may Await a ticket to get the bytes back later (or it may skip that
  // step - in which case the bytes are thrown away after reading).
  // This decoupling is necessary to ensure that cancelled reads by calls do not
  // cause data corruption for other calls.
  class ReadTicket {
   public:
    ReadTicket(ValueOrFailure<uint64_t> payload_tag,
               RefCountedPtr<InputQueue> input_queues)
        : payload_tag_(payload_tag), input_queues_(std::move(input_queues)) {}

    ReadTicket(const ReadTicket&) = delete;
    ReadTicket& operator=(const ReadTicket&) = delete;
    ReadTicket(ReadTicket&& other) noexcept
        : payload_tag_(std::exchange(other.payload_tag_, 0)),
          input_queues_(std::move(other.input_queues_)) {}
    ReadTicket& operator=(ReadTicket&& other) noexcept {
      payload_tag_ = std::exchange(other.payload_tag_, 0);
      input_queues_ = std::move(other.input_queues_);
      return *this;
    }

    ~ReadTicket() {
      if (input_queues_ != nullptr && payload_tag_.ok() && *payload_tag_ != 0) {
        input_queues_->Cancel(*payload_tag_);
      }
    }

    auto Await() {
      return If(
          payload_tag_.ok(),
          [this]() {
            return [input_queues = input_queues_,
                    payload_tag = std::exchange(*payload_tag_, 0)]() {
              return input_queues->PollRead(payload_tag);
            };
          },
          []() {
            return []() -> absl::StatusOr<SliceBuffer> {
              return absl::InternalError("Duplicate read of tagged payload");
            };
          });
    }

   private:
    ValueOrFailure<uint64_t> payload_tag_;
    RefCountedPtr<InputQueue> input_queues_;
  };

  InputQueue() {
    read_requested_.Set(0);
    read_completed_.Set(0);
  }

  ReadTicket Read(uint64_t payload_tag);
  void CompleteRead(uint64_t payload_tag, absl::StatusOr<SliceBuffer> buffer);
  void Cancel(uint64_t payload_tag);

 private:
  struct Cancelled {};

  Poll<absl::StatusOr<SliceBuffer>> PollRead(uint64_t payload_tag);

  Mutex mu_;
  SeqBitSet read_requested_ ABSL_GUARDED_BY(mu_);
  SeqBitSet read_completed_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<uint64_t, Waker> read_wakers_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<uint64_t, absl::StatusOr<SliceBuffer>> read_buffers_
      ABSL_GUARDED_BY(mu_);
};

class Endpoint final {
 public:
  Endpoint(uint32_t id, RefCountedPtr<OutputBuffers> output_buffers,
           RefCountedPtr<InputQueue> input_queues,
           PendingConnection pending_connection, bool enable_tracing,
           TransportContextPtr ctx);

 private:
  static auto WriteLoop(uint32_t id,
                        RefCountedPtr<OutputBuffers> output_buffers,
                        std::shared_ptr<PromiseEndpoint> endpoint);
  static auto ReadLoop(uint32_t id, RefCountedPtr<InputQueue> input_queues,
                       std::shared_ptr<PromiseEndpoint> endpoint);

  RefCountedPtr<Party> party_;
};

}  // namespace data_endpoints_detail

// Collection of data connections.
class DataEndpoints {
 public:
  using ReadTicket = data_endpoints_detail::InputQueue::ReadTicket;

  explicit DataEndpoints(std::vector<PendingConnection> endpoints,
                         TransportContextPtr ctx, bool enable_tracing,
                         data_endpoints_detail::Clock* clock = DefaultClock());

  // Try to queue output_buffer against a data endpoint.
  // Returns a promise that resolves to the data endpoint connection id
  // selected.
  // Connection ids returned by this class are 0 based (which is different
  // to how chaotic good communicates them on the wire - those are 1 based
  // to allow for the control channel identification)
  auto Write(uint64_t tag, SliceBuffer output_buffer) {
    return output_buffers_->Write(tag, std::move(output_buffer));
  }

  ReadTicket Read(uint64_t tag) { return input_queues_->Read(tag); }

  bool empty() const { return output_buffers_->ReadyEndpoints() == 0; }

 private:
  static data_endpoints_detail::Clock* DefaultClock() {
    class ClockImpl final : public data_endpoints_detail::Clock {
     public:
      uint64_t Now() override {
        return std::chrono::steady_clock::now().time_since_epoch().count();
      }
    };
    static ClockImpl clock;
    return &clock;
  }

  RefCountedPtr<data_endpoints_detail::OutputBuffers> output_buffers_;
  RefCountedPtr<data_endpoints_detail::InputQueue> input_queues_;
  Mutex mu_;
  std::vector<data_endpoints_detail::Endpoint> endpoints_ ABSL_GUARDED_BY(mu_);
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_DATA_ENDPOINTS_H
