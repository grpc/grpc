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

#include "src/core/ext/transport/chaotic_good/tcp_frame_transport.h"

#include <sys/types.h>

#include <cstdint>

#include "src/core/ext/transport/chaotic_good/control_endpoint.h"
#include "src/core/ext/transport/chaotic_good/frame_transport.h"
#include "src/core/ext/transport/chaotic_good/serialize_little_endian.h"
#include "src/core/ext/transport/chaotic_good/tcp_ztrace_collector.h"
#include "src/core/ext/transport/chaotic_good/transport_context.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/transport/transport_framing_endpoint_extension.h"

namespace grpc_core {
namespace chaotic_good {

namespace {
TransportFramingEndpointExtension* GetTransportFramingEndpointExtension(
    grpc_event_engine::experimental::EventEngine::Endpoint& endpoint) {
  return grpc_event_engine::experimental::QueryExtension<
      TransportFramingEndpointExtension>(&endpoint);
}
}  // namespace

///////////////////////////////////////////////////////////////////////////////
// TcpFrameTransport

TcpFrameTransport::TcpFrameTransport(
    Options options, PromiseEndpoint control_endpoint,
    std::vector<PendingConnection> pending_data_endpoints,
    TransportContextPtr ctx)
    : DataSource(ctx->socket_node),
      ctx_(ctx),
      control_endpoint_(std::move(control_endpoint), ctx, ztrace_collector_),
      data_endpoints_(std::move(pending_data_endpoints), ctx,
                      options.encode_alignment, options.decode_alignment,
                      ztrace_collector_, options.enable_tracing),
      options_(options) {
  auto* transport_framing_endpoint_extension =
      GetTransportFramingEndpointExtension(
          *control_endpoint_.GetEventEngineEndpoint());
  if (transport_framing_endpoint_extension != nullptr) {
    transport_framing_endpoint_extension->SetSendFrameCallback(
        control_endpoint_.SecureFrameWriterCallback());
  }
}

auto TcpFrameTransport::WriteFrame(const FrameInterface& frame,
                                   std::shared_ptr<TcpCallTracer> call_tracer) {
  FrameHeader header = frame.MakeHeader();
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: WriteFrame to:"
      << ResolvedAddressToString(control_endpoint_.GetPeerAddress())
             .value_or("<<unknown peer address>>")
      << " " << frame.ToString();
  return If(
      // If we have no data endpoints, OR this is a small payload
      data_endpoints_.empty() ||
          header.payload_length <= options_.inlined_payload_size_threshold,
      // ... then write it to the control endpoint
      [this, &header, &frame]() {
        SliceBuffer output;
        TcpFrameHeader hdr{header, 0};
        GRPC_TRACE_LOG(chaotic_good, INFO)
            << "CHAOTIC_GOOD: Send control frame " << hdr.ToString();
        ztrace_collector_->Append(WriteFrameHeaderTrace{hdr});
        hdr.Serialize(output.AddTiny(TcpFrameHeader::kFrameHeaderSize));
        frame.SerializePayload(output);
        return control_endpoint_.Write(std::move(output));
      },
      // ... otherwise write it to a data connection
      [this, header, &frame, &call_tracer]() mutable {
        SliceBuffer control_bytes;
        SliceBuffer data_bytes;
        auto tag = next_payload_tag_;
        ++next_payload_tag_;
        TcpFrameHeader hdr{header, tag};
        GRPC_TRACE_LOG(chaotic_good, INFO)
            << "CHAOTIC_GOOD: Send control frame " << hdr.ToString();
        hdr.Serialize(control_bytes.AddTiny(TcpFrameHeader::kFrameHeaderSize));
        frame.SerializePayload(data_bytes);
        ztrace_collector_->Append(WriteFrameHeaderTrace{hdr});
        return DiscardResult(
            Join(data_endpoints_.Write(tag, std::move(data_bytes),
                                       std::move(call_tracer)),
                 control_endpoint_.Write(std::move(control_bytes))));
      });
}

auto TcpFrameTransport::WriteLoop(MpscReceiver<OutgoingFrame> frames) {
  return Loop([self = RefAsSubclass<TcpFrameTransport>(),
               frames = std::move(frames)]() mutable {
    return TrySeq(
        // Get next outgoing frame.
        frames.Next(),
        // Serialize and write it out.
        [self = self.get()](OutgoingFrame outgoing_frame) {
          return self->WriteFrame(
              absl::ConvertVariantTo<FrameInterface&>(outgoing_frame.payload),
              std::move(outgoing_frame.call_tracer));
        },
        []() -> LoopCtl<absl::Status> {
          // The write failures will be caught in TrySeq and exit
          // loop. Therefore, only need to return Continue() in the
          // last lambda function.
          return Continue();
        });
  });
}

auto TcpFrameTransport::ReadFrameBytes() {
  return Loop([this]() {
    return TrySeq(
        control_endpoint_.ReadSlice(TcpFrameHeader::kFrameHeaderSize),
        [this](Slice read_buffer) {
          auto frame_header =
              TcpFrameHeader::Parse(reinterpret_cast<const uint8_t*>(
                  GRPC_SLICE_START_PTR(read_buffer.c_slice())));
          GRPC_TRACE_LOG(chaotic_good, INFO)
              << "CHAOTIC_GOOD: ReadHeader from:"
              << ResolvedAddressToString(control_endpoint_.GetPeerAddress())
                     .value_or("<<unknown peer address>>")
              << " "
              << (frame_header.ok() ? frame_header->ToString()
                                    : frame_header.status().ToString());
          return frame_header;
        },
        [this](TcpFrameHeader frame_header) {
          ztrace_collector_->Append(ReadFrameHeaderTrace{frame_header});
          return If(
              // If the payload is on the connection frame
              frame_header.payload_tag == 0,
              // ... then read the data immediately and return an IncomingFrame
              //     that contains the payload.
              // We need to do this here so that we do not create head of line
              // blocking issues reading later control frames (but waiting for a
              // call to get scheduled time to read the payload).
              [this, frame_header]() {
                return Map(
                    control_endpoint_.Read(frame_header.header.payload_length),
                    [frame_header, this](absl::StatusOr<SliceBuffer> payload)
                        -> absl::StatusOr<LoopCtl<IncomingFrame>> {
                      if (!payload.ok()) return payload.status();
                      if (frame_header.header.type ==
                          FrameType::kTcpSecurityFrame) {
                        auto* transport_framing_endpoint_extension =
                            GetTransportFramingEndpointExtension(
                                *control_endpoint_.GetEventEngineEndpoint());
                        if (transport_framing_endpoint_extension != nullptr) {
                          transport_framing_endpoint_extension->ReceiveFrame(
                              std::move(*payload));
                        }
                        // Loop around and read the next frame, since we're not
                        // reporting the security frame to the upper layer.
                        return Continue{};
                      }
                      return IncomingFrame(frame_header.header,
                                           std::move(payload));
                    });
              },
              // ... otherwise issue a read to the appropriate data endpoint,
              //     which will return a read ticket - which can be used later
              //     in the call promise to asynchronously wait for those bytes
              //     to be available.
              [this, frame_header]() -> absl::StatusOr<LoopCtl<IncomingFrame>> {
                if (frame_header.header.type == FrameType::kTcpSecurityFrame) {
                  return absl::UnavailableError(
                      "Security frame sent with a payload tag");
                }
                return IncomingFrame(
                    frame_header.header,
                    data_endpoints_.Read(frame_header.payload_tag).Await());
              });
        });
  });
}

template <typename Promise>
auto TcpFrameTransport::UntilClosed(Promise promise) {
  return Race(Map(closed_.Wait(),
                  [self = RefAsSubclass<TcpFrameTransport>()](Empty) {
                    return absl::UnavailableError("Frame transport closed");
                  }),
              data_endpoints_.AwaitClosed(), std::move(promise));
}

void TcpFrameTransport::Start(Party* party, MpscReceiver<OutgoingFrame> frames,
                              RefCountedPtr<FrameTransportSink> sink) {
  party->Spawn(
      "tcp-write",
      [self = RefAsSubclass<TcpFrameTransport>(),
       frames = std::move(frames)]() mutable {
        return self->UntilClosed(self->WriteLoop(std::move(frames)));
      },
      [sink, ztrace_collector = ztrace_collector_](absl::Status status) {
        ztrace_collector->Append(TransportError</*read=*/false>{status});
        sink->OnFrameTransportClosed(std::move(status));
      });
  party->Spawn(
      "tcp-read",
      [self = RefAsSubclass<TcpFrameTransport>(), sink = sink]() {
        return self->UntilClosed(Loop([self = self.get(), sink = sink.get()]() {
          return TrySeq(
              self->ReadFrameBytes(),
              [sink](IncomingFrame incoming_frame) -> LoopCtl<absl::Status> {
                sink->OnIncomingFrame(std::move(incoming_frame));
                return Continue{};
              });
        }));
      },
      [sink, ztrace_collector = ztrace_collector_](absl::Status status) {
        ztrace_collector->Append(TransportError</*read=*/true>{status});
        sink->OnFrameTransportClosed(std::move(status));
      });
}

void TcpFrameTransport::Orphan() {
  ztrace_collector_->Append(OrphanTrace{});
  closed_.Set();
  Unref();
}

void TcpFrameTransport::AddData(channelz::DataSink& sink) {
  Json::Object options;
  options["encode_alignment"] = Json::FromNumber(options_.encode_alignment);
  options["decode_alignment"] = Json::FromNumber(options_.decode_alignment);
  options["inlined_payload_size_threshold"] =
      Json::FromNumber(options_.inlined_payload_size_threshold);
  options["enable_tracing"] = Json::FromBool(options_.enable_tracing);
  sink.AddAdditionalInfo("chaoticGoodTcpOptions", std::move(options));
}

RefCountedPtr<channelz::SocketNode> TcpFrameTransport::MakeSocketNode(
    const ChannelArgs& args, const PromiseEndpoint& endpoint) {
  std::string peer_string =
      grpc_event_engine::experimental::ResolvedAddressToString(
          endpoint.GetPeerAddress())
          .value_or("unknown");
  return MakeRefCounted<channelz::SocketNode>(
      grpc_event_engine::experimental::ResolvedAddressToString(
          endpoint.GetLocalAddress())
          .value_or("unknown"),
      peer_string, absl::StrCat("chaotic-good ", peer_string),
      args.GetObjectRef<channelz::SocketNode::Security>());
}

}  // namespace chaotic_good
}  // namespace grpc_core
