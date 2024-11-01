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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CHAOTIC_GOOD_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CHAOTIC_GOOD_TRANSPORT_H

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <utility>

#include "absl/strings/escaping.h"
#include "src/core/ext/transport/chaotic_good/control_endpoint.h"
#include "src/core/ext/transport/chaotic_good/data_endpoints.h"
#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/match_promise.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

inline std::vector<PromiseEndpoint> OneDataEndpoint(PromiseEndpoint endpoint) {
  std::vector<PromiseEndpoint> ep;
  ep.emplace_back(std::move(endpoint));
  return ep;
}

class IncomingFrame {
 public:
  template <typename T>
  IncomingFrame(FrameHeader header, T payload, size_t remove_padding)
      : header_(header),
        payload_(std::move(payload)),
        remove_padding_(remove_padding) {}

  const FrameHeader& header() { return header_; }

  auto Payload() {
    return Map(
        MatchPromise(
            std::move(payload_),
            [](absl::StatusOr<SliceBuffer> status) { return status; },
            [](DataEndpoints::ReadTicket ticket) { return ticket.Await(); }),
        [remove_padding =
             remove_padding_](absl::StatusOr<SliceBuffer> payload) {
          if (payload.ok()) payload->RemoveLastNBytesNoInline(remove_padding);
          return payload;
        });
  }

 private:
  FrameHeader header_;
  absl::variant<absl::StatusOr<SliceBuffer>, DataEndpoints::ReadTicket>
      payload_;
  size_t remove_padding_;
};

class ChaoticGoodTransport : public RefCounted<ChaoticGoodTransport> {
 public:
  ChaoticGoodTransport(
      PromiseEndpoint control_endpoint,
      std::vector<PromiseEndpoint> data_endpoints,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine,
      uint32_t encode_alignment, uint32_t decode_alignment)
      : event_engine_(std::move(event_engine)),
        control_endpoint_(std::move(control_endpoint), event_engine_.get()),
        data_endpoints_(std::move(data_endpoints), event_engine_.get()),
        encode_alignment_(encode_alignment),
        decode_alignment_(decode_alignment) {}

  auto WriteFrame(const FrameInterface& frame) {
    FrameHeader header = frame.MakeHeader();
    GRPC_TRACE_LOG(chaotic_good, INFO)
        << "CHAOTIC_GOOD: WriteFrame to:"
        << ResolvedAddressToString(control_endpoint_.GetPeerAddress())
               .value_or("<<unknown peer address>>")
        << " " << frame.ToString();
    return If(
        data_endpoints_.empty() || header.payload_length < 128 * 1024,
        [this, &header, &frame]() {
          SliceBuffer output;
          header.Serialize(output.AddTiny(FrameHeader::kFrameHeaderSize));
          frame.SerializePayload(output);
          return control_endpoint_.Write(std::move(output));
        },
        [this, header, &frame]() mutable {
          SliceBuffer payload;
          // Temporarily give a bogus connection id to get padding right
          header.payload_connection_id = 1;
          const size_t padding = header.Padding(encode_alignment_);
          frame.SerializePayload(payload);
          GRPC_TRACE_LOG(chaotic_good, INFO)
              << "CHAOTIC_GOOD: Send " << payload.Length()
              << "b payload on data channel; add " << padding << " bytes for "
              << encode_alignment_ << " alignment";
          if (padding == 0) {
          } else if (padding < GRPC_SLICE_INLINED_SIZE) {
            memset(payload.AddTiny(padding), 0, padding);
          } else {
            auto slice = MutableSlice::CreateUninitialized(padding);
            memset(slice.data(), 0, padding);
            payload.AppendIndexed(Slice(std::move(slice)));
          }
          return Seq(data_endpoints_.Write(std::move(payload)),
                     [this, header](uint32_t connection_id) mutable {
                       header.payload_connection_id = connection_id + 1;
                       SliceBuffer header_frame;
                       header.Serialize(
                           header_frame.AddTiny(FrameHeader::kFrameHeaderSize));
                       return control_endpoint_.Write(std::move(header_frame));
                     });
        });
  }

  template <typename Frame>
  auto TransportWriteLoop(MpscReceiver<Frame>& outgoing_frames) {
    return Loop([self = Ref(), &outgoing_frames] {
      return TrySeq(
          // Get next outgoing frame.
          outgoing_frames.Next(),
          // Serialize and write it out.
          [self = self.get()](Frame client_frame) {
            return self->WriteFrame(GetFrameInterface(client_frame));
          },
          []() -> LoopCtl<absl::Status> {
            // The write failures will be caught in TrySeq and exit loop.
            // Therefore, only need to return Continue() in the last lambda
            // function.
            return Continue();
          });
    });
  }

  // Read frame header and payloads for control and data portions of one frame.
  // Resolves to StatusOr<IncomingFrame>.
  auto ReadFrameBytes() {
    return TrySeq(
        control_endpoint_.ReadSlice(FrameHeader::kFrameHeaderSize),
        [this](Slice read_buffer) {
          auto frame_header =
              FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
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
        [this](FrameHeader frame_header) {
          return If(
              frame_header.payload_connection_id == 0,
              [this, frame_header]() {
                return Map(control_endpoint_.Read(frame_header.payload_length),
                           [frame_header](absl::StatusOr<SliceBuffer> payload)
                               -> absl::StatusOr<IncomingFrame> {
                             if (!payload.ok()) return payload.status();
                             return IncomingFrame(frame_header,
                                                  std::move(payload), 0);
                           });
              },
              [this, frame_header]() -> absl::StatusOr<IncomingFrame> {
                const auto padding = frame_header.Padding(decode_alignment_);
                return IncomingFrame(
                    frame_header,
                    data_endpoints_.Read(frame_header.payload_connection_id - 1,
                                         frame_header.payload_length + padding),
                    padding);
              });
        });
  }

  template <typename T>
  absl::StatusOr<T> DeserializeFrame(const FrameHeader& header,
                                     SliceBuffer payload) {
    T frame;
    GRPC_TRACE_LOG(chaotic_good, INFO)
        << "CHAOTIC_GOOD: Deserialize " << header << " with payload "
        << absl::CEscape(payload.JoinIntoString());
    CHECK_EQ(header.payload_length, payload.Length());
    auto s = frame.Deserialize(header, std::move(payload));
    GRPC_TRACE_LOG(chaotic_good, INFO)
        << "CHAOTIC_GOOD: DeserializeFrame "
        << (s.ok() ? frame.ToString() : s.ToString());
    if (s.ok()) return std::move(frame);
    return std::move(s);
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  ControlEndpoint control_endpoint_;
  DataEndpoints data_endpoints_;
  const uint32_t encode_alignment_;
  const uint32_t decode_alignment_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CHAOTIC_GOOD_TRANSPORT_H
