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
#include <queue>

#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chaotic_good/frame_transport.h"
#include "src/core/ext/transport/chaotic_good/pending_connection.h"
#include "src/core/ext/transport/chaotic_good/scheduler.h"
#include "src/core/ext/transport/chaotic_good/tcp_ztrace_collector.h"
#include "src/core/ext/transport/chaotic_good/transport_context.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"
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

  struct NetworkSend {
    uint64_t start_time;
    uint64_t bytes;
  };
  struct NetworkMetrics {
    std::optional<uint64_t> rtt_usec;
    std::optional<double> bytes_per_nanosecond;
  };
  void StartSend(uint64_t bytes) { last_send_bytes_outstanding_ += bytes; }
  void SetNetworkMetrics(const std::optional<NetworkSend>& network_send,
                         const NetworkMetrics& metrics);
  bool IsRateMeasurementStale() const;
  void AddData(Json::Object& obj) const;
  void PerformRateProbe() { last_rate_measurement_ = Timestamp::Now(); }

  struct DeliveryData {
    // Time in seconds of the time that a byte sent now would be received at the
    // peer.
    double start_time;
    // The rate of bytes per second that a channel is expected to send.
    double bytes_per_second;
  };
  DeliveryData GetDeliveryData(uint64_t current_time) const;

 private:
  uint64_t last_send_started_time_ = 0;
  uint64_t last_send_bytes_outstanding_ = 0;
  double current_rate_;      // bytes per nanosecond
  uint64_t rtt_usec_ = 0.0;  // nanoseconds
  Timestamp last_rate_measurement_ = Timestamp::ProcessEpoch();
};

// The set of output buffers for all connected data endpoints
class OutputBuffers final
    : public DualRefCounted<OutputBuffers, NonPolymorphicRefCount> {
 public:
  OutputBuffers(Clock* clock, uint32_t encode_alignment,
                std::shared_ptr<TcpZTraceCollector> ztrace_collector,
                std::string scheduler_config, TransportContextPtr ctx)
      : encode_alignment_(encode_alignment),
        clock_(clock),
        ztrace_collector_(std::move(ztrace_collector)),
        ctx_(std::move(ctx)),
        scheduling_party_(Party::Make(arena_)),
        scheduler_(MakeScheduler(std::move(scheduler_config))) {
    scheduling_party_->Spawn(
        "output-buffers-scheduler",
        [self = WeakRef()]() mutable {
          return Loop([self]() {
            return Seq([self]() { return self->SchedulerPollForWork(); },
                       [self]() -> LoopCtl<absl::Status> {
                         self->Schedule();
                         return Continue{};
                       });
          });
        },
        [](absl::Status) {});
  }

  ~OutputBuffers() {
    auto scheduling_state = scheduling_state_.load(std::memory_order_acquire);
    switch (scheduling_state) {
      case kSchedulingProcessing:
      case kSchedulingWorkAvailable:
        break;
      default:
        delete reinterpret_cast<Waker*>(scheduling_state);
        break;
    }
  }

  void Orphaned() { scheduling_party_.reset(); }

  struct QueuedFrame final {
    uint64_t payload_tag;
    MpscQueued<OutgoingFrame> frame;
  };

  class Reader final : public RefCounted<Reader, NonPolymorphicRefCount> {
   public:
    // Don't call directly: use MakeReader instead.
    explicit Reader(RefCountedPtr<OutputBuffers> output_buffers, uint32_t id)
        : output_buffers_(std::move(output_buffers)), id_(id) {}
    ~Reader() { CHECK(dropped_); }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    auto Next() { return NextPromise(this); }
    uint32_t id() const { return id_; }
    void SetNetworkMetrics(
        const std::optional<SendRate::NetworkSend>& network_send,
        const SendRate::NetworkMetrics& metrics);
    Json::Object ToJson();
    void Drop() {
      CHECK(!dropped_);
      dropped_ = true;
      output_buffers_->DestroyReader(id_);
    }

   private:
    friend class OutputBuffers;

    class NextPromise {
     public:
      explicit NextPromise(Reader* reader) : reader_(reader) {}

      ~NextPromise() {
        if (reader_ != nullptr) {
          reader_->EndReadNext();
        }
      }

      NextPromise(const NextPromise&) = delete;
      NextPromise& operator=(const NextPromise&) = delete;
      NextPromise(NextPromise&& other)
          : reader_(std::exchange(other.reader_, nullptr)) {}
      NextPromise& operator=(NextPromise&& other) {
        std::swap(reader_, other.reader_);
        return *this;
      }

      Poll<std::vector<QueuedFrame>> operator()() {
        auto r = reader_->PollReadNext();
        if (r.ready()) reader_ = nullptr;
        return r;
      }

     private:
      Reader* reader_;
    };

    void EndReadNext();
    Poll<std::vector<QueuedFrame>> PollReadNext();

    const RefCountedPtr<OutputBuffers> output_buffers_;
    const uint32_t id_;

    Mutex mu_;
    bool reading_ ABSL_GUARDED_BY(mu_) = false;
    bool dropped_{false};
    SendRate send_rate_ ABSL_GUARDED_BY(mu_);
    Waker waker_ ABSL_GUARDED_BY(mu_);
    std::vector<QueuedFrame> frames_ ABSL_GUARDED_BY(mu_);
  };

  void AddData(channelz::DataSink sink);

  void Write(uint64_t payload_tag, MpscQueued<OutgoingFrame> output_buffer);

  size_t ReadyEndpoints() const {
    return num_readers_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] RefCountedPtr<Reader> MakeReader(uint32_t id)
      ABSL_LOCKS_EXCLUDED(mu_reader_data_);

  void SetMpscProbe(MpscProbe<OutgoingFrame> probe) {
    MutexLock lock(&mu_reader_data_);
    mpsc_probe_ = std::move(probe);
  }

 private:
  struct SchedulingData {
    explicit SchedulingData(RefCountedPtr<Reader> reader)
        : reader(std::move(reader)) {}
    RefCountedPtr<Reader> reader;
    std::vector<QueuedFrame> frames;
    uint64_t queued_bytes = 0;
  };

  static constexpr uintptr_t kSchedulingWorkAvailable = 1;
  static constexpr uintptr_t kSchedulingProcessing = 2;

  void DestroyReader(uint32_t id) ABSL_LOCKS_EXCLUDED(mu_reader_data_);

  void WakeupScheduler();
  Poll<Empty> SchedulerPollForWork();
  void Schedule() ABSL_LOCKS_EXCLUDED(mu_reader_data_);

  uint64_t WriteSizeForFrame(const QueuedFrame& queued_frame) {
    auto& frame =
        absl::ConvertVariantTo<FrameInterface&>(queued_frame.frame->payload);
    const auto hdr = frame.MakeHeader();
    const size_t length = hdr.payload_length;
    return TcpDataFrameHeader::kFrameHeaderSize +
           DataConnectionPadding(TcpDataFrameHeader::kFrameHeaderSize,
                                 encode_alignment_) +
           length + DataConnectionPadding(length, encode_alignment_);
  }

  std::atomic<size_t> num_readers_ = 0;
  Mutex mu_reader_data_;
  MpscProbe<OutgoingFrame> mpsc_probe_ ABSL_GUARDED_BY(mu_reader_data_);
  std::vector<RefCountedPtr<Reader>> readers_ ABSL_GUARDED_BY(mu_reader_data_);
  const uint32_t encode_alignment_;
  Clock* const clock_;
  const std::shared_ptr<TcpZTraceCollector> ztrace_collector_;
  TransportContextPtr ctx_;
  RefCountedPtr<Arena> arena_ = [ctx = ctx_]() {
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        ctx->event_engine.get());
    return arena;
  }();
  // Must be held to push into big_frames_queue_ or small_frames_queue_.
  Mutex mu_write_;
  ArenaSpsc<QueuedFrame, false> frames_queue_{arena_.get()};
  std::atomic<uintptr_t> scheduling_state_{kSchedulingProcessing};
  RefCountedPtr<Party> scheduling_party_;
  const std::unique_ptr<Scheduler> scheduler_;
};

class InputQueue final : public RefCounted<InputQueue> {
 private:
  struct Completion : public RefCounted<Completion, NonPolymorphicRefCount> {
    Completion(uint64_t payload_tag, absl::StatusOr<SliceBuffer> result)
        : payload_tag(payload_tag), result(std::move(result)), ready(true) {}
    Completion(uint64_t payload_tag) : payload_tag(payload_tag), ready(false) {}
    Mutex mu;
    const uint64_t payload_tag;
    absl::StatusOr<SliceBuffer> result ABSL_GUARDED_BY(mu);
    bool ready ABSL_GUARDED_BY(mu);
    Waker waker ABSL_GUARDED_BY(mu);
  };

 public:
  // One outstanding read.
  // ReadTickets get filed by read requests, and all tickets are fulfilled
  // by an endpoint.
  // A call may Await a ticket to get the bytes back later (or it may skip that
  // step - in which case the bytes are thrown away after reading).
  // This decoupling is necessary to ensure that cancelled reads by calls do not
  // cause data corruption for other calls.
  class ReadTicket {
   public:
    ReadTicket(RefCountedPtr<Completion> completion,
               RefCountedPtr<InputQueue> input_queues)
        : completion_(std::move(completion)),
          input_queues_(std::move(input_queues)) {}

    ReadTicket(const ReadTicket&) = delete;
    ReadTicket& operator=(const ReadTicket&) = delete;
    ReadTicket(ReadTicket&& other) noexcept
        : completion_(std::move(other.completion_)),
          input_queues_(std::move(other.input_queues_)) {}
    ReadTicket& operator=(ReadTicket&& other) noexcept {
      completion_ = std::move(other.completion_);
      input_queues_ = std::move(other.input_queues_);
      return *this;
    }

    ~ReadTicket() {
      if (input_queues_ != nullptr) {
        completion_->mu.Lock();
        if (!completion_->ready) {
          completion_->mu.Unlock();
          input_queues_->Cancel(completion_.get());
        } else {
          completion_->mu.Unlock();
        }
      }
    }

    auto Await() {
      class AwaitPromise {
       public:
        AwaitPromise(RefCountedPtr<Completion> completion,
                     RefCountedPtr<InputQueue> input_queues)
            : completion_(std::move(completion)),
              input_queues_(std::move(input_queues)) {}

        ~AwaitPromise() {
          if (input_queues_ != nullptr) {
            completion_->mu.Lock();
            if (!completion_->ready) {
              completion_->mu.Unlock();
              input_queues_->Cancel(completion_.get());
            } else {
              completion_->mu.Unlock();
            }
          }
        }

        AwaitPromise(const AwaitPromise&) = delete;
        AwaitPromise& operator=(const AwaitPromise&) = delete;
        AwaitPromise(AwaitPromise&& other) noexcept
            : completion_(std::move(other.completion_)),
              input_queues_(std::move(other.input_queues_)) {}
        AwaitPromise& operator=(AwaitPromise&& other) noexcept {
          completion_ = std::move(other.completion_);
          input_queues_ = std::move(other.input_queues_);
          return *this;
        }

        Poll<absl::StatusOr<SliceBuffer>> operator()() {
          DCHECK(completion_ != nullptr);
          completion_->mu.Lock();
          if (completion_->ready) {
            auto result = std::move(completion_->result);
            completion_->mu.Unlock();
            input_queues_.reset();
            return std::move(result);
          }
          completion_->waker = GetContext<Activity>()->MakeNonOwningWaker();
          completion_->mu.Unlock();
          return Pending{};
        }

       private:
        RefCountedPtr<Completion> completion_;
        RefCountedPtr<InputQueue> input_queues_;
      };
      return AwaitPromise(std::move(completion_), std::move(input_queues_));
    }

   private:
    RefCountedPtr<Completion> completion_;
    RefCountedPtr<InputQueue> input_queues_;
  };

  explicit InputQueue(TransportContextPtr ctx) {
    read_requested_.Set(0);
    read_completed_.Set(0);
  }

  ReadTicket Read(uint64_t payload_tag);
  void CompleteRead(uint64_t payload_tag, SliceBuffer buffer);
  void Cancel(Completion* completion);

  void AddData(channelz::DataSink sink);

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

  Mutex mu_;
  SeqBitSet read_requested_ ABSL_GUARDED_BY(mu_);
  SeqBitSet read_completed_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<uint64_t, RefCountedPtr<Completion>> completions_
      ABSL_GUARDED_BY(mu_);
  absl::Status closed_error_ ABSL_GUARDED_BY(mu_);
  Waker await_closed_ ABSL_GUARDED_BY(mu_);
};

class SecureFrameQueue
    : public RefCounted<SecureFrameQueue, NonPolymorphicRefCount> {
 public:
  explicit SecureFrameQueue(uint32_t encode_alignment)
      : encode_alignment_(encode_alignment) {}

  void Write(SliceBuffer buffer);

  auto Next() {
    return [this]() -> Poll<SliceBuffer> {
      MutexLock lock(&mu_);
      if (all_frames_.Length() == 0) {
        read_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
        return Pending{};
      }
      SliceBuffer buffer = std::move(all_frames_);
      all_frames_.Clear();
      return buffer;
    };
  }

  size_t InstantaneousQueuedBytes() {
    MutexLock lock(&mu_);
    return all_frames_.Length();
  }

 private:
  Mutex mu_;
  const uint32_t encode_alignment_;
  SliceBuffer all_frames_ ABSL_GUARDED_BY(mu_);
  Waker read_waker_ ABSL_GUARDED_BY(mu_);
};

class Endpoint final {
 public:
  Endpoint(uint32_t id, uint32_t encode_alignment, uint32_t decode_alignment,
           Clock* clock, RefCountedPtr<OutputBuffers> output_buffers,
           RefCountedPtr<InputQueue> input_queues,
           PendingConnection pending_connection, bool enable_tracing,
           TransportContextPtr ctx,
           std::shared_ptr<TcpZTraceCollector> ztrace_collector);
  Endpoint(const Endpoint&) = delete;
  Endpoint& operator=(const Endpoint&) = delete;
  Endpoint(Endpoint&&) = delete;
  Endpoint& operator=(Endpoint&&) = delete;
  ~Endpoint() {
    ctx_->ztrace_collector->Append(EndpointCloseTrace{ctx_->id});
    ctx_->reader->Drop();
  }

  void ToJson(absl::AnyInvocable<void(Json::Object)> sink);

 private:
  struct EndpointContext : public RefCounted<EndpointContext> {
    uint32_t id;
    uint32_t encode_alignment;
    uint32_t decode_alignment;
    bool enable_tracing;
    // TODO(ctiller): Inline members into EndpointContext.
    RefCountedPtr<OutputBuffers> output_buffers;
    RefCountedPtr<InputQueue> input_queues;
    RefCountedPtr<SecureFrameQueue> secure_frame_queue;
    std::shared_ptr<PromiseEndpoint> endpoint;
    std::shared_ptr<TcpZTraceCollector> ztrace_collector;
    TransportContextPtr transport_ctx;
    RefCountedPtr<Arena> arena;
    Clock* clock;
    RefCountedPtr<OutputBuffers::Reader> reader;
    Timestamp last_metrics_update = Timestamp::ProcessEpoch();
  };

  static auto PullDataPayload(RefCountedPtr<EndpointContext> ctx);
  static auto WriteLoop(RefCountedPtr<EndpointContext> ctx);
  static auto ReadLoop(RefCountedPtr<EndpointContext> ctx);
  static void ReceiveSecurityFrame(PromiseEndpoint& endpoint,
                                   SliceBuffer buffer);
  RefCountedPtr<EndpointContext> ctx_;
  RefCountedPtr<Party> party_;
};

}  // namespace data_endpoints_detail

// Collection of data connections.
class DataEndpoints final : public channelz::DataSource {
 public:
  using ReadTicket = data_endpoints_detail::InputQueue::ReadTicket;

  explicit DataEndpoints(std::vector<PendingConnection> endpoints,
                         TransportContextPtr ctx, uint32_t encode_alignment,
                         uint32_t decode_alignment,
                         std::shared_ptr<TcpZTraceCollector> ztrace_collector,
                         bool enable_tracing, std::string scheduler_config,
                         data_endpoints_detail::Clock* clock = DefaultClock());
  ~DataEndpoints() { ResetDataSource(); }

  void AddData(channelz::DataSink sink) override;

  // Try to queue output_buffer against a data endpoint.
  // Returns a promise that resolves to the data endpoint connection id
  // selected.
  // Connection ids returned by this class are 0 based (which is different
  // to how chaotic good communicates them on the wire - those are 1 based
  // to allow for the control channel identification)
  void Write(uint64_t tag, MpscQueued<OutgoingFrame> output_buffer) {
    output_buffers_->Write(tag, std::move(output_buffer));
  }

  ReadTicket Read(uint64_t tag) { return input_queues_->Read(tag); }

  auto AwaitClosed() { return input_queues_->AwaitClosed(); }

  bool empty() const { return output_buffers_->ReadyEndpoints() == 0; }

  void SetMpscProbe(MpscProbe<OutgoingFrame> probe) {
    output_buffers_->SetMpscProbe(std::move(probe));
  }

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
