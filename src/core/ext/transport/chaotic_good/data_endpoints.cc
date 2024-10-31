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

#include "src/core/ext/transport/chaotic_good/data_endpoints.h"

#include <cstddef>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/escaping.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"

namespace grpc_core {
namespace chaotic_good {

namespace data_endpoints_detail {

///////////////////////////////////////////////////////////////////////////////
// OutputBuffer

bool OutputBuffer::Accept(SliceBuffer& buffer) {
  if (pending_.Length() != 0 &&
      pending_.Length() + buffer.Length() > pending_max_) {
    return false;
  }
  pending_.Append(buffer);
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// OutputBuffers

OutputBuffers::OutputBuffers(uint32_t num_connections)
    : buffers_(num_connections) {}

Poll<uint32_t> OutputBuffers::PollWrite(SliceBuffer& output_buffer) {
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  MutexLock lock(&mu_);
  for (size_t i = 0; i < buffers_.size(); ++i) {
    if (buffers_[i].Accept(output_buffer)) {
      waker = buffers_[i].TakeWaker();
      return i;
    }
  }
  write_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
  return Pending{};
}

Poll<SliceBuffer> OutputBuffers::PollNext(uint32_t connection_id) {
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  MutexLock lock(&mu_);
  auto& buffer = buffers_[connection_id];
  if (buffer.HavePending()) {
    waker = std::move(write_waker_);
    return buffer.TakePending();
  }
  buffer.SetWaker();
  return Pending{};
}

///////////////////////////////////////////////////////////////////////////////
// InputQueues

InputQueues::InputQueues(uint32_t num_connections)
    : read_requests_(num_connections), read_request_waker_(num_connections) {}

absl::StatusOr<uint64_t> InputQueues::CreateTicket(uint32_t connection_id,
                                                   size_t length) {
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  MutexLock lock(&mu_);
  if (connection_id >= read_requests_.size()) {
    return absl::UnavailableError(
        absl::StrCat("Invalid connection id: ", connection_id));
  }
  uint64_t ticket = next_ticket_id_;
  ++next_ticket_id_;
  read_requests_[connection_id].push_back(ReadRequest{length, ticket});
  waker = std::move(read_request_waker_[connection_id]);
  return ticket;
}

Poll<absl::StatusOr<SliceBuffer>> InputQueues::PollRead(uint64_t ticket) {
  MutexLock lock(&mu_);
  auto ex_rd = completed_reads_.extract(ticket);
  if (!ex_rd.empty()) return std::move(ex_rd.mapped());
  completed_read_wakers_[ticket] = GetContext<Activity>()->MakeNonOwningWaker();
  return Pending{};
}

Poll<std::vector<InputQueues::ReadRequest>> InputQueues::PollNext(
    uint32_t connection_id) {
  MutexLock lock(&mu_);
  LOG(INFO) << "pollnext conn_id = " << connection_id;
  auto& q = read_requests_[connection_id];
  if (q.empty()) {
    read_request_waker_[connection_id] =
        GetContext<Activity>()->MakeNonOwningWaker();
    return Pending{};
  }
  auto r = std::move(q);
  q.clear();
  return r;
}
}  // namespace data_endpoints_detail

///////////////////////////////////////////////////////////////////////////////
// DataEndpoints

DataEndpoints::DataEndpoints(
    std::vector<PromiseEndpoint> endpoints_vec,
    grpc_event_engine::experimental::EventEngine* event_engine)
    : output_buffers_(MakeRefCounted<data_endpoints_detail::OutputBuffers>(
          endpoints_vec.size())),
      input_queues_(MakeRefCounted<data_endpoints_detail::InputQueues>(
          endpoints_vec.size())) {
  CHECK(event_engine != nullptr);
  for (auto& endpoint : endpoints_vec) {
    // Enable RxMemoryAlignment and RPC receive coalescing after the transport
    // setup is complete. At this point all the settings frames should have
    // been read.
    endpoint.EnforceRxMemoryAlignmentAndCoalescing();
  }
  auto endpoints = MakeRefCounted<data_endpoints_detail::Endpoints>();
  endpoints->endpoints = std::move(endpoints_vec);
  parties_.reserve(2 * endpoints->endpoints.size());
  auto arena = SimpleArenaAllocator(0)->MakeArena();
  arena->SetContext(event_engine);
  for (size_t i = 0; i < endpoints->endpoints.size(); ++i) {
    auto write_party = Party::Make(arena);
    auto read_party = Party::Make(arena);
    write_party->Spawn(
        "flush-data",
        [i, endpoints, output_buffers = output_buffers_]() {
          return Loop([i, endpoints, output_buffers]() {
            return TrySeq(
                output_buffers->Next(i),
                [endpoints = endpoints.get(), i](SliceBuffer buffer) {
                  return endpoints->endpoints[i].Write(std::move(buffer));
                },
                []() -> LoopCtl<absl::Status> { return Continue{}; });
          });
        },
        [](absl::Status) {});
    read_party->Spawn(
        "read-data",
        [i, endpoints, input_queues = input_queues_]() {
          return Loop([i, endpoints, input_queues]() {
            return TrySeq(
                input_queues->Next(i),
                [endpoints, i, input_queues](
                    std::vector<data_endpoints_detail::InputQueues::ReadRequest>
                        requests) {
                  return TrySeqContainer(
                      std::move(requests), Empty{},
                      [endpoints, i, input_queues](
                          data_endpoints_detail::InputQueues::ReadRequest
                              read_request,
                          Empty) {
                        LOG(INFO) << "[" << i << "] perform " << read_request;
                        return Seq(
                            endpoints->endpoints[i].Read(read_request.length),
                            [ticket = read_request.ticket,

                             input_queues](absl::StatusOr<SliceBuffer> buffer) {
                              LOG(INFO)
                                  << "#" << ticket << " done: "
                                  << absl::CEscape(buffer->JoinIntoString());
                              input_queues->CompleteRead(ticket,
                                                         std::move(buffer));
                              return Empty{};
                            });
                      });
                },
                []() -> LoopCtl<absl::Status> { return Continue{}; });
          });
        },
        [](absl::Status) {});
    parties_.emplace_back(std::move(write_party));
    parties_.emplace_back(std::move(read_party));
  }
}

}  // namespace chaotic_good
}  // namespace grpc_core
