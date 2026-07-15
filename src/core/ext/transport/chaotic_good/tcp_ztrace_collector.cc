
// Copyright 2025 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good/tcp_ztrace_collector.h"

#include "src/core/ext/transport/chaotic_good/frame_header.h"

namespace grpc_core::chaotic_good::tcp_ztrace_collector_detail {

void TcpFrameHeaderToJsonObject(const TcpFrameHeader& header,
                                Json::Object& object) {
  object["frame_type"] = Json::FromString(FrameTypeString(header.header.type));
  object["stream_id"] = Json::FromNumber(header.header.stream_id);
  object["payload_length"] = Json::FromNumber(header.header.payload_length);
  if (header.payload_tag != 0) {
    object["payload_tag"] = Json::FromNumber(header.payload_tag);
  }
}

void TcpDataFrameHeaderToJsonObject(const TcpDataFrameHeader& header,
                                    Json::Object& object) {
  object["payload_tag"] = Json::FromNumber(header.payload_tag);
  object["send_time"] = Json::FromNumber(header.send_timestamp);
  object["payload_length"] = Json::FromNumber(header.payload_length);
}

void MarkRead(bool read, Json::Object& object) {
  object["read"] = Json::FromBool(read);
}

}  // namespace grpc_core::chaotic_good::tcp_ztrace_collector_detail
