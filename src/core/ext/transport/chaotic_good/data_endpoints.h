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
#include <cstddef>
#include <cstdint>
#include <memory>

#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chaotic_good/pending_connection.h"
#include "src/core/ext/transport/chaotic_good/tcp_ztrace_collector.h"
#include "src/core/ext/transport/chaotic_good/transport_context.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/util/seq_bit_set.h"

namespace grpc_core {
namespace chaotic_good {

namespace data_endpoints_detail {

class Clock {
 public:
  virtual uint64_t Now() = 0;

 protected:
  ~Clock() = default;
};

class SendRate {
 public:
  explicit SendRate(
      double initial_rate = 0 /* <=0 ==> not set, bytes per nanosecond */)
      : current_rate_(initial_rate) {}
  void StartSend(uint64_t current_time, uint64_t send_size);
  void MaybeCompleteSend(uint64_t current_time);
  void SetCurrentRate(double bytes_per_nanosecond);
  bool IsRateMeasurementStale() const;
  // Returns double nanoseconds from now.
  LbDecision GetLbDecision(uint64_t current_time, size_t bytes);
  void AddData(Json::Object& obj) const;
  void PerformRateProbe() { last_rate_measurement_ = Timestamp::Now(); }

 private:
  uint64_t send_start_time_ = 0;
  uint64_t send_size_ = 0;
  double current_rate_;  // bytes per nanosecond
  Timestamp last_rate_measurement_ = Timestamp::ProcessEpoch();
};

struct NextWrite {
  SliceBuffer bytes;
  bool trace;
};

// Buffered writes for one data endpoint
class OutputBuffer {
 public:
  LbDecision GetLbDecision(uint64_t current_time, size_t bytes);
  SliceBuffer& pending() { return pending_; }
  Waker TakeWaker() { return std::move(flush_waker_); }
  void SetWaker() {
    flush_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
  }
  bool HavePending() const { return pending_.Length() > 0; }
  NextWrite TakePendingAndStartWrite(uint64_t current_time);
  void MaybeCompleteSend(uint64_t current_time);
  void UpdateSendRate(double bytes_per_nanosecond) {
    send_rate_.SetCurrentRate(bytes_per_nanosecond);
  }

  void AddData(Json::Object& obj) const;

 private:
  Waker flush_waker_;
  SliceBuffer pending_;
  SendRate send_rate_;
};

// The set of output buffers for all connected data endpoints
class OutputBuffers final : public RefCounted<OutputBuffers>,
                            public channelz::DataSource {
 public:
  OutputBuffers(Clock* clock, uint32_t encode_alignment,
                std::shared_ptr<TcpZTraceCollector> ztrace_collector,
                TransportContextPtr ctx)
      : channelz::DataSource(ctx->socket_node),
        ztrace_collector_(std::move(ztrace_collector)),
        encode_alignment_(encode_alignment),
        clock_(clock) {}
  ~OutputBuffers() override { ResetDataSource(); }

  void AddData(channelz::DataSink& sink) override;

  auto Write(uint64_t payload_tag, SliceBuffer output_buffer,
             std::shared_ptr<TcpCallTracer> call_tracer) {
    return [payload_tag, send_time = clock_->Now(),
            output_buffer = std::move(output_buffer),
            call_tracer = std::move(call_tracer), this]() mutable {
      return PollWrite(payload_tag, send_time, output_buffer, call_tracer);
    };
  }

  void WriteSecurityFrame(uint32_t connection_id, SliceBuffer output_buffer);

  auto Next(uint32_t connection_id) {
    return [this, connection_id]() { return PollNext(connection_id); };
  }

  void AddEndpoint(uint32_t connection_id);

  uint32_t ReadyEndpoints() const {
    return ready_endpoints_.load(std::memory_order_relaxed);
  }

  void UpdateSendRate(uint32_t connection_id, double bytes_per_nanosecond);

 private:
  Poll<Empty> PollWrite(uint64_t payload_tag, uint64_t send_time,
                        SliceBuffer& output_buffer,
                        std::shared_ptr<TcpCallTracer>& call_tracer);
  Poll<NextWrite> PollNext(uint32_t connection_id);
  void UpdateMetrics(size_t output_buffer, const TcpConnectionMetrics& metrics);

  const std::shared_ptr<TcpZTraceCollector> ztrace_collector_;
  Mutex mu_;
  std::vector<std::optional<OutputBuffer>> buffers_ ABSL_GUARDED_BY(mu_);
  Waker write_waker_ ABSL_GUARDED_BY(mu_);
  std::atomic<uint32_t> ready_endpoints_{0};
  const uint32_t encode_alignment_;
  Clock* const clock_;
};

class InputQueue final : public RefCounted<InputQueue>, channelz::DataSource {
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

  explicit InputQueue(TransportContextPtr ctx)
      : channelz::DataSource(ctx->socket_node) {
    read_requested_.Set(0);
    read_completed_.Set(0);
  }
  ~InputQueue() override { ResetDataSource(); }

  ReadTicket Read(uint64_t payload_tag);
  void CompleteRead(uint64_t payload_tag, SliceBuffer buffer);
  void Cancel(uint64_t payload_tag);

  void AddData(channelz::DataSink& sink) override;

  void SetClosed(absl::Status status);
  auto AwaitClosed() {
    return [this]() -> Poll<absl::Status> {
      MutexLock lock(&mu_);
      if (closed_error_.ok()) {
        await_closed_ = GetContext<Activity>()->MakeNonOwningWaker();
        return Pending{};
      }
      return closed_error_;
    };
  }

 private:
  struct Cancelled {};

  Poll<absl::StatusOr<SliceBuffer>> PollRead(uint64_t payload_tag);

  Mutex mu_;
  SeqBitSet read_requested_ ABSL_GUARDED_BY(mu_);
  SeqBitSet read_completed_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<uint64_t, Waker> read_wakers_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<uint64_t, SliceBuffer> read_buffers_ ABSL_GUARDED_BY(mu_);
  absl::Status closed_error_ ABSL_GUARDED_BY(mu_);
  Waker await_closed_ ABSL_GUARDED_BY(mu_);
};

class Endpoint final {
 public:
  Endpoint(uint32_t id, uint32_t decode_alignment,
           RefCountedPtr<OutputBuffers> output_buffers,
           RefCountedPtr<InputQueue> input_queues,
           PendingConnection pending_connection, bool enable_tracing,
           TransportContextPtr ctx,
           std::shared_ptr<TcpZTraceCollector> ztrace_collector);
  Endpoint(const Endpoint&) = delete;
  Endpoint& operator=(const Endpoint&) = delete;
  Endpoint(Endpoint&&) = delete;
  Endpoint& operator=(Endpoint&&) = delete;
  ~Endpoint() { ztrace_collector_->Append(EndpointCloseTrace{id_}); }

 private:
  static auto WriteLoop(uint32_t id,
                        RefCountedPtr<OutputBuffers> output_buffers,
                        std::shared_ptr<PromiseEndpoint> endpoint,
                        std::shared_ptr<TcpZTraceCollector> ztrace_collector);
  static auto ReadLoop(uint32_t id, uint32_t decode_alignment,
                       RefCountedPtr<InputQueue> input_queues,
                       std::shared_ptr<PromiseEndpoint> endpoint,
                       std::shared_ptr<TcpZTraceCollector> ztrace_collector);
  static void ReceiveSecurityFrame(PromiseEndpoint& endpoint,
                                   SliceBuffer buffer);

  const std::shared_ptr<TcpZTraceCollector> ztrace_collector_;
  const uint32_t id_;
  RefCountedPtr<Party> party_;
};

}  // namespace data_endpoints_detail

// Collection of data connections.
class DataEndpoints {
 public:
  using ReadTicket = data_endpoints_detail::InputQueue::ReadTicket;

  explicit DataEndpoints(std::vector<PendingConnection> endpoints,
                         TransportContextPtr ctx, uint32_t encode_alignment,
                         uint32_t decode_alignment,
                         std::shared_ptr<TcpZTraceCollector> ztrace_collector,
                         bool enable_tracing,
                         data_endpoints_detail::Clock* clock = DefaultClock());

  // Try to queue output_buffer against a data endpoint.
  // Returns a promise that resolves to the data endpoint connection id
  // selected.
  // Connection ids returned by this class are 0 based (which is different
  // to how chaotic good communicates them on the wire - those are 1 based
  // to allow for the control channel identification)
  auto Write(uint64_t tag, SliceBuffer output_buffer,
             std::shared_ptr<TcpCallTracer> call_tracer) {
    return output_buffers_->Write(tag, std::move(output_buffer),
                                  std::move(call_tracer));
  }

  ReadTicket Read(uint64_t tag) { return input_queues_->Read(tag); }

  auto AwaitClosed() { return input_queues_->AwaitClosed(); }

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
  std::vector<std::unique_ptr<data_endpoints_detail::Endpoint>> endpoints_
      ABSL_GUARDED_BY(mu_);
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_DATA_ENDPOINTS_H
