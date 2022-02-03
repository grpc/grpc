// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/parsed_metadata.h"

namespace grpc_core {
namespace metadata_detail {

std::string MakeDebugString(absl::string_view key, absl::string_view value) {
  return absl::StrCat(key, ": ", value);
}

Slice SliceFromBuffer(const Buffer& buffer) {
  return Slice(grpc_slice_ref_internal(buffer.slice));
}

void DestroySliceValue(const Buffer& value) {
  grpc_slice_unref_internal(value.slice);
}

void DestroyTrivialMemento(const Buffer&) {}

}  // namespace metadata_detail
}  // namespace grpc_core
