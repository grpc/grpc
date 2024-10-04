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

#include "src/core/lib/transport/message.h"

#include <grpc/impl/grpc_types.h>
#include <grpc/support/port_platform.h>

#include "absl/strings/str_cat.h"

namespace grpc_core {

std::string Message::DebugString() const {
  std::string out = absl::StrCat(payload_.Length(), "b");
  auto flags = flags_;
  auto explain = [&flags, &out](uint32_t flag, absl::string_view name) {
    if (flags & flag) {
      flags &= ~flag;
      absl::StrAppend(&out, ":", name);
    }
  };
  explain(GRPC_WRITE_BUFFER_HINT, "write_buffer");
  explain(GRPC_WRITE_NO_COMPRESS, "no_compress");
  explain(GRPC_WRITE_THROUGH, "write_through");
  explain(GRPC_WRITE_INTERNAL_COMPRESS, "compress");
  explain(GRPC_WRITE_INTERNAL_TEST_ONLY_WAS_COMPRESSED, "was_compressed");
  if (flags != 0) {
    absl::StrAppend(&out, ":huh=0x", absl::Hex(flags));
  }
  return out;
}

}  // namespace grpc_core
