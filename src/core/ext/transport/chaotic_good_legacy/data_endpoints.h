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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_LEGACY_DATA_ENDPOINTS_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_LEGACY_DATA_ENDPOINTS_H

#include <atomic>
#include <cstdint>

#include "src/core/channelz/property_list.h"
#include "src/core/ext/transport/chaotic_good_legacy/legacy_ztrace_collector.h"
#include "src/core/ext/transport/chaotic_good_legacy/pending_connection.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/telemetry/metrics.h"

namespace grpc_core {
namespace chaotic_good_legacy {

namespace data_endpoints_detail {

// Buffered writes for one data endpoint
class OutputBuffer {
 public:
  bool Accept(SliceBuffer& buffer);
  Waker TakeWaker() { return std::move(flush_waker_); }
  void SetWaker() {
    flush_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
  }
  bool HavePending() const { return pending_.Length() > 0; }
  SliceBuffer TakePending() { return std::move(pending_); }
  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList().Set("pending", pending_.Length());
  }

 private:
  Waker flush_waker_;
  size_t pending_max_ = 1024 * 1024;
  SliceBuffer pending_;
};

// The set of output buffers for all connected data endpoints
class OutputBuffers : public RefCounted<OutputBuffers> {
 public:
  explicit OutputBuffers(
      std::shared_ptr<LegacyZTraceCollector> ztrace_collector)
      : ztrace_collector_(std::move(ztrace_collector)) {}

  auto Write(SliceBuffer output_buffer) {
    return [output_buffer = std::move(output_buffer), this]() mutable {
      return PollWrite(output_buffer);
    };
  }

  auto Next(uint32_t connection_id) {
    return [this, connection_id]() { return PollNext(connection_id); };
  }

  void AddEndpoint(uint32_t connection_id);

  uint32_t ReadyEndpoints() const {
    return ready_endpoints_.load(std::memory_order_relaxed);
  }

  void AddData(channelz::DataSink sink) {
    MutexLock lock(&mu_);
    sink.AddData("output_buffers",
                 channelz::PropertyList()
                     .Set("ready_endpoints",
                          ready_endpoints_.load(std::memory_order_relaxed))
                     .Set("buffers", [this]() {
                       mu_.AssertHeld();
                       channelz::PropertyTable table;
                       for (const auto& buffer : buffers_) {
                         if (buffer.has_value()) {
                           table.AppendRow(channelz::PropertyList().Set(
                               "pending", buffer->HavePending()));
                         } else {
                           table.AppendRow(channelz::PropertyList().Set(
                               "pending", "no buffer"));
                         }
                       }
                       return table;
                     }()));
  }

  bool TraceWrite() {
    auto now = Timestamp::Now();
    if (now - last_traced_write_ > Duration::Milliseconds(100)) {
      last_traced_write_ = now;
      // We still only trace if there's a sink for the trace, but we only check
      // that every 100ms because the check can be expensive itself.
      return ztrace_collector_->IsActive();
    }
    return false;
  }

 private:
  Poll<uint32_t> PollWrite(SliceBuffer& output_buffer);
  Poll<SliceBuffer> PollNext(uint32_t connection_id);

  Mutex mu_;
  std::vector<std::optional<OutputBuffer>> buffers_ ABSL_GUARDED_BY(mu_);
  Waker write_waker_ ABSL_GUARDED_BY(mu_);
  std::atomic<uint32_t> ready_endpoints_{0};
  std::shared_ptr<LegacyZTraceCollector> ztrace_collector_;
  Timestamp last_traced_write_ = Timestamp::InfPast();
};

class InputQueues : public RefCounted<InputQueues> {
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
    ReadTicket(absl::StatusOr<uint64_t> ticket,
               RefCountedPtr<InputQueues> input_queues)
        : ticket_(std::move(ticket)), input_queues_(std::move(input_queues)) {}

    ReadTicket(const ReadTicket&) = delete;
    ReadTicket& operator=(const ReadTicket&) = delete;
    ReadTicket(ReadTicket&& other) noexcept
        : ticket_(std::move(other.ticket_)),
          input_queues_(std::move(other.input_queues_)) {}
    ReadTicket& operator=(ReadTicket&& other) noexcept {
      ticket_ = std::move(other.ticket_);
      input_queues_ = std::move(other.input_queues_);
      return *this;
    }

    ~ReadTicket() {
      if (input_queues_ != nullptr && ticket_.ok()) {
        input_queues_->CancelTicket(*ticket_);
      }
    }

    auto Await() {
      return If(
          ticket_.ok(),
          [&]() {
            return
                [ticket = *ticket_, input_queues = std::move(input_queues_)]() {
                  return input_queues->PollRead(ticket);
                };
          },
          [&]() {
            return Immediate(absl::StatusOr<SliceBuffer>(ticket_.status()));
          });
    }

   private:
    absl::StatusOr<uint64_t> ticket_;
    RefCountedPtr<InputQueues> input_queues_;
  };

  struct ReadRequest {
    size_t length;
    uint64_t ticket;

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const ReadRequest& req) {
      sink.Append(absl::StrCat("read#", req.ticket, ":", req.length, "b"));
    }
  };

  explicit InputQueues(std::shared_ptr<LegacyZTraceCollector> ztrace_collector);

  ReadTicket Read(uint32_t connection_id, size_t length) {
    return ReadTicket(CreateTicket(connection_id, length), Ref());
  }

  auto Next(uint32_t connection_id) {
    return [this, connection_id]() { return PollNext(connection_id); };
  }

  void CompleteRead(uint64_t ticket, absl::StatusOr<SliceBuffer> buffer);

  void CancelTicket(uint64_t ticket);

  void AddEndpoint(uint32_t connection_id);

  void AddData(channelz::DataSink sink) {
    MutexLock lock(&mu_);
    sink.AddData(
        "input_queues",
        channelz::PropertyList()
            .Set("outstanding_reads",
                 absl::StrJoin(outstanding_reads_, ",",
                               [](std::string* out, const auto& entry) {
                                 absl::StrAppend(out, entry.first);
                               }))
            .Set("read_requests", [this]() {
              mu_.AssertHeld();
              channelz::PropertyTable table;
              for (const auto& requests : read_requests_) {
                table.AppendRow(channelz::PropertyList().Set(
                    "tickets", absl::StrJoin(requests, ",")));
              }
              return table;
            }()));
  }

 private:
  using ReadState = std::variant<absl::StatusOr<SliceBuffer>, Waker>;

  absl::StatusOr<uint64_t> CreateTicket(uint32_t connection_id, size_t length);
  Poll<absl::StatusOr<SliceBuffer>> PollRead(uint64_t ticket);
  Poll<std::vector<ReadRequest>> PollNext(uint32_t connection_id);

  Mutex mu_;
  uint64_t next_ticket_id_ ABSL_GUARDED_BY(mu_) = 0;
  std::vector<std::vector<ReadRequest>> read_requests_ ABSL_GUARDED_BY(mu_);
  std::vector<Waker> read_request_waker_;
  absl::flat_hash_map<uint64_t, ReadState> outstanding_reads_
      ABSL_GUARDED_BY(mu_);
  std::shared_ptr<LegacyZTraceCollector> ztrace_collector_;
};

class Endpoint final {
 public:
  Endpoint(uint32_t id, RefCountedPtr<OutputBuffers> output_buffers,
           RefCountedPtr<InputQueues> input_queues,
           PendingConnection pending_connection, bool enable_tracing,
           grpc_event_engine::experimental::EventEngine* event_engine,
           std::shared_ptr<GlobalStatsPluginRegistry::StatsPluginGroup>
               stats_plugin_group,
           std::shared_ptr<LegacyZTraceCollector> ztrace_collector);

  void AddData(channelz::DataSink sink) {
    party_->ExportToChannelz(absl::StrCat("endpoint_party", id_), sink);
  }

 private:
  static auto WriteLoop(
      uint32_t id, RefCountedPtr<OutputBuffers> output_buffers,
      std::shared_ptr<PromiseEndpoint> endpoint,
      std::shared_ptr<LegacyZTraceCollector> ztrace_collector);
  static auto ReadLoop(uint32_t id, RefCountedPtr<InputQueues> input_queues,
                       std::shared_ptr<PromiseEndpoint> endpoint);

  const uint32_t id_;
  RefCountedPtr<Party> party_;
  std::shared_ptr<LegacyZTraceCollector> ztrace_collector_;
};

}  // namespace data_endpoints_detail

// Collection of data connections.
class DataEndpoints {
 public:
  using ReadTicket = data_endpoints_detail::InputQueues::ReadTicket;

  explicit DataEndpoints(
      std::vector<PendingConnection> endpoints,
      grpc_event_engine::experimental::EventEngine* event_engine,
      std::shared_ptr<GlobalStatsPluginRegistry::StatsPluginGroup>
          stats_plugin_group,
      bool enable_tracing,
      std::shared_ptr<LegacyZTraceCollector> ztrace_collector);

  // Try to queue output_buffer against a data endpoint.
  // Returns a promise that resolves to the data endpoint connection id
  // selected.
  // Connection ids returned by this class are 0 based (which is different
  // to how chaotic good communicates them on the wire - those are 1 based
  // to allow for the control channel identification)
  auto Write(SliceBuffer output_buffer) {
    return output_buffers_->Write(std::move(output_buffer));
  }

  ReadTicket Read(uint32_t connection_id, uint32_t length) {
    return input_queues_->Read(connection_id, length);
  }

  bool empty() const { return output_buffers_->ReadyEndpoints() == 0; }

  void AddData(channelz::DataSink sink) {
    output_buffers_->AddData(sink);
    input_queues_->AddData(sink);
    MutexLock lock(&mu_);
    for (auto& endpoint : endpoints_) {
      endpoint.AddData(sink);
    }
  }

 private:
  RefCountedPtr<data_endpoints_detail::OutputBuffers> output_buffers_;
  RefCountedPtr<data_endpoints_detail::InputQueues> input_queues_;
  Mutex mu_;
  std::vector<data_endpoints_detail::Endpoint> endpoints_ ABSL_GUARDED_BY(mu_);
};

}  // namespace chaotic_good_legacy
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_LEGACY_DATA_ENDPOINTS_H
