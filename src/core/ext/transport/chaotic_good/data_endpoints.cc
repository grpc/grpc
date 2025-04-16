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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/escaping.h"
#include "src/core/ext/transport/chaotic_good/pending_connection.h"
#include "src/core/ext/transport/chaotic_good/serialize_little_endian.h"
#include "src/core/ext/transport/chaotic_good/transport_context.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/event_engine/extensions/tcp_trace.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/telemetry/default_tcp_tracer.h"

namespace grpc_core {
namespace chaotic_good {

namespace data_endpoints_detail {

///////////////////////////////////////////////////////////////////////////////
// FrameHeader

void DataFrameHeader::Serialize(uint8_t* data) const {
  WriteLittleEndianUint64(payload_tag, data);
  WriteLittleEndianUint64(send_timestamp, data + 8);
  WriteLittleEndianUint32(payload_length, data + 16);
}

absl::StatusOr<DataFrameHeader> DataFrameHeader::Parse(const uint8_t* data) {
  DataFrameHeader header;
  header.payload_tag = ReadLittleEndianUint64(data);
  header.send_timestamp = ReadLittleEndianUint64(data + 8);
  header.payload_length = ReadLittleEndianUint32(data + 16);
  return header;
}

///////////////////////////////////////////////////////////////////////////////
// OutputBuffer

std::optional<double> OutputBuffer::DeliveryTime(uint64_t current_time,
                                                 size_t bytes) {
  if (pending_.Length() != 0 && pending_.Length() + bytes > pending_max_) {
    return std::nullopt;
  }
  return send_rate_.DeliveryTime(current_time, pending_.Length() + bytes);
}

///////////////////////////////////////////////////////////////////////////////
// OutputBuffers

Poll<Empty> OutputBuffers::PollWrite(uint64_t payload_tag, uint64_t send_time,
                                     SliceBuffer& output_buffer) {
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  const uint32_t length = output_buffer.Length();
  const size_t write_size = DataFrameHeader::kFrameHeaderSize + length;
  MutexLock lock(&mu_);
  size_t best_endpoint = std::numeric_limits<size_t>::max();
  double earliest_delivery = std::numeric_limits<double>::max();
  for (size_t i = 0; i < buffers_.size(); ++i) {
    if (!buffers_[i].has_value()) continue;
    auto delivery_time = buffers_[i]->DeliveryTime(send_time, write_size);
    if (!delivery_time.has_value()) continue;
    if (*delivery_time < earliest_delivery) {
      earliest_delivery = *delivery_time;
      best_endpoint = i;
    }
  }
  if (best_endpoint == std::numeric_limits<size_t>::max()) {
    GRPC_TRACE_LOG(chaotic_good, INFO)
        << "CHAOTIC_GOOD: No data endpoint ready for " << length
        << " bytes on queue " << this;
    write_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
    return Pending{};
  }
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: Queue " << length << " data onto endpoint "
      << best_endpoint << " queue " << this;
  waker = buffers_[best_endpoint]->TakeWaker();
  SliceBuffer& output = buffers_[best_endpoint]->pending();
  DataFrameHeader{payload_tag, send_time, length}.Serialize(
      output.AddTiny(DataFrameHeader::kFrameHeaderSize));
  output.TakeAndAppend(output_buffer);
  return Empty{};
}

Poll<SliceBuffer> OutputBuffers::PollNext(uint32_t connection_id) {
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  MutexLock lock(&mu_);
  const auto current_time = clock_->Now();
  auto& buffer = buffers_[connection_id];
  CHECK(buffer.has_value());
  buffer->MaybeCompleteSend(current_time);
  if (buffer->HavePending()) {
    waker = std::move(write_waker_);
    return buffer->TakePendingAndStartWrite(current_time);
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

InputQueue::ReadTicket InputQueue::Read(uint64_t payload_tag) {
  {
    MutexLock lock(&mu_);
    if (read_requested_.Set(payload_tag)) {
      return ReadTicket(Failure{}, nullptr);
    }
  }
  return ReadTicket(payload_tag, Ref());
}

Poll<absl::StatusOr<SliceBuffer>> InputQueue::PollRead(uint64_t payload_tag) {
  MutexLock lock(&mu_);
  if (!read_completed_.IsSet(payload_tag)) {
    read_wakers_.emplace(payload_tag,
                         GetContext<Activity>()->MakeNonOwningWaker());
    return Pending{};
  }
  auto it_buffer = read_buffers_.find(payload_tag);
  // If a read is complete then it must either be in read_buffers_ or it
  // was cancelled; if it was cancelled then we shouldn't be polling for
  // it.
  CHECK(it_buffer != read_buffers_.end());
  auto buffer = std::move(it_buffer->second);
  read_buffers_.erase(it_buffer);
  read_wakers_.erase(payload_tag);
  return std::move(buffer);
}

void InputQueue::CompleteRead(uint64_t payload_tag,
                              absl::StatusOr<SliceBuffer> buffer) {
  if (payload_tag == 0) return;
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  MutexLock lock(&mu_);
  GRPC_TRACE_LOG(chaotic_good, INFO) << "CHAOTIC_GOOD: Complete payload_tag #"
                                     << payload_tag << ": " << buffer.status();
  if (read_completed_.Set(payload_tag)) return;
  read_buffers_.emplace(payload_tag, std::move(buffer));
  auto it = read_wakers_.find(payload_tag);
  if (it != read_wakers_.end()) {
    waker = std::move(it->second);
    read_wakers_.erase(it);
  }
}

void InputQueue::Cancel(uint64_t payload_tag) {
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  MutexLock lock(&mu_);
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: Cancel payload_tag #" << payload_tag;
  auto it = read_wakers_.find(payload_tag);
  if (it != read_wakers_.end()) {
    waker = std::move(it->second);
    read_wakers_.erase(it);
  }
  read_buffers_.erase(payload_tag);
  read_completed_.Set(payload_tag);
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
          return endpoint->Write(std::move(buffer));
        },
        []() -> LoopCtl<absl::Status> { return Continue{}; });
  });
}

auto Endpoint::ReadLoop(uint32_t id, RefCountedPtr<InputQueue> input_queues,
                        std::shared_ptr<PromiseEndpoint> endpoint) {
  return Loop([id, endpoint = std::move(endpoint),
               input_queues = std::move(input_queues)]() {
    return TrySeq(
        endpoint->ReadSlice(DataFrameHeader::kFrameHeaderSize),
        [](Slice frame_header) {
          return DataFrameHeader::Parse(frame_header.data());
        },
        [id, endpoint](DataFrameHeader frame_header) {
          GRPC_TRACE_LOG(chaotic_good, INFO)
              << "CHAOTIC_GOOD: Read " << frame_header
              << " on data connection #" << id;
          return TryStaple(endpoint->Read(frame_header.payload_length),
                           frame_header);
        },
        [input_queues](std::tuple<SliceBuffer, DataFrameHeader> buffer_frame)
            -> LoopCtl<absl::Status> {
          auto& [buffer, frame_header] = buffer_frame;
          input_queues->CompleteRead(frame_header.payload_tag,
                                     std::move(buffer));
          return Continue{};
        });
  });
}

Endpoint::Endpoint(uint32_t id, RefCountedPtr<OutputBuffers> output_buffers,
                   RefCountedPtr<InputQueue> input_queues,
                   PendingConnection pending_connection, bool enable_tracing,
                   TransportContextPtr ctx) {
  auto arena = SimpleArenaAllocator(0)->MakeArena();
  arena->SetContext(ctx->event_engine.get());
  party_ = Party::Make(arena);
  party_->Spawn(
      "write",
      [id, enable_tracing, output_buffers = std::move(output_buffers),
       input_queues = std::move(input_queues),
       pending_connection = std::move(pending_connection),
       arena = std::move(arena), ctx = std::move(ctx)]() mutable {
        return TrySeq(
            pending_connection.Await(),
            [id, enable_tracing, output_buffers = std::move(output_buffers),
             input_queues = std::move(input_queues), arena = std::move(arena),
             ctx = std::move(ctx)](PromiseEndpoint ep) mutable {
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
                      ctx->stats_plugin_group));
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

DataEndpoints::DataEndpoints(std::vector<PendingConnection> endpoints_vec,
                             TransportContextPtr ctx, bool enable_tracing,
                             data_endpoints_detail::Clock* clock)
    : output_buffers_(
          MakeRefCounted<data_endpoints_detail::OutputBuffers>(clock)),
      input_queues_(MakeRefCounted<data_endpoints_detail::InputQueue>()) {
  for (size_t i = 0; i < endpoints_vec.size(); ++i) {
    endpoints_.emplace_back(i, output_buffers_, input_queues_,
                            std::move(endpoints_vec[i]), enable_tracing, ctx);
  }
}

}  // namespace chaotic_good
}  // namespace grpc_core
