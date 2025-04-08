// Copyright 2023 gRPC authors.
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

#include "test/core/end2end/fuzzers/network_input.h"

#include <grpc/slice.h>
#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chaotic_good/tcp_frame_transport.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/varint.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/useful.h"
#include "test/core/end2end/fuzzers/fuzzer_input.pb.h"
#include "test/core/test_util/mock_endpoint.h"

using grpc_event_engine::experimental::EventEngine;

namespace grpc_core {

namespace {
grpc_slice SliceFromH2Frame(Http2Frame frame) {
  SliceBuffer buffer;
  Serialize(absl::Span<Http2Frame>(&frame, 1), buffer);
  return buffer.JoinIntoSlice().TakeCSlice();
}

SliceBuffer SliceBufferFromBytes(const std::string& bytes) {
  SliceBuffer buffer;
  buffer.Append(Slice::FromCopiedString(bytes));
  return buffer;
}

void AppendLength(size_t length, std::vector<uint8_t>* bytes) {
  VarintWriter<1> writer(length);
  uint8_t buffer[8];
  writer.Write(0, buffer);
  bytes->insert(bytes->end(), buffer, buffer + writer.length());
}

SliceBuffer SliceBufferFromSimpleHeaders(
    const fuzzer_input::SimpleHeaders& headers) {
  std::vector<uint8_t> temp;
  auto add_header = [&temp](absl::string_view key, absl::string_view value) {
    temp.push_back(0);
    AppendLength(key.length(), &temp);
    temp.insert(temp.end(), key.begin(), key.end());
    AppendLength(value.length(), &temp);
    temp.insert(temp.end(), value.begin(), value.end());
  };
  if (headers.has_status()) {
    add_header(":status", headers.status());
  }
  if (headers.has_scheme()) {
    add_header(":scheme", headers.scheme());
  }
  if (headers.has_method()) {
    add_header(":method", headers.method());
  }
  if (headers.has_authority()) {
    add_header(":authority", headers.authority());
  }
  if (headers.has_path()) {
    add_header(":path", headers.path());
  }
  for (const auto& header : headers.headers()) {
    if (header.has_key() && header.has_value()) {
      add_header(header.key(), header.value());
      ;
    }
    if (header.has_raw_bytes()) {
      for (auto c : header.raw_bytes()) {
        temp.push_back(static_cast<uint8_t>(c));
      }
    }
  }
  if (headers.has_grpc_timeout()) {
    add_header("grpc-timeout", headers.grpc_timeout());
  }
  if (headers.has_te()) {
    add_header("te", headers.te());
  }
  if (headers.has_content_type()) {
    add_header("content-type", headers.content_type());
  }
  if (headers.has_grpc_encoding()) {
    add_header("grpc-encoding", headers.grpc_encoding());
  }
  if (headers.has_grpc_internal_encoding_request()) {
    add_header("grpc-internal-encoding-request",
               headers.grpc_internal_encoding_request());
  }
  if (headers.has_grpc_accept_encoding()) {
    add_header("grpc-accept-encoding", headers.grpc_accept_encoding());
  }
  if (headers.has_user_agent()) {
    add_header("user-agent", headers.user_agent());
  }
  if (headers.has_grpc_message()) {
    add_header("grpc-message", headers.grpc_message());
  }
  if (headers.has_host()) {
    add_header("host", headers.host());
  }
  if (headers.has_endpoint_load_metrics_bin()) {
    add_header("endpoint-load-metrics-bin",
               headers.endpoint_load_metrics_bin());
  }
  if (headers.has_grpc_server_stats_bin()) {
    add_header("grpc-server-stats-bin", headers.grpc_server_stats_bin());
  }
  if (headers.has_grpc_trace_bin()) {
    add_header("grpc-trace-bin", headers.grpc_trace_bin());
  }
  if (headers.has_grpc_tags_bin()) {
    add_header("grpc-tags-bin", headers.grpc_tags_bin());
  }
  if (headers.has_x_envoy_peer_metadata()) {
    add_header("x-envoy-peer-metadata", headers.x_envoy_peer_metadata());
  }
  if (headers.has_grpc_status()) {
    add_header("grpc-status", headers.grpc_status());
  }
  if (headers.has_grpc_previous_rpc_attempts()) {
    add_header("grpc-previous-rpc-attempts",
               headers.grpc_previous_rpc_attempts());
  }
  if (headers.has_grpc_retry_pushback_ms()) {
    add_header("grpc-retry-pushback-ms", headers.grpc_retry_pushback_ms());
  }
  if (headers.has_grpclb_client_stats()) {
    add_header("grpclb_client_stats", headers.grpclb_client_stats());
  }
  if (headers.has_lb_token()) {
    add_header("lb-token", headers.lb_token());
  }
  if (headers.has_lb_cost_bin()) {
    add_header("lb-cost-bin", headers.lb_cost_bin());
  }
  if (headers.has_chaotic_good_connection_type()) {
    add_header("chaotic-good-connection-type",
               headers.chaotic_good_connection_type());
  }
  if (headers.has_chaotic_good_connection_id()) {
    add_header("chaotic-good-connection-id",
               headers.chaotic_good_connection_id());
  }
  if (headers.has_chaotic_good_alignment()) {
    add_header("chaotic-good-alignment", headers.chaotic_good_alignment());
  }
  SliceBuffer buffer;
  buffer.Append(Slice::FromCopiedBuffer(temp.data(), temp.size()));
  return buffer;
}

template <typename T>
SliceBuffer SliceBufferFromHeaderPayload(const T& payload) {
  switch (payload.payload_case()) {
    case T::kRawBytes:
      return SliceBufferFromBytes(payload.raw_bytes());
    case T::kSimpleHeader:
      return SliceBufferFromSimpleHeaders(payload.simple_header());
    case T::PAYLOAD_NOT_SET:
      break;
  }
  return SliceBuffer();
}

SliceBuffer ChaoticGoodFrame(const fuzzer_input::ChaoticGoodFrame& frame) {
  chaotic_good::TcpFrameHeader h;
  SliceBuffer suffix;
  h.header.stream_id = frame.stream_id();
  switch (frame.frame_type_case()) {
    case fuzzer_input::ChaoticGoodFrame::kKnownType:
      switch (frame.known_type()) {
        case fuzzer_input::ChaoticGoodFrame::SETTINGS:
          h.header.type = chaotic_good::FrameType::kSettings;
          break;
        case fuzzer_input::ChaoticGoodFrame::CLIENT_INITIAL_METADATA:
          h.header.type = chaotic_good::FrameType::kClientInitialMetadata;
          break;
        case fuzzer_input::ChaoticGoodFrame::MESSAGE:
          h.header.type = chaotic_good::FrameType::kMessage;
          break;
        case fuzzer_input::ChaoticGoodFrame::CLIENT_END_OF_STREAM:
          h.header.type = chaotic_good::FrameType::kClientEndOfStream;
          break;
        case fuzzer_input::ChaoticGoodFrame::SERVER_INITIAL_METADATA:
          h.header.type = chaotic_good::FrameType::kServerInitialMetadata;
          break;
        case fuzzer_input::ChaoticGoodFrame::SERVER_TRAILING_METADATA:
          h.header.type = chaotic_good::FrameType::kServerTrailingMetadata;
          break;
        case fuzzer_input::ChaoticGoodFrame::CANCEL:
          h.header.type = chaotic_good::FrameType::kCancel;
          break;
        default:
          break;
      }
      break;
    case fuzzer_input::ChaoticGoodFrame::kUnknownType:
      h.header.type =
          static_cast<chaotic_good::FrameType>(frame.unknown_type());
      break;
    case fuzzer_input::ChaoticGoodFrame::FRAME_TYPE_NOT_SET:
      h.header.type = chaotic_good::FrameType::kMessage;
      break;
  }
  h.header.stream_id = frame.stream_id();
  h.payload_tag = 0;
  h.header.payload_length = 0;
  auto proto_payload = [&](auto payload) {
    std::string temp = payload.SerializeAsString();
    h.header.payload_length = temp.length();
    suffix.Append(Slice::FromCopiedString(temp));
  };
  switch (frame.payload_case()) {
    case fuzzer_input::ChaoticGoodFrame::kPayloadNone:
    case fuzzer_input::ChaoticGoodFrame::PAYLOAD_NOT_SET:
      break;
    case fuzzer_input::ChaoticGoodFrame::kPayloadRawBytes:
      if (frame.payload_raw_bytes().empty()) break;
      h.header.payload_length = frame.payload_raw_bytes().length();
      suffix.Append(Slice::FromCopiedString(frame.payload_raw_bytes()));
      break;
    case fuzzer_input::ChaoticGoodFrame::kPayloadEmptyOfLength:
      h.header.payload_length =
          std::min<uint32_t>(65536, frame.payload_empty_of_length());
      suffix.Append(
          Slice::FromCopiedString(std::string(h.header.payload_length, 'a')));
      break;
    case fuzzer_input::ChaoticGoodFrame::kPayloadOtherConnectionId:
      h.payload_tag = frame.payload_other_connection_id().connection_id();
      h.header.payload_length = std::min<uint32_t>(
          32 * 1024 * 1024, frame.payload_other_connection_id().length());
      break;
    case fuzzer_input::ChaoticGoodFrame::kSettings:
      proto_payload(frame.settings());
      break;
    case fuzzer_input::ChaoticGoodFrame::kClientMetadata:
      proto_payload(frame.client_metadata());
      break;
    case fuzzer_input::ChaoticGoodFrame::kServerMetadata:
      proto_payload(frame.server_metadata());
      break;
  }
  uint8_t bytes[chaotic_good::TcpFrameHeader::kFrameHeaderSize];
  h.Serialize(bytes);
  SliceBuffer out;
  out.Append(Slice::FromCopiedBuffer(
      bytes, chaotic_good::TcpFrameHeader::kFrameHeaderSize));
  out.Append(suffix);
  return out;
}

void store32_little_endian(uint32_t value, unsigned char* buf) {
  buf[3] = static_cast<unsigned char>((value >> 24) & 0xFF);
  buf[2] = static_cast<unsigned char>((value >> 16) & 0xFF);
  buf[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
  buf[0] = static_cast<unsigned char>((value) & 0xFF);
}

grpc_slice SliceFromSegment(const fuzzer_input::InputSegment& segment) {
  switch (segment.payload_case()) {
    case fuzzer_input::InputSegment::kRawBytes:
      return grpc_slice_from_copied_buffer(segment.raw_bytes().data(),
                                           segment.raw_bytes().size());
    case fuzzer_input::InputSegment::kData:
      return SliceFromH2Frame(Http2DataFrame{
          segment.data().stream_id(), segment.data().end_of_stream(),
          SliceBufferFromBytes(segment.data().payload())});
    case fuzzer_input::InputSegment::kHeader:
      return SliceFromH2Frame(Http2HeaderFrame{
          segment.header().stream_id(),
          segment.header().end_headers(),
          segment.header().end_stream(),
          SliceBufferFromHeaderPayload(segment.header()),
      });
    case fuzzer_input::InputSegment::kContinuation:
      return SliceFromH2Frame(Http2ContinuationFrame{
          segment.continuation().stream_id(),
          segment.continuation().end_headers(),
          SliceBufferFromHeaderPayload(segment.header()),
      });
    case fuzzer_input::InputSegment::kRstStream:
      return SliceFromH2Frame(Http2RstStreamFrame{
          segment.rst_stream().stream_id(),
          segment.rst_stream().error_code(),
      });
    case fuzzer_input::InputSegment::kSettings: {
      std::vector<Http2SettingsFrame::Setting> settings;
      for (const auto& setting : segment.settings().settings()) {
        settings.push_back(Http2SettingsFrame::Setting{
            static_cast<uint16_t>(setting.id()),
            setting.value(),
        });
      }
      return SliceFromH2Frame(Http2SettingsFrame{
          segment.settings().ack(),
          std::move(settings),
      });
    }
    case fuzzer_input::InputSegment::kPing:
      return SliceFromH2Frame(Http2PingFrame{
          segment.ping().ack(),
          segment.ping().opaque(),
      });
    case fuzzer_input::InputSegment::kGoaway:
      return SliceFromH2Frame(Http2GoawayFrame{
          segment.goaway().last_stream_id(), segment.goaway().error_code(),
          Slice::FromCopiedString(segment.goaway().debug_data())});
    case fuzzer_input::InputSegment::kWindowUpdate:
      return SliceFromH2Frame(Http2WindowUpdateFrame{
          segment.window_update().stream_id(),
          segment.window_update().increment(),
      });
    case fuzzer_input::InputSegment::kSecurityFrame:
      return SliceFromH2Frame(Http2SecurityFrame{
          SliceBufferFromBytes(segment.security_frame().payload())});
    case fuzzer_input::InputSegment::kClientPrefix:
      return grpc_slice_from_static_string("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n");
    case fuzzer_input::InputSegment::kRepeatedZeros: {
      std::vector<char> zeros;
      zeros.resize(std::min<size_t>(segment.repeated_zeros(), 128 * 1024), 0);
      return grpc_slice_from_copied_buffer(zeros.data(), zeros.size());
    }
    case fuzzer_input::InputSegment::kChaoticGood: {
      return ChaoticGoodFrame(segment.chaotic_good())
          .JoinIntoSlice()
          .TakeCSlice();
    } break;
    case fuzzer_input::InputSegment::kFakeTransportFrame: {
      auto generate = [](absl::string_view payload) {
        uint32_t length = payload.length();
        std::vector<unsigned char> bytes;
        bytes.resize(4);
        store32_little_endian(length + 4, bytes.data());
        for (auto c : payload) {
          bytes.push_back(static_cast<unsigned char>(c));
        }
        return grpc_slice_from_copied_buffer(
            reinterpret_cast<const char*>(bytes.data()), bytes.size());
      };
      switch (segment.fake_transport_frame().payload_case()) {
        case fuzzer_input::FakeTransportFrame::kRawBytes:
          return generate(segment.fake_transport_frame().raw_bytes());
        case fuzzer_input::FakeTransportFrame::kMessageString:
          switch (segment.fake_transport_frame().message_string()) {
            default:
              return generate("UNKNOWN");
            case fuzzer_input::FakeTransportFrame::CLIENT_INIT:
              return generate("CLIENT_INIT");
            case fuzzer_input::FakeTransportFrame::SERVER_INIT:
              return generate("SERVER_INIT");
            case fuzzer_input::FakeTransportFrame::CLIENT_FINISHED:
              return generate("CLIENT_FINISHED");
            case fuzzer_input::FakeTransportFrame::SERVER_FINISHED:
              return generate("SERVER_FINISHED");
          }
        case fuzzer_input::FakeTransportFrame::PAYLOAD_NOT_SET:
          return generate("");
      }
    }
    case fuzzer_input::InputSegment::PAYLOAD_NOT_SET:
      break;
  }
  return grpc_empty_slice();
}

struct QueuedRead {
  QueuedRead(int delay_ms, SliceBuffer slices)
      : delay_ms(delay_ms), slices(std::move(slices)) {}
  int delay_ms;
  SliceBuffer slices;
};

std::vector<QueuedRead> MakeSchedule(
    const fuzzer_input::NetworkInput& network_input) {
  std::vector<QueuedRead> schedule;
  switch (network_input.value_case()) {
    case fuzzer_input::NetworkInput::kSingleReadBytes: {
      schedule.emplace_back(0, SliceBuffer(Slice::FromCopiedBuffer(
                                   network_input.single_read_bytes().data(),
                                   network_input.single_read_bytes().size())));
    } break;
    case fuzzer_input::NetworkInput::kInputSegments: {
      int delay_ms = 0;
      SliceBuffer building;
      for (const auto& segment : network_input.input_segments().segments()) {
        const int segment_delay = Clamp(segment.delay_ms(), 0, 1000);
        if (segment_delay != 0) {
          delay_ms += segment_delay;
          if (building.Length() != 0) {
            schedule.emplace_back(delay_ms, std::move(building));
          }
          building.Clear();
        }
        building.Append(Slice(SliceFromSegment(segment)));
      }
      if (building.Length() != 0) {
        ++delay_ms;
        schedule.emplace_back(delay_ms, std::move(building));
      }
    } break;
    case fuzzer_input::NetworkInput::VALUE_NOT_SET:
      break;
  }
  return schedule;
}

}  // namespace

Duration ScheduleReads(
    const fuzzer_input::NetworkInput& network_input,
    std::shared_ptr<grpc_event_engine::experimental::MockEndpointController>
        mock_endpoint_controller,
    grpc_event_engine::experimental::FuzzingEventEngine* event_engine) {
  int delay = 0;
  for (const auto& q : MakeSchedule(network_input)) {
    event_engine->RunAfterExactly(
        std::chrono::milliseconds(q.delay_ms),
        [mock_endpoint_controller,
         slices = q.slices.JoinIntoSlice()]() mutable {
          ExecCtx exec_ctx;
          mock_endpoint_controller->TriggerReadEvent(
              std::move(grpc_event_engine::experimental::internal::SliceCast<
                        grpc_event_engine::experimental::Slice>(slices)));
        });
    delay = std::max(delay, q.delay_ms);
  }
  event_engine->RunAfterExactly(std::chrono::milliseconds(delay + 1),
                                [mock_endpoint_controller] {
                                  ExecCtx exec_ctx;
                                  mock_endpoint_controller->NoMoreReads();
                                });
  return Duration::Milliseconds(delay + 2);
}

namespace {

void ReadForever(std::shared_ptr<EventEngine::Endpoint> ep) {
  bool finished;
  do {
    auto buffer =
        std::make_unique<grpc_event_engine::experimental::SliceBuffer>();
    auto buffer_ptr = buffer.get();
    finished = ep->Read(
        [ep, buffer = std::move(buffer)](absl::Status status) mutable {
          ExecCtx exec_ctx;
          if (!status.ok()) return;
          ReadForever(std::move(ep));
        },
        buffer_ptr, nullptr);
  } while (finished);
}

void ScheduleWritesForReads(
    std::shared_ptr<EventEngine::Endpoint> ep,
    grpc_event_engine::experimental::FuzzingEventEngine* event_engine,
    std::vector<QueuedRead> schedule) {
  class Scheduler {
   public:
    Scheduler(std::shared_ptr<EventEngine::Endpoint> ep,
              grpc_event_engine::experimental::FuzzingEventEngine* event_engine,
              std::vector<QueuedRead> schedule)
        : ep_(std::move(ep)),
          event_engine_(event_engine),
          schedule_(std::move(schedule)),
          it_(schedule_.begin()) {
      ScheduleNext();
    }

   private:
    void ScheduleNext() {
      if (it_ == schedule_.end()) {
        delete this;
        return;
      }
      event_engine_->RunAfterExactly(
          Duration::Milliseconds(it_->delay_ms - delay_consumed_),
          [this]() mutable {
            ExecCtx exec_ctx;
            delay_consumed_ = it_->delay_ms;
            writing_.Clear();
            writing_.Append(
                grpc_event_engine::experimental::internal::SliceCast<
                    grpc_event_engine::experimental::Slice>(
                    it_->slices.JoinIntoSlice()));
            if (ep_->Write(
                    [this](absl::Status status) {
                      ExecCtx exec_ctx;
                      FinishWrite(std::move(status));
                    },
                    &writing_, nullptr)) {
              FinishWrite(absl::OkStatus());
            }
          });
    }

    void FinishWrite(absl::Status status) {
      if (!status.ok()) {
        it_ = schedule_.end();
      } else {
        ++it_;
      }
      ScheduleNext();
    }

    std::shared_ptr<EventEngine::Endpoint> ep_;
    grpc_event_engine::experimental::FuzzingEventEngine* event_engine_;
    std::vector<QueuedRead> schedule_;
    std::vector<QueuedRead>::iterator it_;
    grpc_event_engine::experimental::SliceBuffer writing_;
    int delay_consumed_ = 0;
  };
  new Scheduler(std::move(ep), event_engine, std::move(schedule));
}

}  // namespace

Duration ScheduleConnection(
    const fuzzer_input::NetworkInput& network_input,
    grpc_event_engine::experimental::FuzzingEventEngine* event_engine,
    testing::FuzzingEnvironment environment, int port) {
  ChannelArgs channel_args =
      CoreConfiguration::Get()
          .channel_args_preconditioning()
          .PreconditionChannelArgs(
              CreateChannelArgsFromFuzzingConfiguration(
                  network_input.endpoint_config(), environment)
                  .ToC()
                  .get());
  auto schedule = MakeSchedule(network_input);
  Duration delay = Duration::Zero();
  for (const auto& q : schedule) {
    delay = std::max(
        delay,
        Duration::Milliseconds(q.delay_ms) +
            Duration::NanosecondsRoundUp(
                (q.slices.Length() * event_engine->max_delay_write()).count()));
  }
  delay += Duration::Milliseconds(network_input.connect_delay_ms()) +
           Duration::Milliseconds(network_input.connect_timeout_ms());
  event_engine->RunAfterExactly(
      Duration::Milliseconds(network_input.connect_delay_ms()),
      [event_engine, channel_args,
       connect_timeout_ms = network_input.connect_timeout_ms(),
       schedule = std::move(schedule), port]() mutable {
        ExecCtx exec_ctx;
        event_engine->Connect(
            [event_engine, schedule = std::move(schedule)](
                absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>>
                    endpoint) mutable {
              ExecCtx exec_ctx;
              if (!endpoint.ok()) {
                LOG(ERROR) << "Failed to connect: " << endpoint.status();
                return;
              }
              std::shared_ptr<EventEngine::Endpoint> ep =
                  std::move(endpoint.value());
              ReadForever(ep);
              ScheduleWritesForReads(std::move(ep), event_engine,
                                     std::move(schedule));
            },
            grpc_event_engine::experimental::ResolvedAddressMakeWild4(port),
            grpc_event_engine::experimental::ChannelArgsEndpointConfig(
                channel_args),
            channel_args.GetObject<ResourceQuota>()
                ->memory_quota()
                ->CreateMemoryAllocator("fuzzer"),
            Duration::Milliseconds(connect_timeout_ms));
      });
  return delay;
}

void ScheduleWrites(
    const fuzzer_input::NetworkInput& network_input,
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint,
    grpc_event_engine::experimental::FuzzingEventEngine* event_engine) {
  auto schedule = MakeSchedule(network_input);
  auto ep = std::shared_ptr<EventEngine::Endpoint>(std::move(endpoint));
  ReadForever(ep);
  ScheduleWritesForReads(ep, event_engine, std::move(schedule));
}

}  // namespace grpc_core
