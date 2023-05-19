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

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_observability {

class PythonOpenCensusServerCallTracerFactory
    : public grpc_core::ServerCallTracerFactory {
 public:
  grpc_core::ServerCallTracer* CreateNewServerCallTracer(
      grpc_core::Arena* arena) override;
};

inline absl::string_view GetMethod(const grpc_core::Slice& path) {
  if (path.empty()) {
    return "";
  }
  // Check for leading '/' and trim it if present.
  return absl::StripPrefix(path.as_string_view(), "/");
}

}  // namespace grpc_observability
