//
//
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
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/uuid_v4.h"

#include <initializer_list>

#include "absl/strings/str_format.h"

namespace grpc_core {

std::string GenerateUUIDv4(uint64_t hi, uint64_t lo) {
  uint32_t time_low = hi >> 32;
  uint16_t time_mid = hi >> 16;
  uint16_t time_hi_and_version = (hi & 0x0fff) | 0x4000;
  uint16_t clock_seq_hi_low = ((lo >> 48) & 0x3fff) | 0x8000;
  uint64_t node = lo & 0xffffffffffff;
  return absl::StrFormat("%08x-%04x-%04x-%04x-%012x", time_low, time_mid,
                         time_hi_and_version, clock_seq_hi_low, node);
}

}  // namespace grpc_core
