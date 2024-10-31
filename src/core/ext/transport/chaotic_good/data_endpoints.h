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

#include <cstdint>

#include "src/core/lib/promise/party.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

namespace data_endpoints_detail {
struct Endpoints : public RefCounted<Endpoints> {
  std::vector<PromiseEndpoint> endpoints;
};

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
  struct ReadRequest {
    size_t length;
    uint64_t ticket;

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const ReadRequest& req) {
      sink.Append(absl::StrCat("read#", req.ticket, ":", req.length, "b"));
    }
  };

  explicit InputQueues(uint32_t num_connections);

  auto Read(uint32_t connection_id, size_t length) {
    auto ticket = CreateTicket(connection_id, length);
    return [ticket, this]() -> Poll<absl::StatusOr<SliceBuffer>> {
      if (!ticket.ok()) return ticket.status();
      return PollRead(*ticket);
    };
  }

  auto Next(uint32_t connection_id) {
    return [this, connection_id]() { return PollNext(connection_id); };
  }

  void CompleteRead(uint64_t ticket, absl::StatusOr<SliceBuffer> buffer);

 private:
  absl::StatusOr<uint64_t> CreateTicket(uint32_t connection_id, size_t length);
  Poll<absl::StatusOr<SliceBuffer>> PollRead(uint64_t ticket);
  Poll<std::vector<ReadRequest>> PollNext(uint32_t connection_id);

  Mutex mu_;
  uint64_t next_ticket_id_ ABSL_GUARDED_BY(mu_) = 0;
  std::vector<std::vector<ReadRequest>> read_requests_ ABSL_GUARDED_BY(mu_);
  std::vector<Waker> read_request_waker_;
  absl::flat_hash_map<uint64_t, absl::StatusOr<SliceBuffer>> completed_reads_
      ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<uint64_t, Waker> completed_read_wakers_
      ABSL_GUARDED_BY(mu_);
};
}  // namespace data_endpoints_detail

class DataEndpoints {
 public:
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

  auto Read(uint32_t connection_id, size_t length) {
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
