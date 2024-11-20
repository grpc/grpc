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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"

bool squelch = false;

using grpc_core::chaotic_good::FrameHeader;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size != FrameHeader::kFrameHeaderSize) return 0;
  auto r = FrameHeader::Parse(data);
  if (!r.ok()) return 0;
  uint8_t reserialized[FrameHeader::kFrameHeaderSize];
  r->Serialize(reserialized);
  // If it parses, we insist that the bytes reserialize to the same thing.
  if (memcmp(data, reserialized, FrameHeader::kFrameHeaderSize) != 0) {
    auto esc = [](const void* s) {
      return absl::CEscape(absl::string_view(static_cast<const char*>(s),
                                             FrameHeader::kFrameHeaderSize));
    };
    fprintf(stderr, "Failed:\nin:  %s\nout: %s\nser: %s\n", esc(data).c_str(),
            esc(reserialized).c_str(), r->ToString().c_str());
    abort();
  }
  return 0;
}
