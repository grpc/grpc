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

#include "src/core/ext/transport/chaotic_good/frame_header.h"

bool squelch = false;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size != 24) return 0;
  auto r = grpc_core::chaotic_good::FrameHeader::Parse(data);
  if (!r.ok()) return 0;
  uint8_t reserialized[24];
  r->Serialize(reserialized);
  // If it parses, we insist that the bytes reserialize to the same thing.
  if (memcmp(data, reserialized, 24) != 0) abort();
  return 0;
}
