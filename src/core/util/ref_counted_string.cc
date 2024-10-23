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

#include "src/core/util/ref_counted_string.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <string.h>

#include <new>

namespace grpc_core {

RefCountedPtr<RefCountedString> RefCountedString::Make(absl::string_view src) {
  void* p = gpr_malloc(sizeof(Header) + src.length() + 1);
  return RefCountedPtr<RefCountedString>(new (p) RefCountedString(src));
}

RefCountedString::RefCountedString(absl::string_view src)
    : header_{{}, src.length()} {
  memcpy(payload_, src.data(), header_.length);
  // Null terminate because we frequently need to convert to char* still to go
  // back and forth to the old c-style api.
  payload_[header_.length] = 0;
}

void RefCountedString::Destroy() { gpr_free(this); }

}  // namespace grpc_core
