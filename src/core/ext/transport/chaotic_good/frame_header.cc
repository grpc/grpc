// Copyright 2022 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good/frame_header.h"

#include <grpc/support/port_platform.h>

#include <cstdint>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {
namespace chaotic_good {

std::string FrameHeader::ToString() const {
  return absl::StrCat("[type:", type, " stream_id:", stream_id,
                      " payload_length:", payload_length, "]");
}

std::string FrameTypeString(FrameType type) {
  switch (type) {
    case FrameType::kSettings:
      return "Settings";
    case FrameType::kClientInitialMetadata:
      return "ClientInitialMetadata";
    case FrameType::kClientEndOfStream:
      return "ClientEndOfStream";
    case FrameType::kMessage:
      return "Message";
    case FrameType::kServerInitialMetadata:
      return "ServerInitialMetadata";
    case FrameType::kServerTrailingMetadata:
      return "ServerTrailingMetadata";
    case FrameType::kCancel:
      return "Cancel";
    case FrameType::kBeginMessage:
      return "BeginMessage";
    case FrameType::kMessageChunk:
      return "MessageChunk";
    case FrameType::kTcpSecurityFrame:
      return "TcpSecurityFrame";
  }
  return absl::StrCat("Unknown[0x", absl::Hex(static_cast<int>(type)), "]");
}

}  // namespace chaotic_good
}  // namespace grpc_core
