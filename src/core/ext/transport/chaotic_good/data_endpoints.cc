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
#include <memory>

#include "absl/cleanup/cleanup.h"
#include "src/core/ext/transport/chaotic_good/tcp_frame_header.h"
#include "src/core/ext/transport/chaotic_good/tcp_ztrace_collector.h"
#include "src/core/ext/transport/chaotic_good/transport_context.h"
#include "src/core/lib/event_engine/extensions/channelz.h"
#include "src/core/lib/event_engine/extensions/tcp_trace.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/transport/transport_framing_endpoint_extension.h"
#include "src/core/telemetry/default_tcp_tracer.h"
#include "src/core/util/dump_args.h"
#include "src/core/util/string.h"

namespace grpc_core {
namespace chaotic_good {

namespace data_endpoints_detail {

namespace {
const uint64_t kSecurityFramePayloadTag = 0;
}

///////////////////////////////////////////////////////////////////////////////
// SendRate

void SendRate::StartSend(uint64_t current_time, uint64_t send_size) {
  CHECK_NE(current_time, 0u);
  send_start_time_ = current_time;
  send_size_ = send_size;
}

void SendRate::MaybeCompleteSend(uint64_t current_time) {
  if (send_start_time_ == 0) return;
  if (current_time > send_start_time_) {
    const double rate = static_cast<double>(send_size_) /
                        static_cast<double>(current_time - send_start_time_);
    // Adjust send rate based on observations.
    if (current_rate_ > 0) {
      current_rate_ = 0.9 * current_rate_ + 0.1 * rate;
    } else {
      current_rate_ = rate;
    }
  }
  send_start_time_ = 0;
}

void SendRate::SetCurrentRate(double bytes_per_nanosecond) {
  CHECK_GE(bytes_per_nanosecond, 0.0);
  current_rate_ = bytes_per_nanosecond;
  last_rate_measurement_ = Timestamp::Now();
}

bool SendRate::IsRateMeasurementStale() const {
  return Timestamp::Now() - last_rate_measurement_ > Duration::Seconds(1);
}

LbDecision SendRate::GetLbDecision(uint64_t current_time, size_t bytes) {
  LbDecision decision;
  decision.bytes = bytes;
  if (send_start_time_ != 0) {
    decision.current_send = {
        send_size_,
        static_cast<double>(current_time - send_start_time_) * 1e-9,
    };
  }
  decision.current_rate = current_rate_;
  if (current_rate_ <= 0 || IsRateMeasurementStale()) {
    decision.delivery_time = 0.0;
    return decision;
  }
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
  decision.delivery_time = (start_time + bytes / current_rate_) * 1e-9;
  return decision;
}

void SendRate::AddData(Json::Object& obj) const {
  if (send_start_time_ != 0) {
    obj["send_start_time"] = Json::FromNumber(send_start_time_);
    obj["send_size"] = Json::FromNumber(send_size_);
  }
  obj["current_rate"] = Json::FromNumber(current_rate_);
  obj["rate_measurement_age"] =
      Json::FromNumber((Timestamp::Now() - last_rate_measurement_).millis());
}

///////////////////////////////////////////////////////////////////////////////
// OutputBuffer

LbDecision OutputBuffer::GetLbDecision(uint64_t current_time, size_t bytes) {
  LbDecision decision =
      send_rate_.GetLbDecision(current_time, pending_.Length() + bytes);
  CHECK(decision.delivery_time.has_value());
  // If there's already data queued and we claim immediate sending (eg new
  // connection) OR if the send would take >300ms... wait for a bit.
  if (pending_.Length() != 0) {
    if (*decision.delivery_time <= 0.0 || *decision.delivery_time > 0.3) {
      decision.delivery_time = std::nullopt;
    }
  }
  return decision;
}

void OutputBuffer::MaybeCompleteSend(uint64_t current_time) {
  send_rate_.MaybeCompleteSend(current_time);
}

NextWrite OutputBuffer::TakePendingAndStartWrite(uint64_t current_time) {
  send_rate_.StartSend(current_time, pending_.Length());
  bool trace = false;
  if (send_rate_.IsRateMeasurementStale()) {
    send_rate_.PerformRateProbe();
    trace = true;
  }
  NextWrite next_write;
  next_write.bytes = std::move(pending_);
  next_write.trace = trace;
  pending_.Clear();
  return next_write;
}

void OutputBuffer::AddData(Json::Object& obj) const {
  obj["have_flush_waker"] = Json::FromBool(!flush_waker_.is_unwakeable());
  obj["pending_bytes"] = Json::FromNumber(pending_.Length());
  send_rate_.AddData(obj);
}

///////////////////////////////////////////////////////////////////////////////
// OutputBuffers

void OutputBuffers::WriteSecurityFrame(uint32_t connection_id,
                                       SliceBuffer output_buffer) {
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  MutexLock lock(&mu_);
  auto& buffer = buffers_[connection_id];
  if (!buffer.has_value()) return;
  waker = buffer->TakeWaker();
  SliceBuffer& output = buffer->pending();
  CHECK_LT(output_buffer.Length(), std::numeric_limits<uint32_t>::max());
  const uint32_t payload_length = static_cast<uint32_t>(output_buffer.Length());
  TcpDataFrameHeader hdr{kSecurityFramePayloadTag, 0, payload_length};
  auto header_padding = DataConnectionPadding(
      TcpDataFrameHeader::kFrameHeaderSize, encode_alignment_);
  MutableSlice header_slice = MutableSlice::CreateUninitialized(
      TcpDataFrameHeader::kFrameHeaderSize + header_padding);
  hdr.Serialize(header_slice.data());
  if (header_padding > 0) {
    memset(header_slice.data() + TcpDataFrameHeader::kFrameHeaderSize, 0,
           header_padding);
  }
  output.AppendIndexed(Slice(std::move(header_slice)));
  const auto payload_padding =
      DataConnectionPadding(output_buffer.Length(), encode_alignment_);
  output.TakeAndAppend(output_buffer);
  if (payload_padding > 0) {
    auto slice = MutableSlice::CreateUninitialized(payload_padding);
    memset(slice.data(), 0, payload_padding);
    output.AppendIndexed(Slice(std::move(slice)));
  }
  CHECK_EQ(output.Length() % encode_alignment_, 0u) << GRPC_DUMP_ARGS(
      output.Length(), encode_alignment_, header_padding, payload_padding);
}

Poll<Empty> OutputBuffers::PollWrite(uint64_t payload_tag, uint64_t send_time,
                                     SliceBuffer& output_buffer,
                                     std::shared_ptr<TcpCallTracer>&) {
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  const uint32_t length = output_buffer.Length();
  const size_t write_size =
      TcpDataFrameHeader::kFrameHeaderSize +
      DataConnectionPadding(TcpDataFrameHeader::kFrameHeaderSize,
                            encode_alignment_) +
      length + DataConnectionPadding(length, encode_alignment_);
  CHECK_EQ(write_size % encode_alignment_, 0u)
      << GRPC_DUMP_ARGS(write_size, length, encode_alignment_);
  MutexLock lock(&mu_);
  size_t best_endpoint = std::numeric_limits<size_t>::max();
  double earliest_delivery = std::numeric_limits<double>::max();
  for (size_t i = 0; i < buffers_.size(); ++i) {
    if (!buffers_[i].has_value()) continue;
    auto decision = buffers_[i]->GetLbDecision(send_time, write_size);
    if (!decision.delivery_time.has_value()) continue;
    if (*decision.delivery_time < earliest_delivery) {
      earliest_delivery = *decision.delivery_time;
      best_endpoint = i;
    }
  }
  if (best_endpoint == std::numeric_limits<size_t>::max()) {
    GRPC_TRACE_LOG(chaotic_good, INFO)
        << "CHAOTIC_GOOD: No data endpoint ready for " << length
        << " bytes on queue " << this;
    ztrace_collector_->Append(NoEndpointForWriteTrace{length, payload_tag});
    write_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
    return Pending{};
  }
  TcpDataFrameHeader hdr{payload_tag, send_time, length};
  ztrace_collector_->Append(
      [this, &hdr, send_time, best_endpoint, write_size]() {
        std::vector<std::optional<LbDecision>> lb_decisions;
        mu_.AssertHeld();
        for (size_t i = 0; i < buffers_.size(); ++i) {
          if (!buffers_[i].has_value()) {
            lb_decisions.emplace_back(std::nullopt);
            continue;
          }
          lb_decisions.emplace_back(
              buffers_[i]->GetLbDecision(send_time, write_size));
        }
        return WriteLargeFrameHeaderTrace{hdr, best_endpoint,
                                          std::move(lb_decisions)};
      });
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: Queue " << length << " data in " << write_size
      << "b wire bytes onto endpoint " << best_endpoint << " queue " << this;
  auto& buffer = buffers_[best_endpoint];
  waker = buffer->TakeWaker();
  SliceBuffer& output = buffer->pending();
  CHECK_EQ(output.Length() % encode_alignment_, 0u)
      << GRPC_DUMP_ARGS(output.Length(), encode_alignment_);
  const auto header_padding = DataConnectionPadding(
      TcpDataFrameHeader::kFrameHeaderSize, encode_alignment_);
  MutableSlice header_slice = MutableSlice::CreateUninitialized(
      TcpDataFrameHeader::kFrameHeaderSize + header_padding);
  TcpDataFrameHeader{payload_tag, send_time, length}.Serialize(
      header_slice.data());
  if (header_padding > 0) {
    memset(header_slice.data() + TcpDataFrameHeader::kFrameHeaderSize, 0,
           header_padding);
  }
  output.AppendIndexed(Slice(std::move(header_slice)));
  const auto payload_padding =
      DataConnectionPadding(output_buffer.Length(), encode_alignment_);
  output.TakeAndAppend(output_buffer);
  // Add padding for output buffer
  if (payload_padding > 0) {
    auto slice = MutableSlice::CreateUninitialized(payload_padding);
    memset(slice.data(), 0, payload_padding);
    output.AppendIndexed(Slice(std::move(slice)));
  }
  CHECK_EQ(output.Length() % encode_alignment_, 0u) << GRPC_DUMP_ARGS(
      output.Length(), encode_alignment_, header_padding, payload_padding);
  return Empty{};
}

Poll<NextWrite> OutputBuffers::PollNext(uint32_t connection_id) {
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

void OutputBuffers::UpdateSendRate(uint32_t connection_id,
                                   double bytes_per_nanosecond) {
  MutexLock lock(&mu_);
  auto& buffer = buffers_[connection_id];
  if (!buffer.has_value()) return;
  buffer->UpdateSendRate(bytes_per_nanosecond);
}

void OutputBuffers::AddData(channelz::DataSink& sink) {
  Json::Object data;
  MutexLock lock(&mu_);
  data["ready_endpoints"] =
      Json::FromNumber(ready_endpoints_.load(std::memory_order_relaxed));
  data["have_write_waker"] = Json::FromBool(!write_waker_.is_unwakeable());
  Json::Array buffers;
  for (const auto& buffer : buffers_) {
    Json::Object obj;
    if (!buffer.has_value()) {
      obj["write_state"] = Json::FromString("closed");
    } else {
      obj["write_state"] = Json::FromString("open");
      buffer->AddData(obj);
    }
    buffers.emplace_back(Json::FromObject(std::move(obj)));
  }
  data["buffers"] = Json::FromArray(std::move(buffers));
  sink.AddAdditionalInfo("outputBuffers", std::move(data));
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
    if (!closed_error_.ok()) return closed_error_;
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

void InputQueue::CompleteRead(uint64_t payload_tag, SliceBuffer buffer) {
  if (payload_tag == 0) return;
  Waker waker;
  auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
  MutexLock lock(&mu_);
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: Complete payload_tag #" << payload_tag;
  if (!closed_error_.ok()) return;
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

void InputQueue::AddData(channelz::DataSink& sink) {
  Json::Object data;
  MutexLock lock(&mu_);
  data["read_requested"] = Json::FromString(absl::StrCat(read_requested_));
  data["read_completed"] = Json::FromString(absl::StrCat(read_completed_));
  if (!read_wakers_.empty()) {
    Json::Array read_wakers;
    for (const auto& [payload_tag, waker] : read_wakers_) {
      read_wakers.emplace_back(Json::FromNumber(payload_tag));
    }
    data["read_wakers"] = Json::FromArray(std::move(read_wakers));
  }
  if (!read_buffers_.empty()) {
    Json::Array read_buffers;
    for (const auto& [payload_tag, buffer] : read_buffers_) {
      Json::Object buffer_data;
      buffer_data["payload_tag"] = Json::FromNumber(payload_tag);
      buffer_data["bytes"] = Json::FromNumber(buffer.Length());
      read_buffers.emplace_back(Json::FromObject(std::move(buffer_data)));
    }
    data["read_buffers"] = Json::FromArray(std::move(read_buffers));
  }
  if (!closed_error_.ok()) {
    data["closed_error"] = Json::FromString(closed_error_.ToString());
  }
  sink.AddAdditionalInfo("inputQueue", std::move(data));
}

void InputQueue::SetClosed(absl::Status status) {
  absl::flat_hash_map<uint64_t, Waker> read_wakers;
  Waker closed_waker;
  auto wake_up = absl::MakeCleanup([&]() {
    for (auto& [_, waker] : read_wakers) waker.Wakeup();
    closed_waker.Wakeup();
  });
  MutexLock lock(&mu_);
  if (!closed_error_.ok()) return;
  if (status.ok()) status = absl::UnavailableError("transport closed");
  closed_error_ = std::move(status);
  read_wakers = std::move(read_wakers_);
  read_wakers_.clear();
  closed_waker = std::move(await_closed_);
}

///////////////////////////////////////////////////////////////////////////////
// Endpoint

namespace {
RefCountedPtr<channelz::SocketNode> MakeSocketNode(
    const TransportContextPtr& ctx, const PromiseEndpoint& endpoint) {
  std::string peer_string =
      grpc_event_engine::experimental::ResolvedAddressToString(
          endpoint.GetPeerAddress())
          .value_or("unknown");
  return MakeRefCounted<channelz::SocketNode>(
      grpc_event_engine::experimental::ResolvedAddressToString(
          endpoint.GetLocalAddress())
          .value_or("unknown"),
      peer_string, absl::StrCat("chaotic-good ", peer_string),
      ctx->socket_node->security());
}

TransportFramingEndpointExtension* GetTransportFramingEndpointExtension(
    PromiseEndpoint& endpoint) {
  return grpc_event_engine::experimental::QueryExtension<
      TransportFramingEndpointExtension>(
      endpoint.GetEventEngineEndpoint().get());
}
}  // namespace

auto Endpoint::WriteLoop(uint32_t id,
                         RefCountedPtr<OutputBuffers> output_buffers,
                         std::shared_ptr<PromiseEndpoint> endpoint,
                         std::shared_ptr<TcpZTraceCollector> ztrace_collector) {
  output_buffers->AddEndpoint(id);
  std::vector<size_t> requested_metrics;
  std::optional<size_t> data_rate_metric =
      endpoint->GetEventEngineEndpoint()->GetMetricKey("delivery_rate");
  if (data_rate_metric.has_value()) {
    requested_metrics.push_back(*data_rate_metric);
  }
  return Loop([id, endpoint = std::move(endpoint),
               output_buffers = std::move(output_buffers),
               requested_metrics = std::move(requested_metrics),
               data_rate_metric, ztrace_collector]() {
    return TrySeq(
        output_buffers->Next(id),
        [endpoint, id,
         requested_metrics = absl::Span<const size_t>(requested_metrics),
         data_rate_metric, output_buffers,
         ztrace_collector](data_endpoints_detail::NextWrite next_write) {
          GRPC_TRACE_LOG(chaotic_good, INFO)
              << "CHAOTIC_GOOD: " << output_buffers.get() << " "
              << ResolvedAddressToString(endpoint->GetPeerAddress())
                     .value_or("peer-unknown")
              << " Write " << next_write.bytes.Length()
              << "b to data endpoint #" << id;
          using grpc_event_engine::experimental::EventEngine;
          PromiseEndpoint::WriteArgs write_args;
          if (next_write.trace && data_rate_metric.has_value()) {
            write_args.set_metrics_sink(EventEngine::Endpoint::WriteEventSink(
                requested_metrics,
                {EventEngine::Endpoint::WriteEvent::kSendMsg,
                 EventEngine::Endpoint::WriteEvent::kScheduled,
                 EventEngine::Endpoint::WriteEvent::kAcked},
                [data_rate_metric, id, output_buffers, ztrace_collector,
                 endpoint = endpoint.get()](
                    EventEngine::Endpoint::WriteEvent event,
                    absl::Time timestamp,
                    std::vector<EventEngine::Endpoint::WriteMetric> metrics) {
                  ztrace_collector->Append([event, timestamp, &metrics,
                                            endpoint]() {
                    EndpointWriteMetricsTrace trace{timestamp, event, {}};
                    trace.metrics.reserve(metrics.size());
                    for (const auto [id, value] : metrics) {
                      if (auto name =
                              endpoint->GetEventEngineEndpoint()->GetMetricName(
                                  id);
                          name.has_value()) {
                        trace.metrics.push_back({*name, value});
                      }
                    }
                    return trace;
                  });
                  for (const auto& metric : metrics) {
                    if (metric.key == *data_rate_metric) {
                      output_buffers->UpdateSendRate(id, metric.value * 1e-9);
                    }
                  }
                }));
          }
          ztrace_collector->Append(WriteBytesToEndpointTrace{
              next_write.bytes.Length(), id, next_write.trace});
          return Map(
              AddGeneratedErrorPrefix(
                  [id, endpoint]() {
                    return absl::StrCat(
                        "DATA_CHANNEL: ",
                        ResolvedAddressToString(endpoint->GetPeerAddress())
                            .value_or("peer-unknown"),
                        "#", id);
                  },
                  GRPC_LATENT_SEE_PROMISE(
                      "DataEndpointWrite",
                      endpoint->Write(std::move(next_write.bytes),
                                      std::move(write_args)))),
              [id, output_buffers, ztrace_collector](absl::Status status) {
                ztrace_collector->Append([id, &status]() {
                  return FinishWriteBytesToEndpointTrace{id, status};
                });
                GRPC_TRACE_LOG(chaotic_good, INFO)
                    << "CHAOTIC_GOOD: " << output_buffers.get() << " "
                    << "Write done to data endpoint #" << id
                    << " status: " << status;
                return status;
              });
        },
        [id, output_buffers]() -> LoopCtl<absl::Status> {
          GRPC_TRACE_LOG(chaotic_good, INFO)
              << "CHAOTIC_GOOD: " << output_buffers.get() << " "
              << "Write done to data endpoint #" << id;
          return Continue{};
        });
  });
}

auto Endpoint::ReadLoop(uint32_t id, uint32_t decode_alignment,
                        RefCountedPtr<InputQueue> input_queues,
                        std::shared_ptr<PromiseEndpoint> endpoint,
                        std::shared_ptr<TcpZTraceCollector> ztrace_collector) {
  return Loop([id, decode_alignment, endpoint = std::move(endpoint),
               input_queues = std::move(input_queues),
               ztrace_collector = std::move(ztrace_collector)]() {
    return TrySeq(
        GRPC_LATENT_SEE_PROMISE(
            "DataEndpointReadHdr",
            endpoint->ReadSlice(
                TcpDataFrameHeader::kFrameHeaderSize +
                DataConnectionPadding(TcpDataFrameHeader::kFrameHeaderSize,
                                      decode_alignment))),
        [id](Slice frame_header) {
          auto hdr = TcpDataFrameHeader::Parse(frame_header.data());
          GRPC_TRACE_LOG(chaotic_good, INFO)
              << "CHAOTIC_GOOD: Read "
              << (hdr.ok() ? absl::StrCat(*hdr) : hdr.status().ToString())
              << " on data connection #" << id;
          return hdr;
        },
        [endpoint, ztrace_collector, id,
         decode_alignment](TcpDataFrameHeader frame_header) {
          ztrace_collector->Append(ReadDataHeaderTrace{frame_header});
          return Map(
              TryStaple(GRPC_LATENT_SEE_PROMISE(
                            "DataEndpointRead",
                            endpoint->Read(frame_header.payload_length +
                                           DataConnectionPadding(
                                               frame_header.payload_length,
                                               decode_alignment))),
                        frame_header),
              [id, frame_header](auto x) {
                GRPC_TRACE_LOG(chaotic_good, INFO)
                    << "CHAOTIC_GOOD: Complete read " << frame_header
                    << " on data connection #" << id
                    << " status: " << x.status();
                return x;
              });
        },
        [endpoint, input_queues, id, decode_alignment](
            std::tuple<SliceBuffer, TcpDataFrameHeader> buffer_frame)
            -> LoopCtl<absl::Status> {
          auto& [buffer, frame_header] = buffer_frame;
          GRPC_TRACE_LOG(chaotic_good, INFO)
              << "CHAOTIC_GOOD: Complete read " << frame_header
              << " on data connection #" << id;
          buffer.RemoveLastNBytesNoInline(DataConnectionPadding(
              frame_header.payload_length, decode_alignment));
          if (GPR_UNLIKELY(frame_header.payload_tag ==
                           kSecurityFramePayloadTag)) {
            ReceiveSecurityFrame(*endpoint, std::move(buffer));
          } else {
            input_queues->CompleteRead(frame_header.payload_tag,
                                       std::move(buffer));
          }
          return Continue{};
        });
  });
}

void Endpoint::ReceiveSecurityFrame(PromiseEndpoint& endpoint,
                                    SliceBuffer buffer) {
  auto* transport_framing_endpoint_extension =
      GetTransportFramingEndpointExtension(endpoint);
  if (transport_framing_endpoint_extension == nullptr) return;
  transport_framing_endpoint_extension->ReceiveFrame(std::move(buffer));
}

Endpoint::Endpoint(uint32_t id, uint32_t decode_alignment,
                   RefCountedPtr<OutputBuffers> output_buffers,
                   RefCountedPtr<InputQueue> input_queues,
                   PendingConnection pending_connection, bool enable_tracing,
                   TransportContextPtr ctx,
                   std::shared_ptr<TcpZTraceCollector> ztrace_collector)
    : ztrace_collector_(ztrace_collector), id_(id) {
  auto arena = SimpleArenaAllocator(0)->MakeArena();
  arena->SetContext(ctx->event_engine.get());
  party_ = Party::Make(arena);
  party_->Spawn(
      "write",
      [id, decode_alignment, enable_tracing,
       output_buffers = std::move(output_buffers), input_queues,
       pending_connection = std::move(pending_connection),
       arena = std::move(arena), ctx = std::move(ctx),
       ztrace_collector = std::move(ztrace_collector)]() mutable {
        return TrySeq(
            pending_connection.Await(),
            [id, decode_alignment, enable_tracing,
             output_buffers = std::move(output_buffers),
             input_queues = std::move(input_queues), arena = std::move(arena),
             ctx = std::move(ctx),
             ztrace_collector =
                 std::move(ztrace_collector)](PromiseEndpoint ep) mutable {
              GRPC_TRACE_LOG(chaotic_good, INFO)
                  << "CHAOTIC_GOOD: data endpoint " << id << " to "
                  << grpc_event_engine::experimental::ResolvedAddressToString(
                         ep.GetPeerAddress())
                         .value_or("<<unknown peer address>>")
                  << " ready";
              RefCountedPtr<channelz::SocketNode> socket_node;
              if (ctx->socket_node != nullptr) {
                auto* channelz_endpoint =
                    grpc_event_engine::experimental::QueryExtension<
                        grpc_event_engine::experimental::ChannelzExtension>(
                        ep.GetEventEngineEndpoint().get());
                socket_node = MakeSocketNode(ctx, ep);
                socket_node->AddParent(ctx->socket_node.get());
                if (channelz_endpoint != nullptr) {
                  channelz_endpoint->SetSocketNode(socket_node);
                }
              }
              auto endpoint = std::make_shared<PromiseEndpoint>(std::move(ep));
              // Enable RxMemoryAlignment and RPC receive coalescing after the
              // transport setup is complete. At this point all the settings
              // frames should have been read.
              if (decode_alignment != 1) {
                endpoint->EnforceRxMemoryAlignmentAndCoalescing();
              }
              if (enable_tracing) {
                auto* epte = grpc_event_engine::experimental::QueryExtension<
                    grpc_event_engine::experimental::TcpTraceExtension>(
                    endpoint->GetEventEngineEndpoint().get());
                if (epte != nullptr) {
                  epte->SetTcpTracer(std::make_shared<DefaultTcpTracer>(
                      ctx->stats_plugin_group));
                }
              }
              auto* transport_framing_endpoint_extension =
                  GetTransportFramingEndpointExtension(*endpoint);
              if (transport_framing_endpoint_extension != nullptr) {
                transport_framing_endpoint_extension->SetSendFrameCallback(
                    [id, output_buffers](SliceBuffer* data) {
                      output_buffers->WriteSecurityFrame(id, std::move(*data));
                    });
              }
              auto read_party = Party::Make(std::move(arena));
              read_party->Spawn(
                  "read",
                  [id, decode_alignment, input_queues, endpoint,
                   ztrace_collector]() {
                    return ReadLoop(id, decode_alignment, input_queues,
                                    endpoint, ztrace_collector);
                  },
                  [input_queues](absl::Status status) {
                    GRPC_TRACE_LOG(chaotic_good, INFO)
                        << "CHAOTIC_GOOD: read party done: " << status;
                    input_queues->SetClosed(std::move(status));
                  });
              return Map(
                  WriteLoop(id, std::move(output_buffers), std::move(endpoint),
                            std::move(ztrace_collector)),
                  [read_party, socket_node = std::move(socket_node)](auto x) {
                    return x;
                  });
            });
      },
      [input_queues](absl::Status status) {
        GRPC_TRACE_LOG(chaotic_good, INFO)
            << "CHAOTIC_GOOD: write party done: " << status;
        input_queues->SetClosed(std::move(status));
      });
}

}  // namespace data_endpoints_detail

///////////////////////////////////////////////////////////////////////////////
// DataEndpoints

DataEndpoints::DataEndpoints(
    std::vector<PendingConnection> endpoints_vec, TransportContextPtr ctx,
    uint32_t encode_alignment, uint32_t decode_alignment,
    std::shared_ptr<TcpZTraceCollector> ztrace_collector, bool enable_tracing,
    data_endpoints_detail::Clock* clock)
    : output_buffers_(MakeRefCounted<data_endpoints_detail::OutputBuffers>(
          clock, encode_alignment, ztrace_collector, ctx)),
      input_queues_(MakeRefCounted<data_endpoints_detail::InputQueue>(ctx)) {
  for (size_t i = 0; i < endpoints_vec.size(); ++i) {
    endpoints_.emplace_back(std::make_unique<data_endpoints_detail::Endpoint>(
        i, decode_alignment, output_buffers_, input_queues_,
        std::move(endpoints_vec[i]), enable_tracing, ctx, ztrace_collector));
  }
}

}  // namespace chaotic_good
}  // namespace grpc_core
