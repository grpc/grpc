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

#include "src/core/ext/transport/chaotic_good_legacy/data_endpoints.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/escaping.h"
#include "src/core/ext/transport/chaotic_good_legacy/pending_connection.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/event_engine/extensions/tcp_trace.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/telemetry/default_tcp_tracer.h"

namespace grpc_core {
namespace chaotic_good_legacy {

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

Poll<uint32_t> OutputBuffers::PollWrite(SliceBuffer& output_buffer) {
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  const auto length = output_buffer.Length();
  MutexLock lock(&mu_);
  for (size_t i = 0; i < buffers_.size(); ++i) {
    if (buffers_[i].has_value() && buffers_[i]->Accept(output_buffer)) {
      GRPC_TRACE_LOG(chaotic_good, INFO)
          << "CHAOTIC_GOOD: Queue " << length << " data onto endpoint " << i
          << " queue " << this;
      waker = buffers_[i]->TakeWaker();
      return i;
    }
  }
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: No data endpoint ready for " << length
      << " bytes on queue " << this;
  write_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
  return Pending{};
}

Poll<SliceBuffer> OutputBuffers::PollNext(uint32_t connection_id) {
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  MutexLock lock(&mu_);
  auto& buffer = buffers_[connection_id];
  CHECK(buffer.has_value());
  if (buffer->HavePending()) {
    waker = std::move(write_waker_);
    return buffer->TakePending();
  }
  buffer->SetWaker();
  return Pending{};
}

void OutputBuffers::AddEndpoint(uint32_t connection_id) {
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  MutexLock lock(&mu_);
  if (buffers_.size() < connection_id + 1) {
    buffers_.resize(connection_id + 1);
  }
  CHECK(!buffers_[connection_id].has_value()) << GRPC_DUMP_ARGS(connection_id);
  buffers_[connection_id].emplace();
  waker = std::move(write_waker_);
  ready_endpoints_.fetch_add(1, std::memory_order_relaxed);
}

///////////////////////////////////////////////////////////////////////////////
// InputQueues

InputQueues::InputQueues() : read_requests_(), read_request_waker_() {}

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
  auto r = ReadRequest{length, ticket};
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: New read ticket on #" << connection_id << " " << r;
  read_requests_[connection_id].push_back(r);
  outstanding_reads_.emplace(ticket, Waker{});
  waker = std::move(read_request_waker_[connection_id]);
  return ticket;
}

Poll<absl::StatusOr<SliceBuffer>> InputQueues::PollRead(uint64_t ticket) {
  MutexLock lock(&mu_);
  auto it = outstanding_reads_.find(ticket);
  CHECK(it != outstanding_reads_.end()) << " ticket=" << ticket;
  if (auto* waker = std::get_if<Waker>(&it->second)) {
    *waker = GetContext<Activity>()->MakeNonOwningWaker();
    return Pending{};
  }
  auto result = std::move(std::get<absl::StatusOr<SliceBuffer>>(it->second));
  outstanding_reads_.erase(it);
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: Poll for ticket #" << ticket
      << " completes: " << result.status();
  return result;
}

Poll<std::vector<InputQueues::ReadRequest>> InputQueues::PollNext(
    uint32_t connection_id) {
  MutexLock lock(&mu_);
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

void InputQueues::CompleteRead(uint64_t ticket,
                               absl::StatusOr<SliceBuffer> buffer) {
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  MutexLock lock(&mu_);
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: Complete ticket #" << ticket << ": " << buffer.status();
  auto it = outstanding_reads_.find(ticket);
  if (it == outstanding_reads_.end()) return;  // cancelled
  waker = std::move(std::get<Waker>(it->second));
  it->second.emplace<absl::StatusOr<SliceBuffer>>(std::move(buffer));
}

void InputQueues::CancelTicket(uint64_t ticket) {
  MutexLock lock(&mu_);
  outstanding_reads_.erase(ticket);
}

void InputQueues::AddEndpoint(uint32_t connection_id) {
  MutexLock lock(&mu_);
  CHECK_EQ(read_requests_.size(), read_request_waker_.size());
  if (read_requests_.size() <= connection_id) {
    read_requests_.resize(connection_id + 1);
    read_request_waker_.resize(connection_id + 1);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Endpoint

auto Endpoint::WriteLoop(uint32_t id,
                         RefCountedPtr<OutputBuffers> output_buffers,
                         std::shared_ptr<PromiseEndpoint> endpoint) {
  output_buffers->AddEndpoint(id);
  return Loop([id, endpoint = std::move(endpoint),
               output_buffers = std::move(output_buffers)]() {
    return TrySeq(
        output_buffers->Next(id),
        [endpoint, id](SliceBuffer buffer) {
          GRPC_TRACE_LOG(chaotic_good, INFO)
              << "CHAOTIC_GOOD: Write " << buffer.Length()
              << "b to data endpoint #" << id;
          return endpoint->Write(std::move(buffer),
                                 PromiseEndpoint::WriteArgs{});
        },
        []() -> LoopCtl<absl::Status> { return Continue{}; });
  });
}

auto Endpoint::ReadLoop(uint32_t id, RefCountedPtr<InputQueues> input_queues,
                        std::shared_ptr<PromiseEndpoint> endpoint) {
  return Loop([id, endpoint, input_queues = std::move(input_queues)]() {
    return TrySeq(
        input_queues->Next(id),
        [endpoint, input_queues](
            std::vector<data_endpoints_detail::InputQueues::ReadRequest>
                requests) {
          return TrySeqContainer(
              std::move(requests), Empty{},
              [endpoint, input_queues](
                  data_endpoints_detail::InputQueues::ReadRequest read_request,
                  Empty) {
                return Seq(endpoint->Read(read_request.length),
                           [ticket = read_request.ticket,

                            input_queues](absl::StatusOr<SliceBuffer> buffer) {
                             input_queues->CompleteRead(ticket,
                                                        std::move(buffer));
                             return Empty{};
                           });
              });
        },
        []() -> LoopCtl<absl::Status> { return Continue{}; });
  });
}

Endpoint::Endpoint(uint32_t id, RefCountedPtr<OutputBuffers> output_buffers,
                   RefCountedPtr<InputQueues> input_queues,
                   PendingConnection pending_connection, bool enable_tracing,
                   grpc_event_engine::experimental::EventEngine* event_engine,
                   std::shared_ptr<GlobalStatsPluginRegistry::StatsPluginGroup>
                       stats_plugin_group) {
  input_queues->AddEndpoint(id);
  auto arena = SimpleArenaAllocator(0)->MakeArena();
  arena->SetContext(event_engine);
  party_ = Party::Make(arena);
  party_->Spawn(
      "write",
      [id, enable_tracing, output_buffers = std::move(output_buffers),
       input_queues = std::move(input_queues),
       pending_connection = std::move(pending_connection),
       arena = std::move(arena),
       stats_plugin_group = std::move(stats_plugin_group)]() mutable {
        return TrySeq(
            pending_connection.Await(),
            [id, enable_tracing, output_buffers = std::move(output_buffers),
             input_queues = std::move(input_queues), arena = std::move(arena),
             stats_plugin_group =
                 std::move(stats_plugin_group)](PromiseEndpoint ep) mutable {
              GRPC_TRACE_LOG(chaotic_good, INFO)
                  << "CHAOTIC_GOOD: data endpoint " << id << " to "
                  << grpc_event_engine::experimental::ResolvedAddressToString(
                         ep.GetPeerAddress())
                         .value_or("<<unknown peer address>>")
                  << " ready";
              auto endpoint = std::make_shared<PromiseEndpoint>(std::move(ep));
              // Enable RxMemoryAlignment and RPC receive coalescing after the
              // transport setup is complete. At this point all the settings
              // frames should have been read.
              endpoint->EnforceRxMemoryAlignmentAndCoalescing();
              if (enable_tracing) {
                auto* epte = grpc_event_engine::experimental::QueryExtension<
                    grpc_event_engine::experimental::TcpTraceExtension>(
                    endpoint->GetEventEngineEndpoint().get());
                if (epte != nullptr) {
                  epte->SetTcpTracer(std::make_shared<DefaultTcpTracer>(
                      std::move(stats_plugin_group)));
                }
              }
              auto read_party = Party::Make(std::move(arena));
              read_party->Spawn(
                  "read",
                  [id, input_queues = std::move(input_queues), endpoint]() {
                    return ReadLoop(id, input_queues, endpoint);
                  },
                  [](absl::Status) {});
              return Map(
                  WriteLoop(id, std::move(output_buffers), std::move(endpoint)),
                  [read_party](auto x) { return x; });
            });
      },
      [](absl::Status) {});
}

}  // namespace data_endpoints_detail

///////////////////////////////////////////////////////////////////////////////
// DataEndpoints

DataEndpoints::DataEndpoints(
    std::vector<PendingConnection> endpoints_vec,
    grpc_event_engine::experimental::EventEngine* event_engine,
    std::shared_ptr<GlobalStatsPluginRegistry::StatsPluginGroup>
        stats_plugin_group,
    bool enable_tracing)
    : output_buffers_(MakeRefCounted<data_endpoints_detail::OutputBuffers>()),
      input_queues_(MakeRefCounted<data_endpoints_detail::InputQueues>()) {
  CHECK(event_engine != nullptr);
  for (size_t i = 0; i < endpoints_vec.size(); ++i) {
    endpoints_.emplace_back(i, output_buffers_, input_queues_,
                            std::move(endpoints_vec[i]), enable_tracing,
                            event_engine, stats_plugin_group);
  }
}

}  // namespace chaotic_good_legacy
}  // namespace grpc_core
