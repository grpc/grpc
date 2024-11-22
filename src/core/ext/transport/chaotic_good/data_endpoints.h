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

#include <cstdint>

#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

namespace data_endpoints_detail {
struct Endpoints : public RefCounted<Endpoints> {
  std::vector<PromiseEndpoint> endpoints;
};

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

 private:
  Waker flush_waker_;
  size_t pending_max_ = 1024 * 1024;
  SliceBuffer pending_;
};

// The set of output buffers for all connected data endpoints
class OutputBuffers : public RefCounted<OutputBuffers> {
 public:
  explicit OutputBuffers(uint32_t num_connections);

  auto Write(SliceBuffer output_buffer) {
    return [output_buffer = std::move(output_buffer), this]() mutable {
      return PollWrite(output_buffer);
    };
  }

  auto Next(uint32_t connection_id) {
    return [this, connection_id]() { return PollNext(connection_id); };
  }

 private:
  Poll<uint32_t> PollWrite(SliceBuffer& output_buffer);
  Poll<SliceBuffer> PollNext(uint32_t connection_id);

  Mutex mu_;
  std::vector<OutputBuffer> buffers_ ABSL_GUARDED_BY(mu_);
  Waker write_waker_ ABSL_GUARDED_BY(mu_);
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

  explicit InputQueues(uint32_t num_connections);

  ReadTicket Read(uint32_t connection_id, size_t length) {
    return ReadTicket(CreateTicket(connection_id, length), Ref());
  }

  auto Next(uint32_t connection_id) {
    return [this, connection_id]() { return PollNext(connection_id); };
  }

  void CompleteRead(uint64_t ticket, absl::StatusOr<SliceBuffer> buffer);

  void CancelTicket(uint64_t ticket);

 private:
  using ReadState = absl::variant<absl::StatusOr<SliceBuffer>, Waker>;

  absl::StatusOr<uint64_t> CreateTicket(uint32_t connection_id, size_t length);
  Poll<absl::StatusOr<SliceBuffer>> PollRead(uint64_t ticket);
  Poll<std::vector<ReadRequest>> PollNext(uint32_t connection_id);

  Mutex mu_;
  uint64_t next_ticket_id_ ABSL_GUARDED_BY(mu_) = 0;
  std::vector<std::vector<ReadRequest>> read_requests_ ABSL_GUARDED_BY(mu_);
  std::vector<Waker> read_request_waker_;
  absl::flat_hash_map<uint64_t, ReadState> outstanding_reads_
      ABSL_GUARDED_BY(mu_);
};
}  // namespace data_endpoints_detail

// Collection of data connections.
class DataEndpoints {
 public:
  using ReadTicket = data_endpoints_detail::InputQueues::ReadTicket;

  explicit DataEndpoints(
      std::vector<PromiseEndpoint> endpoints,
      grpc_event_engine::experimental::EventEngine* event_engine);

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

  bool empty() const { return parties_.empty(); }

 private:
  RefCountedPtr<data_endpoints_detail::OutputBuffers> output_buffers_;
  RefCountedPtr<data_endpoints_detail::InputQueues> input_queues_;
  std::vector<RefCountedPtr<Party>> parties_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_DATA_ENDPOINTS_H
