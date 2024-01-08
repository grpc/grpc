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

#include "src/core/lib/channel/filters.h"

namespace grpc_core {

size_t Filters::StackBuilder::OffsetForNextFilter(size_t alignment,
                                                  size_t size) {
  min_alignment_ = std::max(alignment, min_alignment_);
  if (current_call_offset_ % alignment != 0) {
    current_call_offset_ += alignment - current_call_offset_ % alignment;
  }
  const size_t offset = current_call_offset_;
  current_call_offset_ += size;
  return offset;
}

}  // namespace grpc_core
