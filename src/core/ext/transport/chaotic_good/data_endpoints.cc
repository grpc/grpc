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

#include <grpc/event_engine/event_engine.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/container/inlined_vector.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"
#include "src/core/ext/transport/chaotic_good/tcp_frame_header.h"
#include "src/core/ext/transport/chaotic_good/tcp_ztrace_collector.h"
#include "src/core/ext/transport/chaotic_good/transport_context.h"
#include "src/core/lib/event_engine/extensions/channelz.h"
#include "src/core/lib/event_engine/extensions/tcp_trace.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/transport/transport_framing_endpoint_extension.h"
#include "src/core/telemetry/default_tcp_tracer.h"
#include "src/core/util/dump_args.h"
#include "src/core/util/latent_see.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/string.h"

namespace grpc_core {
namespace chaotic_good {

namespace data_endpoints_detail {

namespace {
const uint64_t kSecurityFramePayloadTag = 0;
}

///////////////////////////////////////////////////////////////////////////////
// SendRate

void SendRate::SetNetworkMetrics(const std::optional<NetworkSend>& network_send,
                                 const NetworkMetrics& metrics) {
  bool updated = false;
  if (metrics.rtt_usec.has_value()) {
    CHECK_GE(*metrics.rtt_usec, 0u);
    rtt_usec_ = *metrics.rtt_usec;
    updated = true;
  }
  if (metrics.bytes_per_nanosecond.has_value()) {
    if (metrics.bytes_per_nanosecond < 0) {
      LOG_EVERY_N_SEC(ERROR, 10)
          << "Negative bytes per nanosecond: " << *metrics.bytes_per_nanosecond;
    } else if (std::isnan(*metrics.bytes_per_nanosecond)) {
      LOG_EVERY_N_SEC(ERROR, 10)
          << "NaN bytes per nanosecond: " << *metrics.bytes_per_nanosecond;
    } else {
      current_rate_ = *metrics.bytes_per_nanosecond;
    }
    updated = true;
  }
  if (network_send.has_value() &&
      network_send->start_time > last_send_started_time_) {
    last_send_started_time_ = network_send->start_time;
    last_send_bytes_outstanding_ = network_send->bytes;
    updated = true;
  }
  if (updated) last_rate_measurement_ = Timestamp::Now();
}

bool SendRate::IsRateMeasurementStale() const {
  return Timestamp::Now() - last_rate_measurement_ > Duration::Seconds(1);
}

SendRate::DeliveryData SendRate::GetDeliveryData(uint64_t current_time) const {
  // start time relative to the current time for this send
  double start_time = 0.0;
  if (last_send_started_time_ != 0 && current_rate_ > 0) {
    // Use integer subtraction to avoid rounding errors, getting everything
    // with a zero base of 'now' to maximize precision.
    // Since we have uint64_ts and want a signed double result we need to
    // care about argument ordering to get a valid result.
    const double send_start_time_relative_to_now =
        current_time > last_send_started_time_
            ? -static_cast<double>(current_time - last_send_started_time_)
            : static_cast<double>(last_send_started_time_ - current_time);
    const double predicted_end_time =
        send_start_time_relative_to_now +
        last_send_bytes_outstanding_ / current_rate_;
    if (predicted_end_time > start_time) start_time = predicted_end_time;
  }
  if (current_rate_ <= 0) {
    return DeliveryData{(start_time + rtt_usec_ * 500.0) * 1e-9, 1e14};
  } else {
    return DeliveryData{(start_time + rtt_usec_ * 500.0) * 1e-9,
                        current_rate_ * 1e9};
  }
}

void SendRate::AddData(Json::Object& obj) const {
  if (last_send_started_time_ != 0) {
    obj["send_start_time"] = Json::FromNumber(last_send_started_time_);
    obj["send_size"] = Json::FromNumber(last_send_bytes_outstanding_);
  }
  obj["current_rate"] = Json::FromNumber(current_rate_);
  obj["rtt"] = Json::FromNumber(rtt_usec_);
  obj["rate_measurement_age"] =
      Json::FromNumber((Timestamp::Now() - last_rate_measurement_).millis());
}

///////////////////////////////////////////////////////////////////////////////
// OutputBuffers

void OutputBuffers::Reader::EndReadNext() {
  mu_.Lock();
  reading_ = false;
  if (GPR_UNLIKELY(!frames_.empty())) {
    // Cancellation -- we need to return the frames to the output buffer.
    // This probably messes up ordering, which messes up fairness (ordering has
    // no semantic meaning here), but this edge puts us far from
    // critical path anyway.
    auto frames = std::move(frames_);
    frames_.clear();
    mu_.Unlock();
    for (auto& frame : frames) {
      output_buffers_->Write(frame.payload_tag, std::move(frame.frame));
    }
  } else {
    mu_.Unlock();
  }
}

Poll<std::vector<OutputBuffers::QueuedFrame>>
OutputBuffers::Reader::PollReadNext() {
  GRPC_LATENT_SEE_INNER_SCOPE("OutputBuffers::PollReadNext");
  mu_.Lock();
  while (true) {
    GRPC_LATENT_SEE_INNER_SCOPE("OutputBuffers::PollReadNext::loop");
    if (frames_.empty()) {
      if (!reading_) {
        reading_ = true;
        output_buffers_->WakeupScheduler();
      }
      waker_ = GetContext<Activity>()->MakeNonOwningWaker();
      mu_.Unlock();
      return Pending{};
    }
    DCHECK(!reading_);
    auto frames = std::move(frames_);
    frames_.clear();
    mu_.Unlock();
    return std::move(frames);
  }
}

void OutputBuffers::Reader::SetNetworkMetrics(
    const std::optional<SendRate::NetworkSend>& network_send,
    const SendRate::NetworkMetrics& metrics) {
  mu_.Lock();
  send_rate_.SetNetworkMetrics(network_send, metrics);
  mu_.Unlock();
  output_buffers_->WakeupScheduler();
}

Json::Object OutputBuffers::Reader::ToJson() {
  MutexLock lock(&mu_);
  Json::Object reader_data;
  reader_data["reading"] = Json::FromBool(reading_);
  send_rate_.AddData(reader_data);
  if (!frames_.empty()) {
    Json::Array queued_frames;
    for (auto& frame : frames_) {
      Json::Object frame_data;
      frame_data["payload_tag"] = Json::FromNumber(frame.payload_tag);
      frame_data["header"] = Json::FromString(
          absl::ConvertVariantTo<FrameInterface&>(frame.frame->payload)
              .ToString());
      frame_data["mpsc_tokens"] = Json::FromNumber(frame.frame.tokens());
      queued_frames.emplace_back(Json::FromObject(std::move(frame_data)));
    }
    reader_data["queued_frames"] = Json::FromArray(std::move(queued_frames));
  }
  return reader_data;
}

void OutputBuffers::AddData(channelz::DataSink sink) {
  Json::Object obj;
  obj["num_readers"] =
      Json::FromNumber(num_readers_.load(std::memory_order_relaxed));
  obj["encode_alignment"] = Json::FromNumber(encode_alignment_);
  obj["scheduling_state"] =
      Json::FromNumber(scheduling_state_.load(std::memory_order_relaxed));
  obj["scheduler"] = Json::FromString(scheduler_->Config());
  sink.AddAdditionalInfo("outputBuffers", std::move(obj));
  scheduling_party_->ToJson([sink](Json::Object obj) mutable {
    sink.AddAdditionalInfo("schedulingParty", std::move(obj));
  });
}

RefCountedPtr<OutputBuffers::Reader> OutputBuffers::MakeReader(uint32_t id) {
  MutexLock lock(&mu_reader_data_);
  if (readers_.size() <= id) {
    readers_.resize(id + 1);
  }
  RefCountedPtr<Reader>& reader = readers_[id];
  DCHECK_EQ(reader.get(), nullptr);
  reader = MakeRefCounted<Reader>(Ref(), id);
  num_readers_.fetch_add(1, std::memory_order_relaxed);
  return reader;
}

void OutputBuffers::DestroyReader(uint32_t id) {
  mu_reader_data_.Lock();
  RefCountedPtr<Reader> reader = std::move(readers_[id]);
  DCHECK_NE(reader.get(), nullptr);
  mu_reader_data_.Unlock();
  reader->mu_.Lock();
  reader->reading_ = false;
  auto waker = std::move(reader->waker_);
  reader->mu_.Unlock();
  waker.Wakeup();
  num_readers_.fetch_sub(1, std::memory_order_relaxed);
}

void OutputBuffers::WakeupScheduler() {
  GRPC_LATENT_SEE_INNER_SCOPE("OutputBuffers::WakeupScheduler");
  auto state = scheduling_state_.load(std::memory_order_acquire);
  while (true) {
    switch (state) {
      case kSchedulingProcessing:
        if (!scheduling_state_.compare_exchange_weak(
                state, kSchedulingWorkAvailable, std::memory_order_release)) {
          continue;
        }
        return;
      case kSchedulingWorkAvailable:
        return;
      default: {
        // Idle: value is a pointer to a waker.
        Waker* waker = reinterpret_cast<Waker*>(state);
        if (!scheduling_state_.compare_exchange_weak(
                state, kSchedulingWorkAvailable, std::memory_order_release)) {
          continue;
        }
        waker->Wakeup();
        delete waker;
        return;
      }
    }
    LOG(FATAL) << "Unreachable state: " << state;
  }
}

Poll<Empty> OutputBuffers::SchedulerPollForWork() {
  GRPC_LATENT_SEE_INNER_SCOPE("OutputBuffers::SchedulerPollForWork");
  auto state = scheduling_state_.load(std::memory_order_acquire);
  while (true) {
    switch (state) {
      case kSchedulingProcessing: {
        // We were processing, now we're done.
        Waker* waker = new Waker(GetContext<Activity>()->MakeNonOwningWaker());
        if (!scheduling_state_.compare_exchange_weak(
                state, reinterpret_cast<uintptr_t>(waker),
                std::memory_order_acq_rel)) {
          delete waker;
          continue;
        }
        return Pending{};
      }
      case kSchedulingWorkAvailable: {
        scheduling_state_.store(kSchedulingProcessing,
                                std::memory_order_relaxed);
        return Empty{};
      }
      default:
        return Pending{};
    }
    LOG(FATAL) << "Unreachable state: " << state;
  }
}

void OutputBuffers::Schedule() {
  GRPC_LATENT_SEE_INNER_SCOPE("OutputBuffers::Schedule");
  auto* first_message = frames_queue_.Peek();
  if (first_message == nullptr) return;
  std::vector<SchedulingData> scheduling_data;
  uint64_t queued_tokens = 0;
  {
    GRPC_LATENT_SEE_INNER_SCOPE("OutputBuffers::Schedule::CollectData1");
    MutexLock lock(&mu_reader_data_);
    scheduling_data.reserve(readers_.size());
    for (const auto& reader : readers_) {
      scheduling_data.emplace_back(reader);
    }
    queued_tokens = mpsc_probe_.QueuedTokens();
  }
  // Note that we use the number of queued tokens as the scheduling metric,
  // not number of bytes on the wire.
  // When we enqueue to the mpsc we don't know the wire size, since we don't
  // know that the bytes are going out over a TCP collective, or whether they'll
  // hit data endpoints or be inlined on a control channel.
  scheduler_->NewStep(queued_tokens, first_message->frame.tokens());
  const auto now = clock_->Now();
  bool any_readers = false;
  {
    GRPC_LATENT_SEE_INNER_SCOPE("OutputBuffers::Schedule::CollectData2");
    for (size_t i = 0; i < scheduling_data.size(); ++i) {
      SchedulingData& scheduling = scheduling_data[i];
      if (scheduling.reader == nullptr) continue;
      scheduling.reader->mu_.Lock();
      auto delivery_data = scheduling.reader->send_rate_.GetDeliveryData(now);
      bool reading = scheduling.reader->reading_;
      if (reading) any_readers = true;
      scheduling.reader->mu_.Unlock();
      scheduler_->AddChannel(i, reading, delivery_data.start_time,
                             delivery_data.bytes_per_second);
    }
  }
  if (!any_readers) return;
  {
    GRPC_LATENT_SEE_INNER_SCOPE("OutputBuffers::Schedule::MakePlan");
    scheduler_->MakePlan(*ztrace_collector_);
  }
  {
    GRPC_LATENT_SEE_INNER_SCOPE("OutputBuffers::Schedule::PlaceMessages");
    while (true) {
      auto* message = frames_queue_.Peek();
      if (message == nullptr) break;
      auto selected_reader =
          scheduler_->AllocateMessage(message->frame.tokens());
      if (!selected_reader.has_value()) {
        // No reader is ready to read this frame.
        // We'll try again later.
        break;
      }
      ztrace_collector_->Append([this, message, selected_reader]() {
        return WriteLargeFrameHeaderTrace{message->payload_tag,
                                          WriteSizeForFrame(*message),
                                          *selected_reader};
      });
      SchedulingData& scheduling = scheduling_data[*selected_reader];
      scheduling.queued_bytes += WriteSizeForFrame(*message);
      scheduling.frames.emplace_back(std::move(*message));
      frames_queue_.Pop();
    }
  }
  {
    GRPC_LATENT_SEE_INNER_SCOPE("OutputBuffers::Schedule::PublishSchedule");
    for (auto& scheduling : scheduling_data) {
      if (scheduling.frames.empty()) continue;
      auto& reader = scheduling.reader;
      DCHECK_NE(reader.get(), nullptr);
      reader->mu_.Lock();
      if (!reader->reading_) {
        // Frames were assigned to this reader, but it's either not reading
        // or not allocated anymore.
        auto frames = std::move(scheduling.frames);
        scheduling.frames.clear();
        reader->mu_.Unlock();
        for (auto& frame : frames) {
          Write(frame.payload_tag, std::move(frame.frame));
        }
        continue;
      }
      reader->send_rate_.StartSend(scheduling.queued_bytes);
      reader->frames_ = std::move(scheduling.frames);
      reader->reading_ = false;
      auto waker = std::move(reader->waker_);
      reader->mu_.Unlock();
      waker.WakeupAsync();
    }
  }
}

void OutputBuffers::Write(uint64_t payload_tag,
                          MpscQueued<OutgoingFrame> output_buffer) {
  GRPC_LATENT_SEE_INNER_SCOPE("OutputBuffers::Write");
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: " << this
      << " Queue data frame write, payload_tag=" << payload_tag;
  mu_write_.Lock();
  frames_queue_.Push(QueuedFrame{payload_tag, std::move(output_buffer)});
  mu_write_.Unlock();
  WakeupScheduler();
}

///////////////////////////////////////////////////////////////////////////////
// SecureFrameQueue

void SecureFrameQueue::Write(SliceBuffer buffer) {
  ReleasableMutexLock lock(&mu_);
  uint32_t frame_length = buffer.Length();
  uint32_t frame_padding =
      DataConnectionPadding(frame_length, encode_alignment_);
  uint32_t header_padding = DataConnectionPadding(
      TcpDataFrameHeader::kFrameHeaderSize, encode_alignment_);
  auto slice = MutableSlice::CreateUninitialized(
      TcpDataFrameHeader::kFrameHeaderSize + header_padding);
  TcpDataFrameHeader{0, 0, frame_length}.Serialize(slice.data());
  if (header_padding != 0) {
    memset(slice.data() + TcpDataFrameHeader::kFrameHeaderSize, 0,
           frame_padding);
  }
  all_frames_.Append(Slice(std::move(slice)));
  all_frames_.TakeAndAppend(buffer);
  if (frame_padding != 0) {
    auto padding = MutableSlice::CreateUninitialized(frame_padding);
    memset(padding.data(), 0, frame_padding);
    all_frames_.Append(Slice(std::move(padding)));
  }
  auto waker = std::move(read_waker_);
  lock.Release();
  waker.Wakeup();
}

///////////////////////////////////////////////////////////////////////////////
// InputQueues

InputQueue::ReadTicket InputQueue::Read(uint64_t payload_tag) {
  MutexLock lock(&mu_);
  if (read_requested_.Set(payload_tag)) {
    return ReadTicket(
        MakeRefCounted<Completion>(
            payload_tag, absl::UnavailableError("Duplicate read requested")),
        nullptr);
  }
  auto it = completions_.find(payload_tag);
  if (it != completions_.end()) {
    return ReadTicket(it->second, nullptr);
  }
  auto completion = MakeRefCounted<Completion>(payload_tag);
  completions_.emplace(payload_tag, completion);
  return ReadTicket(std::move(completion), Ref());
}

void InputQueue::CompleteRead(uint64_t payload_tag, SliceBuffer buffer) {
  GRPC_LATENT_SEE_INNER_SCOPE("InputQueue::CompleteRead");
  if (payload_tag == 0) return;
  mu_.Lock();
  if (!closed_error_.ok()) {
    mu_.Unlock();
    return;
  }
  if (read_completed_.Set(payload_tag)) {
    mu_.Unlock();
    return;
  }
  auto c = completions_.extract(payload_tag);
  if (!c.empty()) {
    auto& completion = c.mapped();
    mu_.Unlock();
    completion->mu.Lock();
    completion->result.emplace(std::move(buffer));
    completion->ready = true;
    auto waker = std::move(completion->waker);
    completion->mu.Unlock();
    waker.Wakeup();
    return;
  }
  completions_.emplace(
      payload_tag, MakeRefCounted<Completion>(payload_tag, std::move(buffer)));
  mu_.Unlock();
}

void InputQueue::Cancel(Completion* completion) {
  mu_.Lock();
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: Cancel payload_tag #" << completion->payload_tag;
  read_completed_.Set(completion->payload_tag);
  auto c = completions_.extract(completion->payload_tag);
  if (!c.empty()) {
    auto& completion = c.mapped();
    mu_.Unlock();
    completion->mu.Lock();
    auto waker = std::move(completion->waker);
    completion->mu.Unlock();
    waker.Wakeup();
  } else {
    mu_.Unlock();
  }
}

void InputQueue::AddData(channelz::DataSink sink) {
  MutexLock lock(&mu_);
  Json::Object obj;
  obj["read_requested"] = Json::FromString(absl::StrCat(read_requested_));
  obj["read_completed"] = Json::FromString(absl::StrCat(read_completed_));
  // TODO(ctiller): log completions_
  if (!closed_error_.ok()) {
    obj["closed_error"] = Json::FromString(closed_error_.ToString());
  }
  sink.AddAdditionalInfo("inputQueue", std::move(obj));
}

void InputQueue::SetClosed(absl::Status status) {
  mu_.Lock();
  if (!closed_error_.ok()) {
    mu_.Unlock();
    return;
  }
  if (status.ok()) status = absl::UnavailableError("transport closed");
  closed_error_ = std::move(status);
  auto completions = std::move(completions_);
  completions_.clear();
  Waker await_closed = std::move(await_closed_);
  mu_.Unlock();
  await_closed.Wakeup();
  for (auto& [tag, completion] : completions) {
    completion->mu.Lock();
    if (!completion->ready) {
      auto waker = std::move(completion->waker);
      completion->mu.Unlock();
      waker.Wakeup();
    } else {
      completion->mu.Unlock();
    }
  }
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

class MetricsCollector
    : public RefCounted<MetricsCollector, NonPolymorphicRefCount> {
 public:
  explicit MetricsCollector(
      Clock* clock,
      grpc_event_engine::experimental::EventEngine::Endpoint& endpoint)
      : clock_(clock), telemetry_info_(endpoint.GetTelemetryInfo()) {
    if (telemetry_info_ == nullptr) return;
    delivery_rate_ = telemetry_info_->GetMetricKey("delivery_rate");
    rtt_ = telemetry_info_->GetMetricKey("net_rtt_usec");
    if (!rtt_.has_value()) rtt_ = telemetry_info_->GetMetricKey("srtt");
    data_notsent_ = telemetry_info_->GetMetricKey("data_notsent");
    byte_offset_ = telemetry_info_->GetMetricKey("byte_offset");
    absl::InlinedVector<size_t, 4> keys;
    if (delivery_rate_.has_value()) {
      keys.push_back(*delivery_rate_);
    }
    if (byte_offset_.has_value()) keys.push_back(*byte_offset_);
    if (rtt_.has_value()) keys.push_back(*rtt_);
    if (data_notsent_.has_value()) keys.push_back(*data_notsent_);
    requested_metrics_ = telemetry_info_->GetMetricsSet(keys);
  }

  bool HasAnyMetrics() const {
    return delivery_rate_.has_value() || rtt_.has_value() ||
           data_notsent_.has_value();
  }

  std::shared_ptr<
      grpc_event_engine::experimental::EventEngine::Endpoint::MetricsSet>
  requested_metrics() const {
    return requested_metrics_;
  }

  std::tuple<SendRate::NetworkMetrics, std::optional<uint64_t>>
  GetNetworkMetrics(
      grpc_event_engine::experimental::EventEngine::Endpoint::WriteEvent event,
      absl::Span<const grpc_event_engine::experimental::EventEngine::Endpoint::
                     WriteMetric>
          metrics,
      uint64_t message_size) const {
    SendRate::NetworkMetrics net_metrics;
    std::optional<int64_t> data_notsent;
    std::optional<int64_t> byte_offset;
    for (const auto& metric : metrics) {
      if (metric.key == delivery_rate_) {
        net_metrics.bytes_per_nanosecond = metric.value * 1e-9;
      }
      if (metric.key == rtt_ && metric.value > 0) {
        net_metrics.rtt_usec = metric.value;
      }
      if (metric.key == data_notsent_) {
        data_notsent = metric.value;
      }
      if (metric.key == byte_offset_) {
        byte_offset = metric.value;
      }
    }
    if (event != grpc_event_engine::experimental::EventEngine::Endpoint::
                     WriteEvent::kSendMsg) {
      byte_offset = std::nullopt;
      data_notsent = std::nullopt;
    }
    std::optional<uint64_t> bytes_outstanding;
    if (byte_offset.has_value() && data_notsent.has_value()) {
      bytes_outstanding = *data_notsent + message_size - *byte_offset;
    }
    return {net_metrics, bytes_outstanding};
  }

  grpc_event_engine::experimental::EventEngine::Endpoint::WriteEventSink
  MakeWriteEventSink(size_t write_size,
                     RefCountedPtr<OutputBuffers::Reader> reader,
                     std::shared_ptr<TcpZTraceCollector> ztrace_collector) {
    using grpc_event_engine::experimental::EventEngine;
    return EventEngine::Endpoint::WriteEventSink(
        requested_metrics(),
        {EventEngine::Endpoint::WriteEvent::kSendMsg,
         EventEngine::Endpoint::WriteEvent::kSent,
         EventEngine::Endpoint::WriteEvent::kAcked},
        [reader, ztrace_collector, write_size, self = Ref()](
            EventEngine::Endpoint::WriteEvent event, absl::Time timestamp,
            std::vector<EventEngine::Endpoint::WriteMetric> metrics) {
          GRPC_LATENT_SEE_PARENT_SCOPE("MetricsCollector::WriteEventSink");
          ztrace_collector->Append([event, timestamp, &metrics,
                                    telemetry_info = self->telemetry_info_,
                                    &reader]() {
            EndpointWriteMetricsTrace trace{timestamp, event, {}, reader->id()};
            trace.metrics.reserve(metrics.size());
            for (const auto [id, value] : metrics) {
              if (auto name = telemetry_info->GetMetricName(id);
                  name.has_value()) {
                trace.metrics.push_back({*name, value});
              }
            }
            return trace;
          });
          auto [net_metrics, data_notsent] =
              self->GetNetworkMetrics(event, metrics, write_size);
          std::optional<SendRate::NetworkSend> network_send;
          if (event == EventEngine::Endpoint::WriteEvent::kSent ||
              data_notsent.has_value()) {
            network_send = SendRate::NetworkSend{
                self->clock_->Now() +
                    absl::ToInt64Nanoseconds(timestamp - absl::Now()),
                static_cast<uint64_t>(data_notsent.value_or(write_size))};
          }
          reader->SetNetworkMetrics(network_send, net_metrics);
        });
  }

 private:
  Clock* const clock_;
  std::optional<size_t> delivery_rate_;
  std::optional<size_t> rtt_;
  std::optional<size_t> data_notsent_;
  std::optional<size_t> byte_offset_;
  std::shared_ptr<
      grpc_event_engine::experimental::EventEngine::Endpoint::MetricsSet>
      requested_metrics_;
  std::shared_ptr<
      grpc_event_engine::experimental::EventEngine::Endpoint::TelemetryInfo>
      telemetry_info_;
};
}  // namespace

auto Endpoint::PullDataPayload(RefCountedPtr<EndpointContext> ctx) {
  return Map(
      ctx->reader->Next(),
      [ctx](
          ValueOrFailure<std::vector<OutputBuffers::QueuedFrame>> queued_frames)
          -> ValueOrFailure<SliceBuffer> {
        if (!queued_frames.ok()) return Failure{};
        GRPC_TRACE_LOG(chaotic_good, INFO)
            << "CHAOTIC_GOOD: " << ctx->reader.get() << " "
            << ResolvedAddressToString(ctx->endpoint->GetPeerAddress())
                   .value_or("peer-unknown")
            << " Write " << queued_frames->size()
            << " frames to data endpoint #" << ctx->id;
        using grpc_event_engine::experimental::EventEngine;

        GRPC_LATENT_SEE_INNER_SCOPE("SerializePayload");
        // Frame everything into a slice buffer.
        SliceBuffer buffer;
        const size_t header_padding = DataConnectionPadding(
            TcpDataFrameHeader::kFrameHeaderSize, ctx->encode_alignment);
        const size_t header_size =
            TcpDataFrameHeader::kFrameHeaderSize + header_padding;
        auto header_frames = MutableSlice::CreateUninitialized(
            header_size * queued_frames->size() + ctx->encode_alignment);
        auto padding_mut =
            header_frames.TakeFirstNoInline(ctx->encode_alignment);
        memset(padding_mut.data(), 0, ctx->encode_alignment);
        auto padding = Slice(std::move(padding_mut));
        for (size_t i = 0; i < queued_frames->size(); ++i) {
          auto& queued_frame = (*queued_frames)[i];
          auto& frame = absl::ConvertVariantTo<FrameInterface&>(
              queued_frame.frame->payload);
          auto hdr = header_frames.TakeFirstNoInline(header_size);
          const uint32_t payload_length = frame.MakeHeader().payload_length;
          TcpDataFrameHeader{queued_frame.payload_tag, ctx->clock->Now(),
                             payload_length}
              .Serialize(hdr.data());
          memset(hdr.data() + TcpDataFrameHeader::kFrameHeaderSize, 0,
                 header_padding);
          buffer.AppendIndexed(Slice(std::move(hdr)));
          frame.SerializePayload(buffer);
          const size_t frame_padding =
              DataConnectionPadding(payload_length, ctx->encode_alignment);
          if (frame_padding != 0) {
            buffer.AppendIndexed(padding.RefSubSlice(0, frame_padding));
          }
        }
        return std::move(buffer);
      });
}

auto Endpoint::WriteLoop(RefCountedPtr<EndpointContext> ctx) {
  auto metrics_collector = MakeRefCounted<MetricsCollector>(
      ctx->clock, *ctx->endpoint->GetEventEngineEndpoint());
  if (!metrics_collector->HasAnyMetrics()) {
    metrics_collector.reset();
  }
  return Loop([ctx = std::move(ctx),
               metrics_collector = std::move(metrics_collector)]() {
    return TrySeq(
        GRPC_LATENT_SEE_PROMISE(
            "DataEndpointPullPayload",
            Race(PullDataPayload(ctx),
                 Map(ctx->secure_frame_queue->Next(),
                     [](auto x) -> ValueOrFailure<SliceBuffer> {
                       return std::move(x);
                     }))),
        [ctx, metrics_collector](SliceBuffer buffer) {
          ctx->ztrace_collector->Append(
              WriteBytesToEndpointTrace{buffer.Length(), ctx->id});
          PromiseEndpoint::WriteArgs write_args;
          auto now = Timestamp::Now();
          if (metrics_collector != nullptr &&
              now - ctx->last_metrics_update > Duration::Milliseconds(100)) {
            ctx->last_metrics_update = now;
            write_args.set_metrics_sink(metrics_collector->MakeWriteEventSink(
                buffer.Length(), ctx->reader, ctx->ztrace_collector));
          }
          return Map(
              AddGeneratedErrorPrefix(
                  [ctx]() {
                    return absl::StrCat(
                        "DATA_CHANNEL: ",
                        ResolvedAddressToString(ctx->endpoint->GetPeerAddress())
                            .value_or("peer-unknown"),
                        "#", ctx->id);
                  },
                  GRPC_LATENT_SEE_PROMISE(
                      "DataEndpointWrite",
                      ctx->endpoint->Write(std::move(buffer),
                                           std::move(write_args)))),
              [ctx](absl::Status status) {
                ctx->ztrace_collector->Append([id = ctx->id, &status]() {
                  return FinishWriteBytesToEndpointTrace{id, status};
                });
                GRPC_TRACE_LOG(chaotic_good, INFO)
                    << "CHAOTIC_GOOD: " << ctx->reader.get() << " "
                    << "Write done to data endpoint #" << ctx->id
                    << " status: " << status;
                return status;
              });
        },
        [id = ctx->id, reader = ctx->reader]() -> LoopCtl<absl::Status> {
          GRPC_TRACE_LOG(chaotic_good, INFO)
              << "CHAOTIC_GOOD: " << reader.get() << " "
              << "Write done to data endpoint #" << id;
          return Continue{};
        });
  });
}

auto Endpoint::ReadLoop(RefCountedPtr<EndpointContext> ctx) {
  return Loop([ctx = std::move(ctx)]() {
    return TrySeq(
        GRPC_LATENT_SEE_PROMISE(
            "DataEndpointReadHdr",
            ctx->endpoint->ReadSlice(
                TcpDataFrameHeader::kFrameHeaderSize +
                DataConnectionPadding(TcpDataFrameHeader::kFrameHeaderSize,
                                      ctx->decode_alignment))),
        [id = ctx->id](Slice frame_header) {
          auto hdr = TcpDataFrameHeader::Parse(frame_header.data());
          GRPC_TRACE_LOG(chaotic_good, INFO)
              << "CHAOTIC_GOOD: Read "
              << (hdr.ok() ? absl::StrCat(*hdr) : hdr.status().ToString())
              << " on data connection #" << id;
          return hdr;
        },
        [ctx](TcpDataFrameHeader frame_header) {
          ctx->ztrace_collector->Append(ReadDataHeaderTrace{frame_header});
          return Map(
              TryStaple(GRPC_LATENT_SEE_PROMISE(
                            "DataEndpointRead",
                            ctx->endpoint->Read(frame_header.payload_length +
                                                DataConnectionPadding(
                                                    frame_header.payload_length,
                                                    ctx->decode_alignment))),
                        frame_header),
              [id = ctx->id, frame_header](auto x) {
                GRPC_TRACE_LOG(chaotic_good, INFO)
                    << "CHAOTIC_GOOD: Complete read " << frame_header
                    << " on data connection #" << id
                    << " status: " << x.status();
                return x;
              });
        },
        [ctx](std::tuple<SliceBuffer, TcpDataFrameHeader> buffer_frame)
            -> LoopCtl<absl::Status> {
          auto& [buffer, frame_header] = buffer_frame;
          GRPC_TRACE_LOG(chaotic_good, INFO)
              << "CHAOTIC_GOOD: Complete read " << frame_header
              << " on data connection #" << ctx->id;
          buffer.RemoveLastNBytesNoInline(DataConnectionPadding(
              frame_header.payload_length, ctx->decode_alignment));
          if (GPR_UNLIKELY(frame_header.payload_tag ==
                           kSecurityFramePayloadTag)) {
            ReceiveSecurityFrame(*ctx->endpoint, std::move(buffer));
          } else {
            ctx->input_queues->CompleteRead(frame_header.payload_tag,
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

void Endpoint::ToJson(absl::AnyInvocable<void(Json::Object)> sink) {
  Json::Object obj;
  obj["id"] = Json::FromNumber(ctx_->id);
  obj["now"] = Json::FromNumber(ctx_->clock->Now());
  obj["encode_alignment"] = Json::FromNumber(ctx_->encode_alignment);
  obj["decode_alignment"] = Json::FromNumber(ctx_->decode_alignment);
  obj["secure_frame_bytes_queued"] =
      Json::FromNumber(ctx_->secure_frame_queue->InstantaneousQueuedBytes());
  obj["enable_tracing"] = Json::FromBool(ctx_->enable_tracing);
  obj["reader"] = Json::FromObject(ctx_->reader->ToJson());
  party_->ToJson([root = std::move(obj),
                  sink = std::move(sink)](Json::Object obj) mutable {
    root["party"] = Json::FromObject(std::move(obj));
    sink(std::move(root));
  });
}

Endpoint::Endpoint(uint32_t id, uint32_t encode_alignment,
                   uint32_t decode_alignment, Clock* clock,
                   RefCountedPtr<OutputBuffers> output_buffers,
                   RefCountedPtr<InputQueue> input_queues,
                   PendingConnection pending_connection, bool enable_tracing,
                   TransportContextPtr ctx,
                   std::shared_ptr<TcpZTraceCollector> ztrace_collector) {
  auto ep_ctx = MakeRefCounted<EndpointContext>();
  ctx_ = ep_ctx;
  ep_ctx->id = id;
  ep_ctx->encode_alignment = encode_alignment;
  ep_ctx->decode_alignment = decode_alignment;
  ep_ctx->enable_tracing = enable_tracing;
  ep_ctx->output_buffers = std::move(output_buffers);
  ep_ctx->input_queues = std::move(input_queues);
  ep_ctx->ztrace_collector = std::move(ztrace_collector);
  ep_ctx->arena = SimpleArenaAllocator(0)->MakeArena();
  ep_ctx->arena->SetContext(ctx->event_engine.get());
  ep_ctx->clock = clock;
  ep_ctx->transport_ctx = std::move(ctx);
  ep_ctx->reader = ep_ctx->output_buffers->MakeReader(ep_ctx->id);
  party_ = Party::Make(ep_ctx->arena);
  party_->Spawn(
      "write",
      [ep_ctx, pending_connection = std::move(pending_connection)]() mutable {
        return TrySeq(
            pending_connection.Await(),
            [ep_ctx = std::move(ep_ctx)](PromiseEndpoint ep) mutable {
              GRPC_TRACE_LOG(chaotic_good, INFO)
                  << "CHAOTIC_GOOD: data endpoint " << ep_ctx->id << " to "
                  << grpc_event_engine::experimental::ResolvedAddressToString(
                         ep.GetPeerAddress())
                         .value_or("<<unknown peer address>>")
                  << " ready";
              RefCountedPtr<channelz::SocketNode> socket_node;
              if (ep_ctx->transport_ctx->socket_node != nullptr) {
                auto* channelz_endpoint =
                    grpc_event_engine::experimental::QueryExtension<
                        grpc_event_engine::experimental::ChannelzExtension>(
                        ep.GetEventEngineEndpoint().get());
                socket_node = MakeSocketNode(ep_ctx->transport_ctx, ep);
                socket_node->AddParent(
                    ep_ctx->transport_ctx->socket_node.get());
                if (channelz_endpoint != nullptr) {
                  channelz_endpoint->SetSocketNode(socket_node);
                }
              }
              auto endpoint = std::make_shared<PromiseEndpoint>(std::move(ep));
              ep_ctx->endpoint = endpoint;
              // Enable RxMemoryAlignment and RPC receive coalescing after the
              // transport setup is complete. At this point all the settings
              // frames should have been read.
              if (ep_ctx->decode_alignment != 1) {
                endpoint->EnforceRxMemoryAlignmentAndCoalescing();
              }
              if (ep_ctx->enable_tracing) {
                auto* epte = grpc_event_engine::experimental::QueryExtension<
                    grpc_event_engine::experimental::TcpTraceExtension>(
                    endpoint->GetEventEngineEndpoint().get());
                if (epte != nullptr) {
                  epte->SetTcpTracer(std::make_shared<DefaultTcpTracer>(
                      ep_ctx->transport_ctx->stats_plugin_group));
                }
              }
              ep_ctx->secure_frame_queue =
                  MakeRefCounted<SecureFrameQueue>(ep_ctx->encode_alignment);
              auto* transport_framing_endpoint_extension =
                  GetTransportFramingEndpointExtension(*endpoint);
              if (transport_framing_endpoint_extension != nullptr) {
                transport_framing_endpoint_extension->SetSendFrameCallback(
                    [ep_ctx](SliceBuffer* data) {
                      ep_ctx->secure_frame_queue->Write(std::move(*data));
                    });
              }
              auto read_party = Party::Make(ep_ctx->arena);
              read_party->Spawn(
                  "read",
                  [ep_ctx]() mutable { return ReadLoop(std::move(ep_ctx)); },
                  [ep_ctx](absl::Status status) {
                    GRPC_TRACE_LOG(chaotic_good, INFO)
                        << "CHAOTIC_GOOD: read party done: " << status;
                    ep_ctx->input_queues->SetClosed(std::move(status));
                  });
              return Map(GRPC_LATENT_SEE_PROMISE("DataEndpointWrite",
                                                 WriteLoop(std::move(ep_ctx))),
                         [read_party, socket_node = std::move(socket_node)](
                             auto x) { return x; });
            });
      },
      [ep_ctx](absl::Status status) {
        GRPC_TRACE_LOG(chaotic_good, INFO)
            << "CHAOTIC_GOOD: write party done: " << status;
        ep_ctx->input_queues->SetClosed(std::move(status));
      });
}

}  // namespace data_endpoints_detail

///////////////////////////////////////////////////////////////////////////////
// DataEndpoints

DataEndpoints::DataEndpoints(
    std::vector<PendingConnection> endpoints_vec, TransportContextPtr ctx,
    uint32_t encode_alignment, uint32_t decode_alignment,
    std::shared_ptr<TcpZTraceCollector> ztrace_collector, bool enable_tracing,
    std::string scheduler_config, data_endpoints_detail::Clock* clock)
    : channelz::DataSource(ctx->socket_node),
      output_buffers_(MakeRefCounted<data_endpoints_detail::OutputBuffers>(
          clock, encode_alignment, ztrace_collector,
          std::move(scheduler_config), ctx)),
      input_queues_(MakeRefCounted<data_endpoints_detail::InputQueue>()) {
  for (size_t i = 0; i < endpoints_vec.size(); ++i) {
    endpoints_.emplace_back(std::make_unique<data_endpoints_detail::Endpoint>(
        i, encode_alignment, decode_alignment, clock, output_buffers_,
        input_queues_, std::move(endpoints_vec[i]), enable_tracing, ctx,
        ztrace_collector));
  }
}

void DataEndpoints::AddData(channelz::DataSink sink) {
  output_buffers_->AddData(sink);
  input_queues_->AddData(sink);
  struct EndpointInfoCollector {
    explicit EndpointInfoCollector(int remaining)
        : remaining(remaining), endpoints(remaining) {}
    Mutex mu;
    int remaining ABSL_GUARDED_BY(mu) = 0;
    Json::Array endpoints ABSL_GUARDED_BY(mu);
  };
  MutexLock lock(&mu_);
  auto endpoint_info_collector =
      std::make_shared<EndpointInfoCollector>(endpoints_.size());
  for (size_t i = 0; i < endpoints_.size(); ++i) {
    endpoints_[i]->ToJson([endpoint_info_collector, i,
                           sink](Json::Object obj) mutable {
      MutexLock lock(&endpoint_info_collector->mu);
      endpoint_info_collector->endpoints[i] = Json::FromObject(std::move(obj));
      if (--endpoint_info_collector->remaining == 0) {
        sink.AddAdditionalInfo(
            "chaoticGoodDataEndpoints",
            {{"endpoints",
              Json::FromArray(std::move(endpoint_info_collector->endpoints))}});
      }
    });
  }
}

}  // namespace chaotic_good
}  // namespace grpc_core
