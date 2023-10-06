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

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "absl/types/span.h"

#include <grpc/slice.h>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/varint.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "test/core/end2end/fuzzers/fuzzer_input.pb.h"
#include "test/core/util/mock_endpoint.h"

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
  for (const auto& header : headers.headers()) {
    temp.push_back(0);
    AppendLength(header.key().length(), &temp);
    temp.insert(temp.end(), header.key().begin(), header.key().end());
    AppendLength(header.value().length(), &temp);
    temp.insert(temp.end(), header.value().begin(), header.value().end());
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
    case fuzzer_input::InputSegment::kClientPrefix:
      return grpc_slice_from_static_string("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n");
    case fuzzer_input::InputSegment::PAYLOAD_NOT_SET:
      break;
  }
  return grpc_empty_slice();
}
}  // namespace

void ScheduleReads(
    const fuzzer_input::NetworkInput& network_input,
    grpc_endpoint* mock_endpoint,
    grpc_event_engine::experimental::FuzzingEventEngine* event_engine) {
  switch (network_input.value_case()) {
    case fuzzer_input::NetworkInput::kSingleReadBytes: {
      grpc_mock_endpoint_put_read(
          mock_endpoint, grpc_slice_from_copied_buffer(
                             network_input.single_read_bytes().data(),
                             network_input.single_read_bytes().size()));
      grpc_mock_endpoint_finish_put_reads(mock_endpoint);
    } break;
    case fuzzer_input::NetworkInput::kInputSegments: {
      int delay_ms = 0;
      for (const auto& segment : network_input.input_segments().segments()) {
        delay_ms += Clamp(segment.delay_ms(), 0, 1000);
        event_engine->RunAfterExactly(
            std::chrono::milliseconds(delay_ms), [mock_endpoint, segment] {
              grpc_mock_endpoint_put_read(mock_endpoint,
                                          SliceFromSegment(segment));
            });
      }
      event_engine->RunAfterExactly(
          std::chrono::milliseconds(delay_ms + 1), [mock_endpoint] {
            grpc_mock_endpoint_finish_put_reads(mock_endpoint);
          });
    } break;
    case fuzzer_input::NetworkInput::VALUE_NOT_SET:
      grpc_mock_endpoint_finish_put_reads(mock_endpoint);
      break;
  }
}

}  // namespace grpc_core
